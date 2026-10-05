#pragma once

#include "brodmabuf/buffer.h"
#include "brodmabuf/gbm.h"
#include "brodmabuf/types.h"

#include <memory>
#include <string>
#include <vector>

namespace brodmabuf {

/// Abstract interface and default factory for allocating kernel DMA-BUF buffers.
class DmaBufAllocator {
public:
    virtual ~DmaBufAllocator() = default;

    /// Allocate a DMA-BUF image with the given dimensions, DRM format, and optional modifiers.
    [[nodiscard]] virtual Result<DmaBufAttributes> allocate(
        uint32_t width, uint32_t height, uint32_t drm_format,
        const std::vector<uint64_t>& modifiers = {}) = 0;

    /// Allocate a GBM buffer object with the given dimensions, DRM format, and optional modifiers.
    [[nodiscard]] virtual Result<std::unique_ptr<GbmBuffer>> allocate_buffer(
        uint32_t width, uint32_t height, uint32_t drm_format,
        const std::vector<uint64_t>& modifiers = {}) = 0;

    /// File descriptor of the underlying DRM device.
    [[nodiscard]] virtual int drm_fd() const noexcept = 0;

    /// Whether this allocator is valid and ready to allocate.
    [[nodiscard]] virtual bool is_valid() const noexcept = 0;

    /// Create default DMA-BUF allocator using the first accessible DRM render node (/dev/dri/renderD128).
    [[nodiscard]] static Result<std::unique_ptr<DmaBufAllocator>> create_default();

    /// Create a GBM-backed DMA-BUF allocator for the specified DRM device node.
    [[nodiscard]] static Result<std::unique_ptr<DmaBufAllocator>> create_gbm(const std::string& node = "");
};

}  // namespace brodmabuf
