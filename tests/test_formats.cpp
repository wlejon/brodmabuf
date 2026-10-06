// The DRM format model, on every platform: FourCC codes and names, per-format
// properties (alpha, bytes per pixel, planes, chroma subsampling), modifiers,
// and on Linux the VkFormat mapping, checked against the byte orders the DRM
// and Vulkan specifications define.
#include "check.h"
#include "brodmabuf/formats.h"

#include <string>

using namespace brodmabuf;

namespace {

void test_fourcc() {
    CHECK_EQ(make_fourcc('X', 'R', '2', '4'), uint32_t(DRM_FORMAT_XRGB8888));
    CHECK_EQ(make_fourcc('A', 'R', '2', '4'), uint32_t(DRM_FORMAT_ARGB8888));
    // 'XR24' little-endian: the first character is the low byte.
    CHECK_EQ(uint32_t(DRM_FORMAT_XRGB8888), 0x34325258u);

    CHECK_EQ(drm_format_to_string(DRM_FORMAT_XRGB8888), std::string("DRM_FORMAT_XRGB8888"));
    CHECK_EQ(drm_format_to_string(DRM_FORMAT_NV12), std::string("DRM_FORMAT_NV12"));
    CHECK_EQ(drm_format_to_string(make_fourcc('A', 'B', 'C', 'D')), std::string("ABCD"));
    CHECK_EQ(drm_format_to_string(0x01020304u), std::string("0x01020304"));

    CHECK_EQ(drm_format_from_string("DRM_FORMAT_XRGB8888"), uint32_t(DRM_FORMAT_XRGB8888));
    CHECK_EQ(drm_format_from_string("XRGB8888"), uint32_t(DRM_FORMAT_XRGB8888));
    CHECK_EQ(drm_format_from_string("XR24"), uint32_t(DRM_FORMAT_XRGB8888));
    CHECK_EQ(drm_format_from_string("YU12"), uint32_t(DRM_FORMAT_YUV420));
    CHECK_EQ(drm_format_from_string("0x34325258"), uint32_t(DRM_FORMAT_XRGB8888));
    CHECK_EQ(drm_format_from_string("INVALID"), uint32_t(DRM_FORMAT_INVALID));
    CHECK_EQ(drm_format_from_string("0xZZZ"), uint32_t(DRM_FORMAT_INVALID));
    // Any other four characters are taken as a FourCC.
    CHECK_EQ(drm_format_from_string("AB30"), uint32_t(DRM_FORMAT_ABGR2101010));
    CHECK_EQ(drm_format_from_string("nonsense"), uint32_t(DRM_FORMAT_INVALID));

    // Every named format survives a round trip through its name.
    const uint32_t named[] = {
        DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888, DRM_FORMAT_XBGR8888, DRM_FORMAT_ABGR8888,
        DRM_FORMAT_RGBX8888, DRM_FORMAT_RGBA8888, DRM_FORMAT_BGRX8888, DRM_FORMAT_BGRA8888,
        DRM_FORMAT_RGB565, DRM_FORMAT_BGR565, DRM_FORMAT_ARGB2101010, DRM_FORMAT_XRGB2101010,
        DRM_FORMAT_ABGR2101010, DRM_FORMAT_XBGR2101010, DRM_FORMAT_ABGR16161616F,
        DRM_FORMAT_XBGR16161616F, DRM_FORMAT_ABGR16161616, DRM_FORMAT_XBGR16161616,
        DRM_FORMAT_R8, DRM_FORMAT_GR88, DRM_FORMAT_NV12, DRM_FORMAT_NV21,
        DRM_FORMAT_YUV420, DRM_FORMAT_YVU420, DRM_FORMAT_P010};
    for (uint32_t f : named) {
        CHECK_EQ(drm_format_from_string(drm_format_to_string(f)), f);
    }
}

void test_format_properties() {
    CHECK(!drm_format_has_alpha(DRM_FORMAT_XRGB8888));
    CHECK(!drm_format_has_alpha(DRM_FORMAT_RGB565));
    CHECK(!drm_format_has_alpha(DRM_FORMAT_NV12));
    CHECK(drm_format_has_alpha(DRM_FORMAT_ARGB8888));
    CHECK(drm_format_has_alpha(DRM_FORMAT_RGBA8888));
    CHECK(drm_format_has_alpha(DRM_FORMAT_ABGR16161616F));

    CHECK_EQ(drm_format_bytes_per_pixel(DRM_FORMAT_ARGB8888), 4u);
    CHECK_EQ(drm_format_bytes_per_pixel(DRM_FORMAT_RGBA1010102), 4u);
    CHECK_EQ(drm_format_bytes_per_pixel(DRM_FORMAT_RGB565), 2u);
    CHECK_EQ(drm_format_bytes_per_pixel(DRM_FORMAT_GR88), 2u);
    CHECK_EQ(drm_format_bytes_per_pixel(DRM_FORMAT_R8), 1u);
    CHECK_EQ(drm_format_bytes_per_pixel(DRM_FORMAT_ABGR16161616F), 8u);
    CHECK_EQ(drm_format_bytes_per_pixel(DRM_FORMAT_NV12), 0u);

    CHECK_EQ(drm_format_plane_count(DRM_FORMAT_ARGB8888), 1u);
    CHECK_EQ(drm_format_plane_count(DRM_FORMAT_NV12), 2u);
    CHECK_EQ(drm_format_plane_count(DRM_FORMAT_P016), 2u);
    CHECK_EQ(drm_format_plane_count(DRM_FORMAT_YUV420), 3u);
    CHECK_EQ(drm_format_plane_count(DRM_FORMAT_YUV410), 3u);
    CHECK_EQ(drm_format_plane_count(DRM_FORMAT_INVALID), 0u);

    struct Sub {
        uint32_t format;
        size_t plane;
        uint32_t h, v;
    };
    const Sub cases[] = {
        {DRM_FORMAT_ARGB8888, 0, 1, 1}, {DRM_FORMAT_NV12, 0, 1, 1},   {DRM_FORMAT_NV12, 1, 2, 2},
        {DRM_FORMAT_NV16, 1, 2, 1},     {DRM_FORMAT_NV24, 1, 1, 1},   {DRM_FORMAT_YUV420, 2, 2, 2},
        {DRM_FORMAT_YUV422, 1, 2, 1},   {DRM_FORMAT_YUV444, 2, 1, 1}, {DRM_FORMAT_YUV410, 1, 4, 4},
        {DRM_FORMAT_YVU411, 2, 4, 1},   {DRM_FORMAT_P010, 1, 2, 2},   {DRM_FORMAT_P012, 1, 2, 2},
        {DRM_FORMAT_P016, 1, 2, 2},
    };
    for (const auto& c : cases) {
        uint32_t hs = 0, vs = 0;
        CHECK(drm_format_plane_subsampling(c.format, c.plane, &hs, &vs));
        CHECK_EQ(hs, c.h);
        CHECK_EQ(vs, c.v);
    }
    uint32_t hs = 0, vs = 0;
    CHECK(!drm_format_plane_subsampling(DRM_FORMAT_NV12, 2, &hs, &vs));
    CHECK(!drm_format_plane_subsampling(DRM_FORMAT_ARGB8888, 1, &hs, &vs));
    CHECK(!drm_format_plane_subsampling(DRM_FORMAT_NV12, 0, nullptr, &vs));
}

void test_modifiers() {
    CHECK_EQ(drm_modifier_vendor(DRM_FORMAT_MOD_LINEAR), uint8_t(DRM_FORMAT_MOD_VENDOR_NONE));
    CHECK_EQ(drm_modifier_vendor_name(DRM_FORMAT_MOD_LINEAR), std::string("Linear"));
    CHECK_EQ(drm_modifier_vendor_name(DRM_FORMAT_MOD_INVALID), std::string("Invalid"));
    CHECK_EQ(drm_modifier_to_string(DRM_FORMAT_MOD_LINEAR), std::string("DRM_FORMAT_MOD_LINEAR"));
    CHECK_EQ(drm_modifier_to_string(DRM_FORMAT_MOD_INVALID), std::string("DRM_FORMAT_MOD_INVALID"));
    CHECK_EQ(drm_modifier_from_string("LINEAR"), uint64_t(DRM_FORMAT_MOD_LINEAR));
    CHECK_EQ(drm_modifier_from_string("DRM_FORMAT_MOD_INVALID"), uint64_t(DRM_FORMAT_MOD_INVALID));
    CHECK_EQ(drm_modifier_from_string("garbage"), uint64_t(DRM_FORMAT_MOD_INVALID));

    const uint64_t intel_x = I915_FORMAT_MOD_X_TILED;
    CHECK_EQ(intel_x, 0x0100000000000001ull);
    CHECK_EQ(drm_modifier_vendor(intel_x), uint8_t(DRM_FORMAT_MOD_VENDOR_INTEL));
    CHECK_EQ(drm_modifier_vendor_name(intel_x), std::string("Intel"));
    CHECK_EQ(drm_modifier_to_string(intel_x), std::string("I915_FORMAT_MOD_X_TILED"));
    CHECK_EQ(drm_modifier_from_string("I915_FORMAT_MOD_X_TILED"), intel_x);
    CHECK_EQ(drm_modifier_from_string("I915_FORMAT_MOD_4_TILED"), uint64_t(I915_FORMAT_MOD_4_TILED));

    // An AMD modifier has no name: it prints as vendor plus hex and parses back.
    const uint64_t amd = (uint64_t(DRM_FORMAT_MOD_VENDOR_AMD) << 56) | 0x1234;
    CHECK_EQ(drm_modifier_vendor_name(amd), std::string("AMD"));
    CHECK_EQ(drm_modifier_to_string(amd), std::string("AMD:0x0200000000001234"));
    CHECK_EQ(drm_modifier_from_string(drm_modifier_to_string(amd)), amd);
    CHECK_EQ(drm_modifier_vendor_name(uint64_t(DRM_FORMAT_MOD_VENDOR_APPLE) << 56 | 1), std::string("Apple"));
}

#if defined(__linux__)
void test_vulkan_mapping() {
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_ARGB8888), VK_FORMAT_B8G8R8A8_UNORM);
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_XRGB8888), VK_FORMAT_B8G8R8A8_UNORM);
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_ABGR8888), VK_FORMAT_R8G8B8A8_UNORM);
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_RGB565), VK_FORMAT_R5G6B5_UNORM_PACK16);
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_ARGB2101010), VK_FORMAT_A2R10G10B10_UNORM_PACK32);
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_GR88), VK_FORMAT_R8G8_UNORM);
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_NV12), VK_FORMAT_G8_B8R8_2PLANE_420_UNORM);
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_YUV420), VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM);
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_ABGR16161616F), VK_FORMAT_R16G16B16A16_SFLOAT);
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_P010), VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16);
    // RGBA8888 is bytes A,B,G,R in memory; no core VkFormat has that order.
    CHECK_EQ(drm_format_to_vk_format(DRM_FORMAT_RGBA8888), VK_FORMAT_UNDEFINED);

    CHECK_EQ(vk_format_to_drm_format(VK_FORMAT_B8G8R8A8_UNORM), uint32_t(DRM_FORMAT_ARGB8888));
    CHECK_EQ(vk_format_to_drm_format(VK_FORMAT_R8G8B8A8_UNORM), uint32_t(DRM_FORMAT_ABGR8888));
    // A8B8G8R8_PACK32 is a native word with R in the low byte: bytes R,G,B,A.
    CHECK_EQ(vk_format_to_drm_format(VK_FORMAT_A8B8G8R8_UNORM_PACK32), uint32_t(DRM_FORMAT_ABGR8888));
    CHECK_EQ(vk_format_to_drm_format(VK_FORMAT_R5G6B5_UNORM_PACK16), uint32_t(DRM_FORMAT_RGB565));
    CHECK_EQ(vk_format_to_drm_format(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM), uint32_t(DRM_FORMAT_NV12));
    CHECK_EQ(vk_format_to_drm_format(VK_FORMAT_D32_SFLOAT), uint32_t(DRM_FORMAT_INVALID));

    CHECK(is_drm_format_vulkan_compatible(DRM_FORMAT_ARGB8888));
    CHECK(is_drm_format_vulkan_compatible(DRM_FORMAT_NV12));
    CHECK(!is_drm_format_vulkan_compatible(DRM_FORMAT_INVALID));
}
#endif

}  // namespace

int main() {
    test_fourcc();
    test_format_properties();
    test_modifiers();
#if defined(__linux__)
    test_vulkan_mapping();
#endif
    return bstest::finish("test_formats");
}
