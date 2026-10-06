// GBM allocation and DMA-BUF export on a real DRM device (Linux).
//
// Oracle: the kernel. Pixels written through gbm_bo_map() must be readable by
// mmap()ing the exported DMA-BUF (bracketed by DMA_BUF_IOCTL_SYNC), and the
// other way round, so the exported fd, stride and offset really describe the
// buffer GBM allocated. The exported fd must outlive the buffer object, and
// re-importing it must yield a buffer with the same geometry.
#include "check.h"
#include "drm_node.h"
#include "brodmabuf/allocator.h"
#include "brodmabuf/gbm.h"

#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace brodmabuf;

namespace {

constexpr uint32_t kW = 64;
constexpr uint32_t kH = 48;

uint32_t pattern(uint32_t x, uint32_t y, uint32_t salt) {
    return 0xff000000u | ((y & 0xff) << 16) | ((x & 0xff) << 8) | (salt & 0xff);
}

// Maps a single-plane linear DMA-BUF for CPU access, as any importer would.
class DmaBufMap {
public:
    DmaBufMap(int fd, size_t len, uint64_t sync_flags) : fd_(fd), len_(len), flags_(sync_flags) {
        ptr_ = ::mmap(nullptr, len_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr_ == MAP_FAILED) {
            error_ = std::strerror(errno);
            ptr_ = nullptr;
            return;
        }
        dma_buf_sync s{DMA_BUF_SYNC_START | flags_};
        ::ioctl(fd_, DMA_BUF_IOCTL_SYNC, &s);
    }
    ~DmaBufMap() {
        if (!ptr_) return;
        dma_buf_sync s{DMA_BUF_SYNC_END | flags_};
        ::ioctl(fd_, DMA_BUF_IOCTL_SYNC, &s);
        ::munmap(ptr_, len_);
    }
    uint8_t* data() const { return static_cast<uint8_t*>(ptr_); }
    const std::string& error() const { return error_; }

private:
    int fd_;
    size_t len_;
    uint64_t flags_;
    void* ptr_ = nullptr;
    std::string error_;
};

std::unique_ptr<GbmBuffer> linear_buffer(GbmDevice& dev, uint32_t format) {
    auto bo = dev.create_buffer_with_modifiers(kW, kH, format, {DRM_FORMAT_MOD_LINEAR});
    if (bo.ok()) return std::move(bo.value());
    bstest::skip_check("create_buffer_with_modifiers(LINEAR)", std::string(bo.error_message()) +
                                                                "; using GBM_BO_USE_LINEAR instead");
    auto flagged = dev.create_buffer(kW, kH, format, GBM_BO_USE_LINEAR);
    if (flagged.ok()) return std::move(flagged.value());
    return nullptr;
}

void test_allocate_export_import(GbmDevice& dev) {
    auto bo_res = dev.create_buffer(kW, kH, DRM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    REQUIRE_OK(bo_res);
    auto bo = std::move(bo_res.value());
    CHECK(bo->valid());
    CHECK_EQ(bo->width(), kW);
    CHECK_EQ(bo->height(), kH);
    CHECK_EQ(bo->format(), uint32_t(DRM_FORMAT_XRGB8888));
    CHECK(bo->stride() >= kW * 4);
    CHECK(bo->plane_count() >= 1);
    CHECK(dev.is_format_supported(DRM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING));

    auto attrs_res = bo->export_dmabuf();
    REQUIRE_OK(attrs_res);
    DmaBufAttributes attrs = std::move(attrs_res.value());
    CHECK(attrs.is_valid());
    CHECK_EQ(attrs.width, kW);
    CHECK_EQ(attrs.height, kH);
    CHECK_EQ(attrs.drm_format, uint32_t(DRM_FORMAT_XRGB8888));
    CHECK_EQ(attrs.modifier, bo->modifier());
    REQUIRE(!attrs.planes.empty());
    CHECK_EQ(attrs.planes[0].stride, bo->stride(0));
    // The DMA-BUF is at least as large as the pixels it describes.
    CHECK(attrs.planes[0].size >= uint64_t(attrs.planes[0].stride) * kH);
    std::printf("XRGB8888 %ux%u: stride %u, modifier %s, %zu plane(s), %llu bytes\n", kW, kH,
                attrs.planes[0].stride, drm_modifier_to_string(attrs.modifier).c_str(), attrs.planes.size(),
                static_cast<unsigned long long>(attrs.planes[0].size));

    // The exported descriptor keeps the memory alive after the bo is gone.
    const int fd0 = attrs.planes[0].fd.get();
    bo.reset();
    struct stat st{};
    CHECK_EQ(::fstat(fd0, &st), 0);

    auto imported = dev.import_buffer(attrs);
    REQUIRE_OK(imported);
    CHECK_EQ(imported.value()->width(), kW);
    CHECK_EQ(imported.value()->height(), kH);
    CHECK_EQ(imported.value()->format(), uint32_t(DRM_FORMAT_XRGB8888));
    CHECK_EQ(imported.value()->stride(), attrs.planes[0].stride);

    // An inconsistent description is refused before it reaches the driver.
    DmaBufAttributes broken = attrs.dup();
    broken.planes[0].stride = 0;
    CHECK(!dev.import_buffer(broken).ok());
}

void test_pixels_through_dmabuf(GbmDevice& dev) {
    auto bo = linear_buffer(dev, DRM_FORMAT_ARGB8888);
    REQUIRE(bo != nullptr);
    CHECK_EQ(bo->modifier() == DRM_FORMAT_MOD_INVALID ? uint64_t(DRM_FORMAT_MOD_LINEAR) : bo->modifier(),
             uint64_t(DRM_FORMAT_MOD_LINEAR));

    auto attrs_res = bo->export_dmabuf();
    REQUIRE_OK(attrs_res);
    const DmaBufAttributes& attrs = attrs_res.value();
    REQUIRE(attrs.planes.size() == 1);
    const uint32_t stride = attrs.planes[0].stride;
    const uint32_t offset = attrs.planes[0].offset;
    const size_t len = static_cast<size_t>(offset) + size_t(stride) * kH;

    // GBM writes, the DMA-BUF reads.
    {
        auto map = bo->map(0, 0, kW, kH, GBM_BO_TRANSFER_WRITE);
        REQUIRE(map && map->valid());
        for (uint32_t y = 0; y < kH; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(map->data()) + size_t(y) * map->stride());
            for (uint32_t x = 0; x < kW; ++x) row[x] = pattern(x, y, 1);
        }
    }
    {
        DmaBufMap m(attrs.planes[0].fd.get(), len, DMA_BUF_SYNC_READ);
        if (!m.data()) {
            bstest::fail(__FILE__, __LINE__, "mmap of the exported DMA-BUF failed: " + m.error());
            return;
        }
        int bad = 0;
        for (uint32_t y = 0; y < kH; ++y) {
            auto* row = reinterpret_cast<const uint32_t*>(m.data() + offset + size_t(y) * stride);
            for (uint32_t x = 0; x < kW; ++x) bad += row[x] != pattern(x, y, 1);
        }
        CHECK_EQ(bad, 0);
    }

    // The DMA-BUF writes, GBM reads.
    {
        DmaBufMap m(attrs.planes[0].fd.get(), len, DMA_BUF_SYNC_WRITE);
        REQUIRE(m.data() != nullptr);
        for (uint32_t y = 0; y < kH; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(m.data() + offset + size_t(y) * stride);
            for (uint32_t x = 0; x < kW; ++x) row[x] = pattern(x, y, 2);
        }
    }
    {
        auto map = bo->map(0, 0, kW, kH, GBM_BO_TRANSFER_READ);
        REQUIRE(map && map->valid());
        int bad = 0;
        for (uint32_t y = 0; y < kH; ++y) {
            auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(map->data()) +
                                                          size_t(y) * map->stride());
            for (uint32_t x = 0; x < kW; ++x) bad += row[x] != pattern(x, y, 2);
        }
        CHECK_EQ(bad, 0);
    }
}

