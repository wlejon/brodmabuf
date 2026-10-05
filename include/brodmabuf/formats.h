#pragma once

#include <drm_fourcc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brodmabuf {

/// Construct a 32-bit DRM FourCC code from 4 characters.
constexpr uint32_t make_fourcc(char a, char b, char c, char d) noexcept {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

/// Convert a DRM format FourCC code to human-readable string (e.g. "XR24" or format name "DRM_FORMAT_XRGB8888").
std::string drm_format_to_string(uint32_t drm_fourcc);

/// Convert a string (e.g. "XRGB8888", "XR24", or "DRM_FORMAT_XRGB8888") to DRM FourCC code.
/// Returns DRM_FORMAT_INVALID if unrecognized.
uint32_t drm_format_from_string(std::string_view name);

/// Whether the format possesses an alpha channel that is used (not ignored/padded X).
bool drm_format_has_alpha(uint32_t drm_fourcc) noexcept;

/// Bytes per pixel for single-plane packed pixel formats. Returns 0 for multi-planar or complex formats.
uint32_t drm_format_bytes_per_pixel(uint32_t drm_fourcc) noexcept;

/// Number of planes required for the specified DRM format.
uint32_t drm_format_plane_count(uint32_t drm_fourcc) noexcept;

/// Horizontal and vertical subsampling factors for a specific plane (0-indexed).
/// Plane 0 always has (1, 1). For chroma planes in NV12/YUV420, factors are typically (2, 2).
/// Returns false if format is invalid or plane index is out of bounds.
bool drm_format_plane_subsampling(uint32_t drm_fourcc, size_t plane,
                                  uint32_t* h_subsample, uint32_t* v_subsample) noexcept;

/// Extract vendor code from a 64-bit DRM modifier (bits 56-63).
constexpr uint8_t drm_modifier_vendor(uint64_t modifier) noexcept {
    return static_cast<uint8_t>(modifier >> 56);
}

/// Returns vendor name for a given DRM modifier (e.g. "Intel", "AMD", "NVIDIA", "ARM", "Linear").
std::string drm_modifier_vendor_name(uint64_t modifier);

/// Convert modifier value to descriptive string (e.g. "DRM_FORMAT_MOD_LINEAR", "I915_X_TILED", hex fallback).
std::string drm_modifier_to_string(uint64_t modifier);

/// Convert string description or hex string to 64-bit modifier.
/// Returns DRM_FORMAT_MOD_INVALID on parse error.
uint64_t drm_modifier_from_string(std::string_view name);

/// Convert a DRM format FourCC code to corresponding VkFormat.
/// Returns VK_FORMAT_UNDEFINED if there is no direct equivalent.
VkFormat drm_format_to_vk_format(uint32_t drm_fourcc) noexcept;

/// Convert a VkFormat to corresponding DRM format FourCC code.
/// Returns DRM_FORMAT_INVALID if there is no direct equivalent.
uint32_t vk_format_to_drm_format(VkFormat vk_format) noexcept;

/// Check if a DRM FourCC format can be mapped to Vulkan.
bool is_drm_format_vulkan_compatible(uint32_t drm_fourcc) noexcept;

}  // namespace brodmabuf
