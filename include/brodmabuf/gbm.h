#pragma once

#if !defined(__linux__)
#error "brodmabuf/gbm.h is Linux-only (Mesa GBM). Off Linux use allocator.h, whose factories report why DMA-BUF is unavailable."
#endif

#include "brodmabuf/buffer.h"
#include "brodmabuf/types.h"

#include <gbm.h>

#include <memory>
#include <string>
#include <vector>

namespace brodmabuf {

class GbmDevice;

/// RAII wrapper around a GBM buffer object (`gbm_bo`).
class GbmBuffer {
public:
    /// RAII wrapper for a mapped region of a GBM buffer.
    class Mapping {
    public:
        Mapping(struct gbm_bo* bo, void* data, uint32_t stride, void* map_data) noexcept
            : bo_(bo), data_(data), stride_(stride), map_data_(map_data) {}
        ~Mapping() noexcept;

        Mapping(const Mapping&) = delete;
        Mapping& operator=(const Mapping&) = delete;
        Mapping(Mapping&& other) noexcept;
        Mapping& operator=(Mapping&& other) noexcept;

        [[nodiscard]] void* data() noexcept { return data_; }
        [[nodiscard]] const void* data() const noexcept { return data_; }
        [[nodiscard]] uint32_t stride() const noexcept { return stride_; }
        [[nodiscard]] bool valid() const noexcept { return data_ != nullptr; }

    private:
        struct gbm_bo* bo_ = nullptr;
        void* data_ = nullptr;
        uint32_t stride_ = 0;
        void* map_data_ = nullptr;
    };

    explicit GbmBuffer(struct gbm_bo* bo) noexcept : bo_(bo) {}
    ~GbmBuffer() noexcept;

    GbmBuffer(const GbmBuffer&) = delete;
    GbmBuffer& operator=(const GbmBuffer&) = delete;
    GbmBuffer(GbmBuffer&& other) noexcept;
    GbmBuffer& operator=(GbmBuffer&& other) noexcept;

    [[nodiscard]] struct gbm_bo* handle() const noexcept { return bo_; }
    [[nodiscard]] bool valid() const noexcept { return bo_ != nullptr; }
    explicit operator bool() const noexcept { return valid(); }

    [[nodiscard]] uint32_t width() const noexcept;
    [[nodiscard]] uint32_t height() const noexcept;
    [[nodiscard]] uint32_t format() const noexcept;
    [[nodiscard]] uint32_t stride() const noexcept;
    [[nodiscard]] uint32_t stride(int plane) const noexcept;
    [[nodiscard]] uint32_t offset(int plane) const noexcept;
    [[nodiscard]] uint64_t modifier() const noexcept;
    [[nodiscard]] int plane_count() const noexcept;

    /// Export single/primary file descriptor. Exports are read-write
    /// (DRM_RDWR), so an importer can mmap them for writing; some GBM
    /// backends (kms_swrast) would otherwise hand out read-only ones.
    [[nodiscard]] UniqueFd export_fd() const noexcept;

    /// Export file descriptor for a specific plane (0-indexed), read-write.
    [[nodiscard]] UniqueFd export_fd(int plane) const noexcept;

    /// Export complete DMA-BUF attributes including plane fds, strides, offsets, and modifier.
    [[nodiscard]] Result<DmaBufAttributes> export_dmabuf() const;

    /// Map a rectangle of the buffer for CPU access.
    [[nodiscard]] std::unique_ptr<Mapping> map(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                              uint32_t transfer_flags = GBM_BO_TRANSFER_READ_WRITE);

    /// Write raw data into the buffer via `gbm_bo_write`.
    bool write(const void* data, size_t count) noexcept;

private:
    struct gbm_bo* bo_ = nullptr;
};

/// RAII wrapper around a GBM device (`gbm_device`).
class GbmDevice {
public:
    explicit GbmDevice(struct gbm_device* dev, UniqueFd drm_fd) noexcept
        : device_(dev), drm_fd_(std::move(drm_fd)) {}
    ~GbmDevice() noexcept;

    GbmDevice(const GbmDevice&) = delete;
    GbmDevice& operator=(const GbmDevice&) = delete;
    GbmDevice(GbmDevice&& other) noexcept;
    GbmDevice& operator=(GbmDevice&& other) noexcept;

    /// Open a DRM node (e.g. "/dev/dri/renderD128"). If node is empty, automatically finds first accessible render node.
    [[nodiscard]] static Result<std::unique_ptr<GbmDevice>> open(const std::string& node = "");

    /// Wrap an existing DRM file descriptor. Takes ownership of the file descriptor.
    [[nodiscard]] static Result<std::unique_ptr<GbmDevice>> wrap_fd(UniqueFd drm_fd);

    [[nodiscard]] struct gbm_device* handle() const noexcept { return device_; }
    [[nodiscard]] int drm_fd() const noexcept { return drm_fd_.get(); }
    [[nodiscard]] bool valid() const noexcept { return device_ != nullptr; }

    /// Allocate a buffer object with standard flags.
    [[nodiscard]] Result<std::unique_ptr<GbmBuffer>> create_buffer(
        uint32_t width, uint32_t height, uint32_t drm_format, uint32_t flags = GBM_BO_USE_RENDERING);

    /// Allocate a buffer object selecting from a list of DRM format modifiers.
    [[nodiscard]] Result<std::unique_ptr<GbmBuffer>> create_buffer_with_modifiers(
        uint32_t width, uint32_t height, uint32_t drm_format,
        const std::vector<uint64_t>& modifiers, uint32_t flags = 0);

    /// Import an existing DMA-BUF image into GBM.
    [[nodiscard]] Result<std::unique_ptr<GbmBuffer>> import_buffer(
        const DmaBufAttributes& attrs, uint32_t flags = 0);

    /// Query if the device supports the specified DRM format for buffer creation.
    [[nodiscard]] bool is_format_supported(uint32_t drm_format, uint32_t flags) const noexcept;

private:
    struct gbm_device* device_ = nullptr;
    UniqueFd drm_fd_;
};

/// Locate the first accessible DRM render node ("/dev/dri/renderD*").
/// Returns empty string if none found.
std::string find_render_node();

/// Locate the first accessible DRM primary card node ("/dev/dri/card*").
/// Returns empty string if none found.
std::string find_card_node();

}  // namespace brodmabuf
