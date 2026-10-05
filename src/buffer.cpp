#include "brodmabuf/buffer.h"

#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>

namespace brodmabuf {

DmaBufPlane DmaBufPlane::dup() const {
    return DmaBufPlane(fd.dup(), stride, offset, size);
}

DmaBufAttributes DmaBufAttributes::dup() const {
    DmaBufAttributes copy;
    copy.width = width;
    copy.height = height;
    copy.drm_format = drm_format;
    copy.modifier = modifier;
    copy.planes.reserve(planes.size());
    for (const auto& plane : planes) {
        copy.planes.push_back(plane.dup());
    }
    return copy;
}

bool DmaBufAttributes::is_valid() const noexcept {
    if (width == 0 || height == 0) return false;
    if (drm_format == DRM_FORMAT_INVALID) return false;
    if (planes.empty() || planes.size() > kMaxPlanes) return false;

    uint32_t min_planes = drm_format_plane_count(drm_format);
    if (min_planes != 0 && planes.size() < min_planes) {
        return false;
    }
    if ((modifier == DRM_FORMAT_MOD_LINEAR || modifier == DRM_FORMAT_MOD_INVALID) && min_planes != 0) {
        if (planes.size() != min_planes) {
            return false;
        }
    }

    for (const auto& plane : planes) {
        if (!plane.fd.valid()) return false;
        if (plane.stride == 0) return false;
    }

    return true;
}

bool DmaBufAttributes::is_disjoint() const noexcept {
    if (planes.size() <= 1) return false;

    struct stat first_stat{};
    if (fstat(planes[0].fd.get(), &first_stat) != 0) {
        return false;
    }

    for (size_t i = 1; i < planes.size(); ++i) {
        struct stat st{};
        if (fstat(planes[i].fd.get(), &st) != 0) {
            return true;
        }
        if (st.st_ino != first_stat.st_ino || st.st_dev != first_stat.st_dev) {
            return true;
        }
    }

    return false;
}

uint64_t DmaBufAttributes::total_size() const noexcept {
    if (planes.empty()) return 0;

    bool disjoint = is_disjoint();
    if (disjoint) {
        uint64_t sum = 0;
        for (size_t i = 0; i < planes.size(); ++i) {
            if (planes[i].size > 0) {
                sum += planes[i].size;
            } else {
                sum += calculate_min_plane_size(drm_format, width, height, i, planes[i].stride);
            }
        }
        return sum;
    }

    // Single buffer shared by all planes
    uint64_t max_span = 0;
    for (size_t i = 0; i < planes.size(); ++i) {
        uint64_t min_p_size = calculate_min_plane_size(drm_format, width, height, i, planes[i].stride);
        uint64_t plane_end = static_cast<uint64_t>(planes[i].offset) + min_p_size;
        if (planes[i].size > 0 && planes[i].size > plane_end) {
            plane_end = planes[i].size;
        }
        max_span = std::max(max_span, plane_end);
    }
    return max_span;
}

uint32_t calculate_min_stride(uint32_t drm_format, uint32_t width, size_t plane) noexcept {
    uint32_t h_sub = 1;
    uint32_t v_sub = 1;
    if (!drm_format_plane_subsampling(drm_format, plane, &h_sub, &v_sub)) {
        return 0;
    }

    uint32_t plane_width = (width + h_sub - 1) / h_sub;
    uint32_t bpp = drm_format_bytes_per_pixel(drm_format);
    if (bpp > 0) {
        return plane_width * bpp;
    }

    switch (drm_format) {
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
            // Plane 0: Y (1 byte per pixel), Plane 1: UV/VU (2 bytes per sub-pixel)
            return (plane == 0) ? width : width;

        case DRM_FORMAT_YUV420:
        case DRM_FORMAT_YVU420:
            // Plane 0: Y (1 byte), Plane 1: U (1 byte, subsampled 2), Plane 2: V (1 byte, subsampled 2)
            return (plane == 0) ? width : plane_width;

        case DRM_FORMAT_P010:
            // Plane 0: Y (2 bytes per pixel), Plane 1: UV (4 bytes per sub-pixel)
            return (plane == 0) ? width * 2 : width * 2;

        default:
            return width * 4;
    }
}

uint64_t calculate_min_plane_size(uint32_t drm_format, [[maybe_unused]] uint32_t width, uint32_t height,
                                 size_t plane, uint32_t stride) noexcept {
    uint32_t h_sub = 1;
    uint32_t v_sub = 1;
    if (!drm_format_plane_subsampling(drm_format, plane, &h_sub, &v_sub)) {
        return 0;
    }

    uint32_t plane_height = (height + v_sub - 1) / v_sub;
    return static_cast<uint64_t>(stride) * plane_height;
}

}  // namespace brodmabuf
