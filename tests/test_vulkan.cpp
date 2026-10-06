// DMA-BUF import into and export from Vulkan (Linux).
//
// Oracle: pixels. A linear buffer GBM allocated and filled on the CPU is
// imported as a VkImage and copied out by the GPU; the bytes must match. A
// VkImage cleared by the GPU and exported must import into GBM, and when it is
// linear its pixels must read back as the clear colour through gbm_bo_map().
// Runs on any Vulkan driver with VK_EXT_external_memory_dma_buf and
// VK_EXT_image_drm_format_modifier (radv, anv, lavapipe on udmabuf, ...).
#include "check.h"
#include "drm_node.h"
#include "brodmabuf/gbm.h"
#include "brodmabuf/vulkan.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace brodmabuf;

namespace {

constexpr uint32_t kW = 64;
constexpr uint32_t kH = 48;

uint32_t pattern(uint32_t x, uint32_t y) {
    return 0xff000000u | ((y & 0xff) << 16) | ((x & 0xff) << 8) | 0x5a;
}

// A host-visible buffer, a command buffer and a one-shot submit on the
// context's queue: just enough Vulkan to move pixels in and out of an image.
class Gpu {
public:
    explicit Gpu(const VulkanContext& vk) : vk_(vk) {
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = vk.queue_family();
        ok_ = vkCreateCommandPool(vk.device(), &pci, nullptr, &pool_) == VK_SUCCESS;
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool_;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        ok_ = ok_ && vkAllocateCommandBuffers(vk.device(), &cai, &cmd_) == VK_SUCCESS;

        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = size_t(kW) * kH * 4;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        ok_ = ok_ && vkCreateBuffer(vk.device(), &bci, nullptr, &buffer_) == VK_SUCCESS;
        VkMemoryRequirements req{};
        if (ok_) vkGetBufferMemoryRequirements(vk.device(), buffer_, &req);
        uint32_t type = vk.find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ok_ = ok_ && type != UINT32_MAX;
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = type;
        ok_ = ok_ && vkAllocateMemory(vk.device(), &mai, nullptr, &memory_) == VK_SUCCESS;
        ok_ = ok_ && vkBindBufferMemory(vk.device(), buffer_, memory_, 0) == VK_SUCCESS;
    }
    ~Gpu() {
        if (memory_) vkFreeMemory(vk_.device(), memory_, nullptr);
        if (buffer_) vkDestroyBuffer(vk_.device(), buffer_, nullptr);
        if (pool_) vkDestroyCommandPool(vk_.device(), pool_, nullptr);
    }
    bool ok() const { return ok_; }

    VkCommandBuffer begin() {
        vkResetCommandBuffer(cmd_, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_, &bi);
        return cmd_;
    }
    bool submit() {
        if (vkEndCommandBuffer(cmd_) != VK_SUCCESS) return false;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd_;
        if (vkQueueSubmit(vk_.queue(), 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) return false;
        return vkQueueWaitIdle(vk_.queue()) == VK_SUCCESS;
    }

    // Ownership transfer between the external owner of a DMA-BUF and this
    // queue, with a layout change.
    void barrier(VkCommandBuffer cmd, VkImage image, bool acquire, VkImageLayout from, VkImageLayout to,
                 VkAccessFlags access) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask = acquire ? 0 : access;
        b.dstAccessMask = acquire ? access : 0;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = acquire ? VK_QUEUE_FAMILY_EXTERNAL : vk_.queue_family();
        b.dstQueueFamilyIndex = acquire ? vk_.queue_family() : VK_QUEUE_FAMILY_EXTERNAL;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
    }

