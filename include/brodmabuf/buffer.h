#pragma once

#include "brodmabuf/formats.h"
#include "brodmabuf/types.h"

#include <cstdint>
#include <vector>

namespace brodmabuf {

/// Description and file descriptor of an individual buffer plane.
struct DmaBufPlane {
    UniqueFd fd;
    uint32_t stride = 0;  // Row pitch in bytes
    uint32_t offset = 0;  // Byte offset from start of plane buffer
    uint64_t size = 0;    // Total size of underlying plane buffer (or 0 if unknown)

    DmaBufPlane() noexcept = default;
    DmaBufPlane(UniqueFd plane_fd, uint32_t plane_stride, uint32_t plane_offset, uint64_t plane_size = 0) noexcept
        : fd(std::move(plane_fd)), stride(plane_stride), offset(plane_offset), size(plane_size) {}

    DmaBufPlane(const DmaBufPlane&) = delete;
    DmaBufPlane& operator=(const DmaBufPlane&) = delete;

    DmaBufPlane(DmaBufPlane&&) noexcept = default;
    DmaBufPlane& operator=(DmaBufPlane&&) noexcept = default;

    /// Duplicates the plane file descriptor, producing an independent DmaBufPlane.
    [[nodiscard]] DmaBufPlane dup() const;
};

/// Full description of a DMA-BUF image, including dimensions, DRM format,
/// DRM format modifier, and plane descriptions.
struct DmaBufAttributes {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t drm_format = DRM_FORMAT_INVALID;
    uint64_t modifier = DRM_FORMAT_MOD_INVALID;
    std::vector<DmaBufPlane> planes;

    DmaBufAttributes() noexcept = default;
    DmaBufAttributes(const DmaBufAttributes&) = delete;
    DmaBufAttributes& operator=(const DmaBufAttributes&) = delete;
    DmaBufAttributes(DmaBufAttributes&&) noexcept = default;
    DmaBufAttributes& operator=(DmaBufAttributes&&) noexcept = default;

    [[nodiscard]] size_t plane_count() const noexcept { return planes.size(); }

    /// Checks if dimensions, format, plane count, and file descriptors are consistent and valid.
    [[nodiscard]] bool is_valid() const noexcept;

    /// Checks if planes reside in separate memory objects (different underlying files).
    /// If plane_count <= 1 or fstat fails, returns false.
    [[nodiscard]] bool is_disjoint() const noexcept;

    /// Duplicates all plane file descriptors, producing an independent copy of attributes.
    [[nodiscard]] DmaBufAttributes dup() const;

    /// Returns the sum of plane sizes if known, or calculates estimate based on strides and heights.
    [[nodiscard]] uint64_t total_size() const noexcept;
};

/// Minimum row stride for a given format, width, and plane index.
uint32_t calculate_min_stride(uint32_t drm_format, uint32_t width, size_t plane) noexcept;

/// Minimum plane buffer size in bytes for a given format, width, height, plane index, and stride.
uint64_t calculate_min_plane_size(uint32_t drm_format, uint32_t width, uint32_t height,
                                 size_t plane, uint32_t stride) noexcept;

}  // namespace brodmabuf
