// DmaBufPlane / DmaBufAttributes and the plane layout math. Validity, dup()
// and sizes run on every platform with ordinary descriptors standing in for
// buffer memory (a DmaBufAttributes only holds descriptors); whether planes
// share one memory object is checked on Linux and macOS with descriptors that
// do and do not refer to the same file. Real DMA-BUFs go through these types
// in test_gbm and test_vulkan.
#include "check.h"
#include "brodmabuf/buffer.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/mman.h>
#endif

using namespace brodmabuf;

namespace {

// An open descriptor of the platform's cheapest kind. On Linux a sized memfd
// (memory, as a DMA-BUF is); elsewhere one end of a pipe.
UniqueFd make_fd(uint64_t size) {
#if defined(__linux__)
    int fd = ::memfd_create("brodmabuf_test", MFD_CLOEXEC);
    if (fd >= 0 && size > 0 && ::ftruncate(fd, static_cast<off_t>(size)) != 0) {
        ::close(fd);
        return UniqueFd();
    }
    return UniqueFd(fd);
#else
    (void)size;
    int fds[2] = {-1, -1};
#if defined(_WIN32)
    if (::_pipe(fds, 256, _O_BINARY) != 0) return UniqueFd();
    ::_close(fds[1]);
#else
    if (::pipe(fds) != 0) return UniqueFd();
    ::close(fds[1]);
#endif
    return UniqueFd(fds[0]);
#endif
}

void test_plane() {
    UniqueFd fd = make_fd(4096);
    REQUIRE(fd.valid());
    const int raw = fd.get();
    DmaBufPlane plane(std::move(fd), 1920 * 4, 64, 4096);
    CHECK_EQ(plane.fd.get(), raw);
    CHECK_EQ(plane.stride, 1920u * 4);
    CHECK_EQ(plane.offset, 64u);
    CHECK_EQ(plane.size, uint64_t(4096));

    DmaBufPlane copy = plane.dup();
    CHECK(copy.fd.valid());
    CHECK(copy.fd.get() != plane.fd.get());
    CHECK_EQ(copy.stride, plane.stride);
    CHECK_EQ(copy.offset, plane.offset);
    CHECK_EQ(copy.size, plane.size);
}

void test_validity() {
    DmaBufAttributes a;
    CHECK(!a.is_valid());
    CHECK_EQ(a.total_size(), uint64_t(0));

    a.width = 64;
    a.height = 32;
    a.drm_format = DRM_FORMAT_XRGB8888;
    a.modifier = DRM_FORMAT_MOD_LINEAR;
    CHECK(!a.is_valid());  // no planes
    a.planes.emplace_back(make_fd(64 * 32 * 4), 64 * 4, 0, 64 * 32 * 4);
    REQUIRE(a.planes[0].fd.valid());
    CHECK(a.is_valid());
    CHECK(!a.is_disjoint());
    CHECK_EQ(a.total_size(), uint64_t(64 * 32 * 4));

    DmaBufAttributes copy = a.dup();
    CHECK(copy.is_valid());
    CHECK_EQ(copy.width, a.width);
    CHECK_EQ(copy.height, a.height);
    CHECK_EQ(copy.drm_format, a.drm_format);
    CHECK_EQ(copy.modifier, a.modifier);
    REQUIRE(copy.planes.size() == 1);
    CHECK(copy.planes[0].fd.get() != a.planes[0].fd.get());

    // Each broken invariant on its own makes the description invalid.
    DmaBufAttributes z = a.dup();
    z.width = 0;
    CHECK(!z.is_valid());
    DmaBufAttributes f = a.dup();
    f.drm_format = DRM_FORMAT_INVALID;
    CHECK(!f.is_valid());
    DmaBufAttributes s = a.dup();
    s.planes[0].stride = 0;
    CHECK(!s.is_valid());
    DmaBufAttributes c = a.dup();
    c.planes[0].fd.reset();
    CHECK(!c.is_valid());
    // A linear XRGB8888 buffer has exactly one plane.
    DmaBufAttributes extra = a.dup();
    extra.planes.push_back(a.planes[0].dup());
    CHECK(!extra.is_valid());

    // NV12 needs its two planes.
    DmaBufAttributes nv;
    nv.width = 64;
    nv.height = 32;
    nv.drm_format = DRM_FORMAT_NV12;
    nv.modifier = DRM_FORMAT_MOD_LINEAR;
    nv.planes.emplace_back(make_fd(64 * 48), 64, 0, 0);
    CHECK(!nv.is_valid());
    nv.planes.emplace_back(nv.planes[0].fd.dup(), 64, 64 * 32, 0);
    CHECK(nv.is_valid());
    // Both planes in one memory object (dup'd descriptors of one file): the
    // size is the span to the end of the chroma plane.
    CHECK(!nv.is_disjoint());
    CHECK_EQ(nv.total_size(), uint64_t(64 * 32 + 64 * 16));
}

void test_disjoint() {
#if defined(_WIN32)
    bstest::skip_check("is_disjoint", "Windows descriptors carry no file identity; is_disjoint() is false there");
#else
    DmaBufAttributes nv;
    nv.width = 64;
    nv.height = 32;
    nv.drm_format = DRM_FORMAT_NV12;
    nv.modifier = DRM_FORMAT_MOD_LINEAR;
    nv.planes.emplace_back(make_fd(64 * 32), 64, 0, 64 * 32);
    nv.planes.emplace_back(make_fd(64 * 16), 64, 0, 64 * 16);
    REQUIRE(nv.planes[0].fd.valid() && nv.planes[1].fd.valid());
    CHECK(nv.is_valid());
    CHECK(nv.is_disjoint());
    CHECK_EQ(nv.total_size(), uint64_t(64 * 32 + 64 * 16));
#endif
}

void test_layout_math() {
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_XRGB8888, 1920, 0), 1920u * 4);
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_RGB565, 1920, 0), 1920u * 2);
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_ABGR16161616F, 10, 0), 80u);
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_XRGB8888, 1920, 1), 0u);  // no plane 1

    // NV12: luma 1 byte per pixel, interleaved chroma 2 bytes per 2x2 block.
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_NV12, 1920, 0), 1920u);
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_NV12, 1920, 1), 1920u);
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_NV12, 1921, 1), 1922u);
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_NV24, 100, 1), 200u);
    // Three-plane: one byte per sample.
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_YUV420, 1920, 1), 960u);
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_YUV410, 1920, 2), 480u);
    // P010: 16-bit samples.
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_P010, 1920, 0), 3840u);
    CHECK_EQ(calculate_min_stride(DRM_FORMAT_P010, 1921, 1), 3844u);
    // An unknown format has no honest minimum.
    CHECK_EQ(calculate_min_stride(make_fourcc('Q', 'Q', 'Q', 'Q'), 64, 0), 0u);

    CHECK_EQ(calculate_min_plane_size(DRM_FORMAT_XRGB8888, 1920, 1080, 0, 7680), uint64_t(7680) * 1080);
    CHECK_EQ(calculate_min_plane_size(DRM_FORMAT_NV12, 1920, 1080, 0, 1920), uint64_t(1920) * 1080);
    CHECK_EQ(calculate_min_plane_size(DRM_FORMAT_NV12, 1920, 1080, 1, 1920), uint64_t(1920) * 540);
    CHECK_EQ(calculate_min_plane_size(DRM_FORMAT_NV12, 1920, 1081, 1, 1920), uint64_t(1920) * 541);
    CHECK_EQ(calculate_min_plane_size(DRM_FORMAT_NV12, 1920, 1080, 2, 1920), uint64_t(0));
}

}  // namespace

int main() {
    test_plane();
    test_validity();
    test_disjoint();
    test_layout_math();
    return bstest::finish("test_buffer");
}