    // GPU copy of the whole image into the host buffer; returns its pixels.
    std::vector<uint32_t> read_image(VkImage image) {
        VkCommandBuffer cmd = begin();
        barrier(cmd, image, true, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kW, kH, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_, 1, &region);
        if (!submit()) return {};
        std::vector<uint32_t> out(size_t(kW) * kH);
        void* p = nullptr;
        if (vkMapMemory(vk_.device(), memory_, 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS) return {};
        std::memcpy(out.data(), p, out.size() * 4);
        vkUnmapMemory(vk_.device(), memory_);
        return out;
    }

    // GPU clear of the whole image, then release to the external owner.
    bool clear_image(VkImage image, const VkClearColorValue& color) {
        VkCommandBuffer cmd = begin();
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &b);
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
        barrier(cmd, image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_TRANSFER_WRITE_BIT);
        return submit();
    }

private:
    const VulkanContext& vk_;
    bool ok_ = false;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
};

std::unique_ptr<GbmBuffer> linear_argb(GbmDevice& gbm) {
    auto bo = gbm.create_buffer_with_modifiers(kW, kH, DRM_FORMAT_ARGB8888, {DRM_FORMAT_MOD_LINEAR});
    if (!bo.ok()) bo = gbm.create_buffer(kW, kH, DRM_FORMAT_ARGB8888, GBM_BO_USE_LINEAR);
    return bo.ok() ? std::move(bo.value()) : nullptr;
}

void test_import(VulkanContext& vk, GbmDevice& gbm, Gpu& gpu) {
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!vk.is_import_supported(VK_FORMAT_B8G8R8A8_UNORM, DRM_FORMAT_MOD_LINEAR, usage)) {
        bstest::skip_check("import", "this Vulkan driver cannot import linear B8G8R8A8 DMA-BUFs");
        return;
    }
    auto bo = linear_argb(gbm);
    REQUIRE(bo != nullptr);
    {
        auto map = bo->map(0, 0, kW, kH, GBM_BO_TRANSFER_WRITE);
        REQUIRE(map && map->valid());
        for (uint32_t y = 0; y < kH; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(map->data()) + size_t(y) * map->stride());
            for (uint32_t x = 0; x < kW; ++x) row[x] = pattern(x, y);
        }
    }
    auto attrs = bo->export_dmabuf();
    REQUIRE_OK(attrs);
    if (attrs.value().modifier == DRM_FORMAT_MOD_INVALID) attrs.value().modifier = DRM_FORMAT_MOD_LINEAR;

    auto img = vk.import_dmabuf(attrs.value(), usage);
    REQUIRE_OK(img);
    CHECK(img.value()->handle() != VK_NULL_HANDLE);
    CHECK_EQ(img.value()->width(), kW);
    CHECK_EQ(img.value()->height(), kH);
    CHECK_EQ(img.value()->format(), VK_FORMAT_B8G8R8A8_UNORM);
    CHECK_EQ(img.value()->modifier(), uint64_t(DRM_FORMAT_MOD_LINEAR));
    CHECK(img.value()->memory_count() >= 1);

    auto layouts = vk.query_subresource_layouts(img.value()->handle(), 1);
    REQUIRE(layouts.size() == 1);
    CHECK_EQ(layouts[0].rowPitch, VkDeviceSize(attrs.value().planes[0].stride));
    CHECK_EQ(layouts[0].offset, VkDeviceSize(attrs.value().planes[0].offset));

    // ARGB8888 and B8G8R8A8_UNORM are the same bytes, so the GPU's copy must
    // reproduce the CPU's words exactly.
    auto pixels = gpu.read_image(img.value()->handle());
    REQUIRE(pixels.size() == size_t(kW) * kH);
    int bad = 0;
    for (uint32_t y = 0; y < kH; ++y)
        for (uint32_t x = 0; x < kW; ++x) bad += pixels[size_t(y) * kW + x] != pattern(x, y);
    CHECK_EQ(bad, 0);

    // A description Vulkan cannot take is refused with a reason.
    DmaBufAttributes no_mod = attrs.value().dup();
    no_mod.modifier = DRM_FORMAT_MOD_INVALID;
    CHECK(!vk.import_dmabuf(no_mod).ok());
}

