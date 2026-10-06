#include "brodmabuf/formats.h"

#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

namespace brodmabuf {

std::string drm_format_to_string(uint32_t f) {
    switch (f) {
        case DRM_FORMAT_INVALID: return "DRM_FORMAT_INVALID";
        case DRM_FORMAT_XRGB8888: return "DRM_FORMAT_XRGB8888";
        case DRM_FORMAT_ARGB8888: return "DRM_FORMAT_ARGB8888";
        case DRM_FORMAT_XBGR8888: return "DRM_FORMAT_XBGR8888";
        case DRM_FORMAT_ABGR8888: return "DRM_FORMAT_ABGR8888";
        case DRM_FORMAT_RGBX8888: return "DRM_FORMAT_RGBX8888";
        case DRM_FORMAT_RGBA8888: return "DRM_FORMAT_RGBA8888";
        case DRM_FORMAT_BGRX8888: return "DRM_FORMAT_BGRX8888";
        case DRM_FORMAT_BGRA8888: return "DRM_FORMAT_BGRA8888";
        case DRM_FORMAT_RGB565: return "DRM_FORMAT_RGB565";
        case DRM_FORMAT_BGR565: return "DRM_FORMAT_BGR565";
        case DRM_FORMAT_ARGB2101010: return "DRM_FORMAT_ARGB2101010";
        case DRM_FORMAT_XRGB2101010: return "DRM_FORMAT_XRGB2101010";
        case DRM_FORMAT_ABGR2101010: return "DRM_FORMAT_ABGR2101010";
        case DRM_FORMAT_XBGR2101010: return "DRM_FORMAT_XBGR2101010";
        case DRM_FORMAT_ABGR16161616F: return "DRM_FORMAT_ABGR16161616F";
        case DRM_FORMAT_XBGR16161616F: return "DRM_FORMAT_XBGR16161616F";
        case DRM_FORMAT_ABGR16161616: return "DRM_FORMAT_ABGR16161616";
        case DRM_FORMAT_XBGR16161616: return "DRM_FORMAT_XBGR16161616";
        case DRM_FORMAT_R8: return "DRM_FORMAT_R8";
        case DRM_FORMAT_GR88: return "DRM_FORMAT_GR88";
        case DRM_FORMAT_NV12: return "DRM_FORMAT_NV12";
        case DRM_FORMAT_NV21: return "DRM_FORMAT_NV21";
        case DRM_FORMAT_YUV420: return "DRM_FORMAT_YUV420";
        case DRM_FORMAT_YVU420: return "DRM_FORMAT_YVU420";
        case DRM_FORMAT_P010: return "DRM_FORMAT_P010";
        default: {
            char buf[5] = {
                static_cast<char>(f & 0xff),
                static_cast<char>((f >> 8) & 0xff),
                static_cast<char>((f >> 16) & 0xff),
                static_cast<char>((f >> 24) & 0xff),
                '\0'
            };
            for (int i = 0; i < 4; ++i) {
                if (!std::isprint(static_cast<unsigned char>(buf[i]))) {
                    std::ostringstream ss;
                    ss << "0x" << std::hex << std::setfill('0') << std::setw(8) << f;
                    return ss.str();
                }
            }
            return std::string(buf);
        }
    }
}

uint32_t drm_format_from_string(std::string_view name) {
    if (name.empty() || name == "INVALID" || name == "DRM_FORMAT_INVALID") return DRM_FORMAT_INVALID;
    if (name == "XRGB8888" || name == "DRM_FORMAT_XRGB8888" || name == "XR24") return DRM_FORMAT_XRGB8888;
    if (name == "ARGB8888" || name == "DRM_FORMAT_ARGB8888" || name == "AR24") return DRM_FORMAT_ARGB8888;
    if (name == "XBGR8888" || name == "DRM_FORMAT_XBGR8888" || name == "XB24") return DRM_FORMAT_XBGR8888;
    if (name == "ABGR8888" || name == "DRM_FORMAT_ABGR8888" || name == "AB24") return DRM_FORMAT_ABGR8888;
    if (name == "RGBX8888" || name == "DRM_FORMAT_RGBX8888" || name == "RX24") return DRM_FORMAT_RGBX8888;
    if (name == "RGBA8888" || name == "DRM_FORMAT_RGBA8888" || name == "RA24") return DRM_FORMAT_RGBA8888;
    if (name == "BGRX8888" || name == "DRM_FORMAT_BGRX8888" || name == "BX24") return DRM_FORMAT_BGRX8888;
    if (name == "BGRA8888" || name == "DRM_FORMAT_BGRA8888" || name == "BA24") return DRM_FORMAT_BGRA8888;
    if (name == "RGB565" || name == "DRM_FORMAT_RGB565" || name == "RG16") return DRM_FORMAT_RGB565;
    if (name == "BGR565" || name == "DRM_FORMAT_BGR565" || name == "BG16") return DRM_FORMAT_BGR565;
    if (name == "ARGB2101010" || name == "DRM_FORMAT_ARGB2101010" || name == "AR30") return DRM_FORMAT_ARGB2101010;
    if (name == "XRGB2101010" || name == "DRM_FORMAT_XRGB2101010" || name == "XR30") return DRM_FORMAT_XRGB2101010;
    if (name == "ABGR2101010" || name == "DRM_FORMAT_ABGR2101010" || name == "AB30") return DRM_FORMAT_ABGR2101010;
    if (name == "XBGR2101010" || name == "DRM_FORMAT_XBGR2101010" || name == "XB30") return DRM_FORMAT_XBGR2101010;
    if (name == "ABGR16161616F" || name == "DRM_FORMAT_ABGR16161616F" || name == "AB4H") return DRM_FORMAT_ABGR16161616F;
    if (name == "XBGR16161616F" || name == "DRM_FORMAT_XBGR16161616F" || name == "XB4H") return DRM_FORMAT_XBGR16161616F;
    if (name == "ABGR16161616" || name == "DRM_FORMAT_ABGR16161616") return DRM_FORMAT_ABGR16161616;
    if (name == "XBGR16161616" || name == "DRM_FORMAT_XBGR16161616") return DRM_FORMAT_XBGR16161616;
    if (name == "R8" || name == "DRM_FORMAT_R8") return DRM_FORMAT_R8;
    if (name == "GR88" || name == "DRM_FORMAT_GR88") return DRM_FORMAT_GR88;
    if (name == "NV12" || name == "DRM_FORMAT_NV12") return DRM_FORMAT_NV12;
    if (name == "NV21" || name == "DRM_FORMAT_NV21") return DRM_FORMAT_NV21;
    if (name == "YUV420" || name == "DRM_FORMAT_YUV420" || name == "YU12") return DRM_FORMAT_YUV420;
    if (name == "YVU420" || name == "DRM_FORMAT_YVU420" || name == "YV12") return DRM_FORMAT_YVU420;
    if (name == "P010" || name == "DRM_FORMAT_P010") return DRM_FORMAT_P010;

    // 4-character FourCC fallback
    if (name.size() == 4) {
        return make_fourcc(name[0], name[1], name[2], name[3]);
    }
    // Hex string fallback: 0x...
    if (name.size() > 2 && name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
        try {
            return static_cast<uint32_t>(std::stoul(std::string(name), nullptr, 16));
        } catch (...) {
            return DRM_FORMAT_INVALID;
        }
    }
    return DRM_FORMAT_INVALID;
}

bool drm_format_has_alpha(uint32_t f) noexcept {
    switch (f) {
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_RGBA8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_ARGB2101010:
        case DRM_FORMAT_ABGR2101010:
        case DRM_FORMAT_RGBA1010102:
        case DRM_FORMAT_BGRA1010102:
        case DRM_FORMAT_ARGB16161616F:
        case DRM_FORMAT_ABGR16161616F:
        case DRM_FORMAT_ARGB16161616:
        case DRM_FORMAT_ABGR16161616:
        case DRM_FORMAT_ARGB1555:
        case DRM_FORMAT_ABGR1555:
        case DRM_FORMAT_RGBA5551:
        case DRM_FORMAT_BGRA5551:
        case DRM_FORMAT_ARGB4444:
        case DRM_FORMAT_ABGR4444:
        case DRM_FORMAT_RGBA4444:
        case DRM_FORMAT_BGRA4444:
            return true;
        default:
            return false;
    }
}

uint32_t drm_format_bytes_per_pixel(uint32_t f) noexcept {
    switch (f) {
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_RGBA8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_BGRX8888:
        case DRM_FORMAT_ARGB2101010:
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_ABGR2101010:
        case DRM_FORMAT_XBGR2101010:
        case DRM_FORMAT_RGBX1010102:
        case DRM_FORMAT_BGRX1010102:
        case DRM_FORMAT_RGBA1010102:
        case DRM_FORMAT_BGRA1010102:
            return 4;

        case DRM_FORMAT_RGB565:
        case DRM_FORMAT_BGR565:
        case DRM_FORMAT_ARGB1555:
        case DRM_FORMAT_XRGB1555:
        case DRM_FORMAT_ABGR1555:
        case DRM_FORMAT_XBGR1555:
        case DRM_FORMAT_RGBA5551:
        case DRM_FORMAT_RGBX5551:
        case DRM_FORMAT_BGRA5551:
        case DRM_FORMAT_BGRX5551:
        case DRM_FORMAT_ARGB4444:
        case DRM_FORMAT_XRGB4444:
        case DRM_FORMAT_ABGR4444:
        case DRM_FORMAT_XBGR4444:
        case DRM_FORMAT_RGBA4444:
        case DRM_FORMAT_RGBX4444:
        case DRM_FORMAT_BGRA4444:
        case DRM_FORMAT_BGRX4444:
        case DRM_FORMAT_GR88:
        case DRM_FORMAT_RG88:
            return 2;

        case DRM_FORMAT_R8:
            return 1;

        case DRM_FORMAT_ABGR16161616F:
        case DRM_FORMAT_XBGR16161616F:
        case DRM_FORMAT_ARGB16161616F:
        case DRM_FORMAT_XRGB16161616F:
        case DRM_FORMAT_ABGR16161616:
        case DRM_FORMAT_XBGR16161616:
        case DRM_FORMAT_ARGB16161616:
        case DRM_FORMAT_XRGB16161616:
            return 8;

        default:
            return 0; // Multi-planar or variable
    }
}

uint32_t drm_format_plane_count(uint32_t f) noexcept {
    switch (f) {
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
        case DRM_FORMAT_NV16:
        case DRM_FORMAT_NV61:
        case DRM_FORMAT_NV24:
        case DRM_FORMAT_NV42:
        case DRM_FORMAT_P010:
        case DRM_FORMAT_P012:
        case DRM_FORMAT_P016:
            return 2;

        case DRM_FORMAT_YUV410:
        case DRM_FORMAT_YVU410:
        case DRM_FORMAT_YUV411:
        case DRM_FORMAT_YVU411:
        case DRM_FORMAT_YUV420:
        case DRM_FORMAT_YVU420:
        case DRM_FORMAT_YUV422:
        case DRM_FORMAT_YVU422:
        case DRM_FORMAT_YUV444:
        case DRM_FORMAT_YVU444:
            return 3;

        case DRM_FORMAT_INVALID:
            return 0;

        default:
            return 1;
    }
}

bool drm_format_plane_subsampling(uint32_t f, size_t plane,
                                  uint32_t* h_subsample, uint32_t* v_subsample) noexcept {
    if (!h_subsample || !v_subsample) return false;
    uint32_t count = drm_format_plane_count(f);
    if (plane >= count) return false;

    if (plane == 0) {
        *h_subsample = 1;
        *v_subsample = 1;
        return true;
    }

    switch (f) {
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
        case DRM_FORMAT_YUV420:
        case DRM_FORMAT_YVU420:
        case DRM_FORMAT_P010:
        case DRM_FORMAT_P012:
        case DRM_FORMAT_P016:
            *h_subsample = 2;
            *v_subsample = 2;
            return true;

        case DRM_FORMAT_YUV410:
        case DRM_FORMAT_YVU410:
            *h_subsample = 4;
            *v_subsample = 4;
            return true;

        case DRM_FORMAT_YUV411:
        case DRM_FORMAT_YVU411:
            *h_subsample = 4;
            *v_subsample = 1;
            return true;

        case DRM_FORMAT_NV16:
        case DRM_FORMAT_NV61:
        case DRM_FORMAT_YUV422:
        case DRM_FORMAT_YVU422:
            *h_subsample = 2;
            *v_subsample = 1;
            return true;

        case DRM_FORMAT_NV24:
        case DRM_FORMAT_NV42:
        case DRM_FORMAT_YUV444:
        case DRM_FORMAT_YVU444:
            *h_subsample = 1;
            *v_subsample = 1;
            return true;

        default:
            *h_subsample = 1;
            *v_subsample = 1;
            return true;
    }
}

std::string drm_modifier_vendor_name(uint64_t modifier) {
    if (modifier == DRM_FORMAT_MOD_INVALID) return "Invalid";
    if (modifier == DRM_FORMAT_MOD_LINEAR) return "Linear";

    uint8_t vendor = drm_modifier_vendor(modifier);
    switch (vendor) {
        case DRM_FORMAT_MOD_VENDOR_NONE: return "None";
        case DRM_FORMAT_MOD_VENDOR_INTEL: return "Intel";
        case DRM_FORMAT_MOD_VENDOR_AMD: return "AMD";
        case DRM_FORMAT_MOD_VENDOR_NVIDIA: return "NVIDIA";
        case DRM_FORMAT_MOD_VENDOR_SAMSUNG: return "Samsung";
        case DRM_FORMAT_MOD_VENDOR_QCOM: return "Qualcomm";
        case DRM_FORMAT_MOD_VENDOR_VIVANTE: return "Vivante";
        case DRM_FORMAT_MOD_VENDOR_BROADCOM: return "Broadcom";
        case DRM_FORMAT_MOD_VENDOR_ARM: return "ARM";
        case DRM_FORMAT_MOD_VENDOR_ALLWINNER: return "Allwinner";
        case DRM_FORMAT_MOD_VENDOR_AMLOGIC: return "Amlogic";
        case DRM_FORMAT_MOD_VENDOR_MTK: return "MediaTek";
        case DRM_FORMAT_MOD_VENDOR_APPLE: return "Apple";
        default: return "Unknown";
    }
}

std::string drm_modifier_to_string(uint64_t modifier) {
    if (modifier == DRM_FORMAT_MOD_INVALID) return "DRM_FORMAT_MOD_INVALID";
    if (modifier == DRM_FORMAT_MOD_LINEAR) return "DRM_FORMAT_MOD_LINEAR";

    switch (modifier) {
        case I915_FORMAT_MOD_X_TILED: return "I915_FORMAT_MOD_X_TILED";
        case I915_FORMAT_MOD_Y_TILED: return "I915_FORMAT_MOD_Y_TILED";
        case I915_FORMAT_MOD_Yf_TILED: return "I915_FORMAT_MOD_Yf_TILED";
        case I915_FORMAT_MOD_Y_TILED_CCS: return "I915_FORMAT_MOD_Y_TILED_CCS";
        case I915_FORMAT_MOD_4_TILED: return "I915_FORMAT_MOD_4_TILED";
        default: break;
    }

    std::ostringstream ss;
    ss << drm_modifier_vendor_name(modifier) << ":0x" << std::hex << std::setfill('0')
       << std::setw(16) << modifier;
    return ss.str();
}

uint64_t drm_modifier_from_string(std::string_view name) {
    if (name.empty() || name == "INVALID" || name == "DRM_FORMAT_MOD_INVALID") {
        return DRM_FORMAT_MOD_INVALID;
    }
    if (name == "LINEAR" || name == "DRM_FORMAT_MOD_LINEAR") {
        return DRM_FORMAT_MOD_LINEAR;
    }
    if (name == "I915_FORMAT_MOD_X_TILED") return I915_FORMAT_MOD_X_TILED;
    if (name == "I915_FORMAT_MOD_Y_TILED") return I915_FORMAT_MOD_Y_TILED;
    if (name == "I915_FORMAT_MOD_Yf_TILED") return I915_FORMAT_MOD_Yf_TILED;
    if (name == "I915_FORMAT_MOD_4_TILED") return I915_FORMAT_MOD_4_TILED;

    // Check for hex representation: 0x... or Vendor:0x...
    size_t pos = name.rfind("0x");
    if (pos == std::string_view::npos) {
        pos = name.rfind("0X");
    }
    if (pos != std::string_view::npos) {
        try {
            return std::stoull(std::string(name.substr(pos)), nullptr, 16);
        } catch (...) {
            return DRM_FORMAT_MOD_INVALID;
        }
    }

    return DRM_FORMAT_MOD_INVALID;
}

#if defined(__linux__)
// DRM codes name a little-endian packed word; Vulkan's _PACK formats name a
// native word and the others a byte order. XRGB8888 is bytes B,G,R,X (Vulkan
// B8G8R8A8), XBGR8888 bytes R,G,B,X (R8G8B8A8, and also A8B8G8R8_PACK32).
// RGBA8888 is bytes A,B,G,R, which no core VkFormat describes.
VkFormat drm_format_to_vk_format(uint32_t f) noexcept {
    switch (f) {
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_ARGB8888:
            return VK_FORMAT_B8G8R8A8_UNORM;

        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_ABGR8888:
            return VK_FORMAT_R8G8B8A8_UNORM;

        case DRM_FORMAT_RGB565:
            return VK_FORMAT_R5G6B5_UNORM_PACK16;

        case DRM_FORMAT_BGR565:
            return VK_FORMAT_B5G6R5_UNORM_PACK16;

        case DRM_FORMAT_ARGB2101010:
        case DRM_FORMAT_XRGB2101010:
            return VK_FORMAT_A2R10G10B10_UNORM_PACK32;

        case DRM_FORMAT_ABGR2101010:
        case DRM_FORMAT_XBGR2101010:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;

        case DRM_FORMAT_ABGR16161616F:
        case DRM_FORMAT_XBGR16161616F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;

        case DRM_FORMAT_ABGR16161616:
        case DRM_FORMAT_XBGR16161616:
            return VK_FORMAT_R16G16B16A16_UNORM;

        case DRM_FORMAT_R8:
            return VK_FORMAT_R8_UNORM;

        case DRM_FORMAT_GR88:
            return VK_FORMAT_R8G8_UNORM;

        case DRM_FORMAT_NV12:
            return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;

        case DRM_FORMAT_YUV420:
            return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;

        case DRM_FORMAT_P010:
            return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;

        default:
            return VK_FORMAT_UNDEFINED;
    }
}

uint32_t vk_format_to_drm_format(VkFormat vk) noexcept {
    switch (vk) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return DRM_FORMAT_ARGB8888;

        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return DRM_FORMAT_ABGR8888;

        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
            return DRM_FORMAT_ABGR8888;

        case VK_FORMAT_R5G6B5_UNORM_PACK16:
            return DRM_FORMAT_RGB565;

        case VK_FORMAT_B5G6R5_UNORM_PACK16:
            return DRM_FORMAT_BGR565;

        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            return DRM_FORMAT_ARGB2101010;

        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return DRM_FORMAT_ABGR2101010;

        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return DRM_FORMAT_ABGR16161616F;

        case VK_FORMAT_R16G16B16A16_UNORM:
            return DRM_FORMAT_ABGR16161616;

        case VK_FORMAT_R8_UNORM:
            return DRM_FORMAT_R8;

        case VK_FORMAT_R8G8_UNORM:
            return DRM_FORMAT_GR88;

        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
            return DRM_FORMAT_NV12;

        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
            return DRM_FORMAT_YUV420;

        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
            return DRM_FORMAT_P010;

        default:
            return DRM_FORMAT_INVALID;
    }
}

bool is_drm_format_vulkan_compatible(uint32_t drm_fourcc) noexcept {
    return drm_format_to_vk_format(drm_fourcc) != VK_FORMAT_UNDEFINED;
}
#endif  // __linux__

}  // namespace brodmabuf
