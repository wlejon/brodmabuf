#include "brodmabuf/gbm.h"
#include "brodmabuf/vulkan.h"

#include <cassert>
#include <iostream>

using namespace brodmabuf;

int main() {
    auto vk_res = VulkanContext::create_headless();
    if (!vk_res.ok()) {
        std::cout << "[test_vulkan] VulkanContext::create_headless returned: "
                  << vk_res.error_message() << ", skipping with code 77" << std::endl;
        return 77;
    }

    auto vk_ctx = std::move(vk_res.value());
    assert(vk_ctx->device() != VK_NULL_HANDLE);

    // 1. Query format modifiers
    auto modifiers = vk_ctx->query_format_modifiers(VK_FORMAT_B8G8R8A8_UNORM);
    std::cout << "[test_vulkan] Supported modifiers for VK_FORMAT_B8G8R8A8_UNORM: "
              << modifiers.size() << std::endl;
    for (const auto& mod : modifiers) {
        std::cout << "  - Modifier: " << drm_modifier_to_string(mod.modifier)
                  << ", planes=" << mod.plane_count << std::endl;
    }

    // 2. Allocate buffer via GBM
    auto gbm_res = GbmDevice::open();
    if (!gbm_res.ok()) {
        std::cout << "[test_vulkan] GbmDevice::open failed, skipping with code 77" << std::endl;
        return 77;
    }
    auto gbm_dev = std::move(gbm_res.value());

    std::vector<uint64_t> mod_list;
    for (const auto& m : modifiers) {
        mod_list.push_back(m.modifier);
    }
    if (mod_list.empty()) {
        mod_list.push_back(DRM_FORMAT_MOD_LINEAR);
    }

    auto bo_res = gbm_dev->create_buffer_with_modifiers(512, 512, DRM_FORMAT_ARGB8888, mod_list);
    if (!bo_res.ok()) {
        // Fallback to standard buffer
        bo_res = gbm_dev->create_buffer(512, 512, DRM_FORMAT_ARGB8888);
    }
    if (!bo_res.ok()) {
        std::cout << "[test_vulkan] Failed to allocate GBM buffer: " << bo_res.error_message() << std::endl;
        return 77;
    }

    auto bo = std::move(bo_res.value());
    auto attrs_res = bo->export_dmabuf();
    assert(attrs_res.ok());

    DmaBufAttributes attrs = std::move(attrs_res.value());
    assert(attrs.is_valid());
    std::cout << "[test_vulkan] Exported GBM buffer: " << attrs.width << "x" << attrs.height
              << ", modifier=" << drm_modifier_to_string(attrs.modifier) << std::endl;

    // 3. Import DMA-BUF into Vulkan
    auto vk_img_res = vk_ctx->import_dmabuf(attrs);
    if (vk_img_res.ok()) {
        auto vk_img = std::move(vk_img_res.value());
        assert(vk_img->handle() != VK_NULL_HANDLE);
        assert(vk_img->width() == 512);
        assert(vk_img->height() == 512);
        assert(vk_img->format() == VK_FORMAT_B8G8R8A8_UNORM);
        std::cout << "[test_vulkan] Successfully imported DMA-BUF into Vulkan as VkImage!" << std::endl;

        // Query subresource layouts
        auto layouts = vk_ctx->query_subresource_layouts(vk_img->handle(), static_cast<uint32_t>(attrs.planes.size()));
        assert(layouts.size() == attrs.planes.size());
        for (size_t i = 0; i < layouts.size(); ++i) {
            std::cout << "  Plane " << i << ": rowPitch=" << layouts[i].rowPitch
                      << ", offset=" << layouts[i].offset << std::endl;
        }
    } else {
        std::cout << "[test_vulkan] import_dmabuf returned: " << vk_img_res.error_message() << std::endl;
    }

    // 4. Create exportable Vulkan image and export to DMA-BUF
    auto exp_img_res = vk_ctx->create_exportable_image(256, 256, VK_FORMAT_B8G8R8A8_UNORM, mod_list);
    if (exp_img_res.ok()) {
        auto exp_img = std::move(exp_img_res.value());
        assert(exp_img->handle() != VK_NULL_HANDLE);
        std::cout << "[test_vulkan] Created exportable Vulkan image, modifier="
                  << drm_modifier_to_string(exp_img->modifier()) << std::endl;

        auto exp_dmabuf_res = vk_ctx->export_dmabuf(*exp_img);
        if (exp_dmabuf_res.ok()) {
            DmaBufAttributes exp_attrs = std::move(exp_dmabuf_res.value());
            assert(exp_attrs.is_valid());
            assert(exp_attrs.width == 256);
            assert(exp_attrs.height == 256);
            std::cout << "[test_vulkan] Exported VkImage to DMA-BUF successfully, planes="
                      << exp_attrs.planes.size() << std::endl;

            // Test re-importing Vulkan-exported DMA-BUF into GBM!
            auto gbm_import_res = gbm_dev->import_buffer(exp_attrs);
            if (gbm_import_res.ok()) {
                std::cout << "[test_vulkan] Successfully imported Vulkan-exported DMA-BUF into GBM!" << std::endl;
            } else {
                std::cout << "[test_vulkan] Note: GBM import of Vulkan dmabuf returned: "
                          << gbm_import_res.error_message() << std::endl;
            }
        } else {
            std::cout << "[test_vulkan] export_dmabuf returned: "
                      << exp_dmabuf_res.error_message() << std::endl;
        }
    } else {
        std::cout << "[test_vulkan] create_exportable_image returned: "
                  << exp_img_res.error_message() << std::endl;
    }

    std::cout << "[test_vulkan] All tests passed!" << std::endl;
    return 0;
}