void test_allocator(const std::string& node) {
    auto alloc_res = DmaBufAllocator::create_gbm(node);
    REQUIRE_OK(alloc_res);
    auto& alloc = alloc_res.value();
    CHECK(alloc->is_valid());
    CHECK(alloc->drm_fd() >= 0);
    CHECK(platform_status().is_ok());

    auto attrs = alloc->allocate(kW, kH, DRM_FORMAT_ARGB8888);
    REQUIRE_OK(attrs);
    CHECK(attrs.value().is_valid());
    CHECK_EQ(attrs.value().width, kW);
    CHECK(attrs.value().modifier != DRM_FORMAT_MOD_INVALID);

    auto buffer = alloc->allocate_buffer(kW, kH, DRM_FORMAT_XRGB8888, {DRM_FORMAT_MOD_LINEAR});
    REQUIRE_OK(buffer);
    CHECK_EQ(buffer.value()->height(), kH);

    auto dflt = DmaBufAllocator::create_default();
    CHECK_OK(dflt);
}

}  // namespace

int main() {
    const std::string node = bstest::drm_node();
    if (node.empty()) bstest::skip("test_gbm", "no accessible DRM render or card node");
    std::printf("DRM node: %s\n", node.c_str());

    auto dev_res = GbmDevice::open(node);
    if (!dev_res.ok()) {
        bstest::skip("test_gbm", "GBM has no backend for " + node + ": " + std::string(dev_res.error_message()));
    }
    auto dev = std::move(dev_res.value());
    CHECK(dev->valid());
    CHECK(dev->drm_fd() >= 0);
    CHECK(!GbmDevice::open("/dev/dri/does-not-exist").ok());

    test_allocate_export_import(*dev);
    test_pixels_through_dmabuf(*dev);
    test_allocator(node);
    return bstest::finish("test_gbm");
}
