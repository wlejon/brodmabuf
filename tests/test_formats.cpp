#include "brodmabuf/formats.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace brodmabuf;

void test_fourcc() {
    std::cout << "[test_formats] Running test_fourcc..." << std::endl;

    constexpr uint32_t xr24 = make_fourcc('X', 'R', '2', '4');
    assert(xr24 == DRM_FORMAT_XRGB8888);

    constexpr uint32_t ar24 = make_fourcc('A', 'R', '2', '4');
    assert(ar24 == DRM_FORMAT_ARGB8888);

    assert(drm_format_to_string(DRM_FORMAT_XRGB8888) == "DRM_FORMAT_XRGB8888");
    assert(drm_format_to_string(DRM_FORMAT_ARGB8888) == "DRM_FORMAT_ARGB8888");
    assert(drm_format_to_string(DRM_FORMAT_NV12) == "DRM_FORMAT_NV12");

    assert(drm_format_from_string("DRM_FORMAT_XRGB8888") == DRM_FORMAT_XRGB8888);
    assert(drm_format_from_string("XRGB8888") == DRM_FORMAT_XRGB8888);
    assert(drm_format_from_string("XR24") == DRM_FORMAT_XRGB8888);
    assert(drm_format_from_string("NV12") == DRM_FORMAT_NV12);
    assert(drm_format_from_string("INVALID") == DRM_FORMAT_INVALID);

    std::cout << "[test_formats] test_fourcc passed!" << std::endl;
}

void test_format_properties() {
    std::cout << "[test_formats] Running test_format_properties..." << std::endl;

    assert(!drm_format_has_alpha(DRM_FORMAT_XRGB8888));
    assert(!drm_format_has_alpha(DRM_FORMAT_XBGR8888));
    assert(!drm_format_has_alpha(DRM_FORMAT_RGB565));
    assert(drm_format_has_alpha(DRM_FORMAT_ARGB8888));
    assert(drm_format_has_alpha(DRM_FORMAT_ABGR8888));
    assert(drm_format_has_alpha(DRM_FORMAT_RGBA8888));

    assert(drm_format_bytes_per_pixel(DRM_FORMAT_ARGB8888) == 4);
    assert(drm_format_bytes_per_pixel(DRM_FORMAT_XRGB8888) == 4);
    assert(drm_format_bytes_per_pixel(DRM_FORMAT_RGB565) == 2);
    assert(drm_format_bytes_per_pixel(DRM_FORMAT_R8) == 1);
    assert(drm_format_bytes_per_pixel(DRM_FORMAT_ABGR16161616F) == 8);
    assert(drm_format_bytes_per_pixel(DRM_FORMAT_NV12) == 0); // Multi-planar

    assert(drm_format_plane_count(DRM_FORMAT_ARGB8888) == 1);
    assert(drm_format_plane_count(DRM_FORMAT_RGB565) == 1);
    assert(drm_format_plane_count(DRM_FORMAT_NV12) == 2);
    assert(drm_format_plane_count(DRM_FORMAT_YUV420) == 3);

    uint32_t hs = 0, vs = 0;
    assert(drm_format_plane_subsampling(DRM_FORMAT_ARGB8888, 0, &hs, &vs));
    assert(hs == 1 && vs == 1);

    assert(drm_format_plane_subsampling(DRM_FORMAT_NV12, 0, &hs, &vs));
    assert(hs == 1 && vs == 1);

    assert(drm_format_plane_subsampling(DRM_FORMAT_NV12, 1, &hs, &vs));
    assert(hs == 2 && vs == 2);

    assert(!drm_format_plane_subsampling(DRM_FORMAT_NV12, 2, &hs, &vs)); // Out of range

    std::cout << "[test_formats] test_format_properties passed!" << std::endl;
}