void test_export(VulkanContext& vk, GbmDevice* gbm, Gpu& gpu) {
    std::vector<uint64_t> mods;
    for (const auto& m : vk.query_format_modifiers(VK_FORMAT_B8G8R8A8_UNORM)) {
        // Exported to GBM below: keep single-plane layouts only.
        if (m.plane_count == 1) mods.push_back(m.modifier);
    }
    std::printf("single-plane modifiers for B8G8R8A8_UNORM: %zu\n", mods.size());
    REQUIRE(!mods.empty());

    const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto img = vk.create_exportable_image(kW, kH, VK_FORMAT_B8G8R8A8_UNORM, mods, usage);
    REQUIRE_OK(img);
    std::printf("exportable image modifier: %s\n", drm_modifier_to_string(img.value()->modifier()).c_str());
    VkClearColorValue color{};
    color.float32[0] = 1.0f;  // R
    color.float32[1] = 0.0f;
    color.float32[2] = 1.0f;  // B
    color.float32[3] = 1.0f;
    REQUIRE(gpu.clear_image(img.value()->handle(), color));

    auto attrs = vk.export_dmabuf(*img.value());
    REQUIRE_OK(attrs);
    CHECK(attrs.value().is_valid());
    CHECK_EQ(attrs.value().width, kW);
    CHECK_EQ(attrs.value().height, kH);
    CHECK_EQ(attrs.value().drm_format, uint32_t(DRM_FORMAT_ARGB8888));
    CHECK_EQ(attrs.value().modifier, img.value()->modifier());

    // Whatever the tiling, importing the exported DMA-BUF as a second image
    // and copying it out must give the colour the first image was cleared to.
    {
        auto again = vk.import_dmabuf(attrs.value(), VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        REQUIRE_OK(again);
        CHECK_EQ(again.value()->modifier(), attrs.value().modifier);
        auto pixels = gpu.read_image(again.value()->handle());
        REQUIRE(pixels.size() == size_t(kW) * kH);
        int bad = 0;
        for (uint32_t p : pixels) bad += p != 0xffff00ffu;  // A=1 R=1 G=0 B=1
        CHECK_EQ(bad, 0);
    }

    if (!gbm) {
        bstest::skip_check("export into GBM", "no GBM device on this machine");
        return;
    }
    auto bo = gbm->import_buffer(attrs.value());
    REQUIRE_OK(bo);
    CHECK_EQ(bo.value()->width(), kW);
    if (attrs.value().modifier != DRM_FORMAT_MOD_LINEAR) {
        bstest::skip_check("exported pixels on the CPU",
                           "the driver chose a tiled modifier (checked above through a Vulkan re-import)");
        return;
    }
    auto map = bo.value()->map(0, 0, kW, kH, GBM_BO_TRANSFER_READ);
    REQUIRE(map && map->valid());
    int bad = 0;
    for (uint32_t y = 0; y < kH; ++y) {
        auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(map->data()) +
                                                      size_t(y) * map->stride());
        for (uint32_t x = 0; x < kW; ++x) bad += row[x] != 0xffff00ffu;  // A=1 R=1 G=0 B=1
    }
    CHECK_EQ(bad, 0);
}

}  // namespace

int main() {
    auto vk_res = VulkanContext::create_headless();
    if (!vk_res.ok()) {
        bstest::skip("test_vulkan", "no Vulkan device with DMA-BUF import: " + std::string(vk_res.error_message()));
    }
    auto& vk = *vk_res.value();
    CHECK(vk.device() != VK_NULL_HANDLE);
    Gpu gpu(vk);
    if (!gpu.ok()) {
        bstest::fail(__FILE__, __LINE__, "could not set up a command buffer and host-visible buffer");
        return bstest::finish("test_vulkan");
    }

    std::unique_ptr<GbmDevice> gbm;
    const std::string node = bstest::drm_node();
    if (!node.empty()) {
        auto g = GbmDevice::open(node);
        if (g.ok()) gbm = std::move(g.value());
    }
    if (gbm) {
        test_import(vk, *gbm, gpu);
    } else {
        bstest::skip_check("import", "no GBM device to allocate a DMA-BUF from");
    }
    test_export(vk, gbm.get(), gpu);
    return bstest::finish("test_vulkan");
}