void test_modifiers() {
    std::cout << "[test_formats] Running test_modifiers..." << std::endl;

    assert(drm_modifier_vendor(DRM_FORMAT_MOD_LINEAR) == DRM_FORMAT_MOD_VENDOR_NONE);
    assert(drm_modifier_vendor_name(DRM_FORMAT_MOD_LINEAR) == "Linear");
    assert(drm_modifier_vendor_name(DRM_FORMAT_MOD_INVALID) == "Invalid");

    assert(drm_modifier_to_string(DRM_FORMAT_MOD_LINEAR) == "DRM_FORMAT_MOD_LINEAR");
    assert(drm_modifier_to_string(DRM_FORMAT_MOD_INVALID) == "DRM_FORMAT_MOD_INVALID");

    assert(drm_modifier_from_string("DRM_FORMAT_MOD_LINEAR") == DRM_FORMAT_MOD_LINEAR);
    assert(drm_modifier_from_string("LINEAR") == DRM_FORMAT_MOD_LINEAR);
    assert(drm_modifier_from_string("DRM_FORMAT_MOD_INVALID") == DRM_FORMAT_MOD_INVALID);
    assert(drm_modifier_from_string("INVALID") == DRM_FORMAT_MOD_INVALID);

    // Intel modifier
    uint64_t intel_x = I915_FORMAT_MOD_X_TILED;
    assert(drm_modifier_vendor(intel_x) == DRM_FORMAT_MOD_VENDOR_INTEL);
    assert(drm_modifier_vendor_name(intel_x) == "Intel");
    assert(drm_modifier_to_string(intel_x) == "I915_FORMAT_MOD_X_TILED");
    assert(drm_modifier_from_string("I915_FORMAT_MOD_X_TILED") == intel_x);

    std::cout << "[test_formats] test_modifiers passed!" << std::endl;
}

void test_vulkan_mapping() {
    std::cout << "[test_formats] Running test_vulkan_mapping..." << std::endl;

    assert(drm_format_to_vk_format(DRM_FORMAT_ARGB8888) == VK_FORMAT_B8G8R8A8_UNORM);
    assert(drm_format_to_vk_format(DRM_FORMAT_XRGB8888) == VK_FORMAT_B8G8R8A8_UNORM);
    assert(drm_format_to_vk_format(DRM_FORMAT_ABGR8888) == VK_FORMAT_R8G8B8A8_UNORM);
    assert(drm_format_to_vk_format(DRM_FORMAT_RGB565) == VK_FORMAT_R5G6B5_UNORM_PACK16);
    assert(drm_format_to_vk_format(DRM_FORMAT_NV12) == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);
    assert(drm_format_to_vk_format(DRM_FORMAT_ABGR16161616F) == VK_FORMAT_R16G16B16A16_SFLOAT);
    assert(drm_format_to_vk_format(DRM_FORMAT_P010) == VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16);

    assert(vk_format_to_drm_format(VK_FORMAT_B8G8R8A8_UNORM) == DRM_FORMAT_ARGB8888);
    assert(vk_format_to_drm_format(VK_FORMAT_R8G8B8A8_UNORM) == DRM_FORMAT_ABGR8888);
    assert(vk_format_to_drm_format(VK_FORMAT_R5G6B5_UNORM_PACK16) == DRM_FORMAT_RGB565);
    assert(vk_format_to_drm_format(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM) == DRM_FORMAT_NV12);
    assert(vk_format_to_drm_format(VK_FORMAT_R16G16B16A16_SFLOAT) == DRM_FORMAT_ABGR16161616F);

    assert(is_drm_format_vulkan_compatible(DRM_FORMAT_ARGB8888));
    assert(is_drm_format_vulkan_compatible(DRM_FORMAT_NV12));
    assert(!is_drm_format_vulkan_compatible(DRM_FORMAT_INVALID));

    std::cout << "[test_formats] test_vulkan_mapping passed!" << std::endl;
}

int main() {
    test_fourcc();
    test_format_properties();
    test_modifiers();
    test_vulkan_mapping();
    std::cout << "[test_formats] All tests passed!" << std::endl;
    return 0;
}
