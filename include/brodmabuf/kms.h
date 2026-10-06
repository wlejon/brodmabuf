#pragma once

#if !defined(__linux__)
#error "brodmabuf/kms.h is Linux-only (DRM/KMS). Off Linux use allocator.h, whose factories report why DMA-BUF is unavailable."
#endif

#include "brodmabuf/buffer.h"
#include "brodmabuf/types.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace brodmabuf {

/// RAII wrapper for a DRM Framebuffer (`drmModeRmFB`).
class KmsFramebuffer {
public:
    KmsFramebuffer(int drm_fd, uint32_t fb_id, uint32_t width, uint32_t height, uint32_t format) noexcept
        : drm_fd_(drm_fd), fb_id_(fb_id), width_(width), height_(height), format_(format) {}
    ~KmsFramebuffer() noexcept;

    KmsFramebuffer(const KmsFramebuffer&) = delete;
    KmsFramebuffer& operator=(const KmsFramebuffer&) = delete;
    KmsFramebuffer(KmsFramebuffer&& other) noexcept;
    KmsFramebuffer& operator=(KmsFramebuffer&& other) noexcept;

    /// Create a KMS framebuffer from a DMA-BUF using `drmModeAddFB2WithModifiers` (or `drmModeAddFB2`).
    [[nodiscard]] static Result<std::unique_ptr<KmsFramebuffer>> create_from_dmabuf(
        int drm_fd, const DmaBufAttributes& attrs);

    [[nodiscard]] uint32_t fb_id() const noexcept { return fb_id_; }
    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] uint32_t format() const noexcept { return format_; }
    [[nodiscard]] int drm_fd() const noexcept { return drm_fd_; }
    [[nodiscard]] bool valid() const noexcept { return fb_id_ != 0 && drm_fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

private:
    int drm_fd_ = -1;
    uint32_t fb_id_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t format_ = 0;
};

/// Cached property IDs for a DRM Plane object.
struct KmsPlaneProps {
    uint32_t fb_id = 0;
    uint32_t crtc_id = 0;
    uint32_t crtc_x = 0;
    uint32_t crtc_y = 0;
    uint32_t crtc_w = 0;
    uint32_t crtc_h = 0;
    uint32_t src_x = 0;
    uint32_t src_y = 0;
    uint32_t src_w = 0;
    uint32_t src_h = 0;
    uint32_t in_fence_fd = 0;
    uint32_t type = 0;
};

/// Cached property IDs for a DRM CRTC object.
struct KmsCrtcProps {
    uint32_t active = 0;
    uint32_t mode_id = 0;
    uint32_t out_fence_ptr = 0;
};

/// Cached property IDs for a DRM Connector object.
struct KmsConnectorProps {
    uint32_t crtc_id = 0;
    uint32_t link_status = 0;
};

/// Description of an active KMS display pipeline.
struct KmsPipeline {
    uint32_t connector_id = 0;
    uint32_t crtc_id = 0;
    uint32_t plane_id = 0;
    drmModeModeInfo mode{};
    KmsPlaneProps plane_props;
    KmsCrtcProps crtc_props;
    KmsConnectorProps connector_props;
};

/// RAII wrapper for an atomic modesetting request (`drmModeAtomicReqPtr`).
class KmsAtomicReq {
public:
    KmsAtomicReq() noexcept;
    ~KmsAtomicReq() noexcept;

    KmsAtomicReq(const KmsAtomicReq&) = delete;
    KmsAtomicReq& operator=(const KmsAtomicReq&) = delete;
    KmsAtomicReq(KmsAtomicReq&& other) noexcept;
    KmsAtomicReq& operator=(KmsAtomicReq&& other) noexcept;

    [[nodiscard]] drmModeAtomicReqPtr handle() const noexcept { return req_; }
    [[nodiscard]] bool valid() const noexcept { return req_ != nullptr; }

    /// Add a property value to the atomic request.
    bool add_property(uint32_t object_id, uint32_t property_id, uint64_t value) noexcept;

    /// Configure a primary or overlay plane in the atomic request.
    bool set_plane(
        const KmsPlaneProps& props, uint32_t plane_id, uint32_t crtc_id, uint32_t fb_id,
        int32_t crtc_x, int32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h,
        uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h) noexcept;

    /// Set an explicit synchronization in-fence file descriptor on a plane.
    bool set_in_fence(const KmsPlaneProps& props, uint32_t plane_id, int fence_fd) noexcept;

    /// Request an explicit synchronization out-fence file descriptor on a CRTC.
    /// The kernel writes the fence fd (an s32) to *out_fence_fd when the commit
    /// succeeds; it must stay valid until commit() returns.
    bool set_out_fence_ptr(const KmsCrtcProps& props, uint32_t crtc_id, int32_t* out_fence_fd) noexcept;

    /// Commit this atomic request.
    Result<void> commit(int drm_fd, uint32_t flags, void* user_data = nullptr) noexcept;

private:
    drmModeAtomicReqPtr req_ = nullptr;
};

/// RAII wrapper for a DRM card node device providing KMS access.
class KmsDevice {
public:
    explicit KmsDevice(UniqueFd drm_fd) noexcept : drm_fd_(std::move(drm_fd)) {}
    ~KmsDevice() noexcept = default;

    KmsDevice(const KmsDevice&) = delete;
    KmsDevice& operator=(const KmsDevice&) = delete;
    KmsDevice(KmsDevice&&) noexcept = default;
    KmsDevice& operator=(KmsDevice&&) noexcept = default;

    /// Open a DRM card node (e.g. "/dev/dri/card0"). If empty, finds first accessible card node.
    [[nodiscard]] static Result<std::unique_ptr<KmsDevice>> open(const std::string& path = "");

    /// Wrap an existing DRM file descriptor.
    [[nodiscard]] static Result<std::unique_ptr<KmsDevice>> wrap_fd(UniqueFd drm_fd);

    [[nodiscard]] int fd() const noexcept { return drm_fd_.get(); }
    [[nodiscard]] bool valid() const noexcept { return drm_fd_.valid(); }

    /// Whether this DRM device supports the DRM Atomic Modesetting API.
    [[nodiscard]] bool is_atomic_supported() const noexcept;

    /// Discover a working connector, CRTC, primary plane, and display mode.
    [[nodiscard]] Result<KmsPipeline> find_default_pipeline() const;

    /// Query atomic property IDs for a specific plane.
    [[nodiscard]] KmsPlaneProps query_plane_props(uint32_t plane_id) const;

    /// Query atomic property IDs for a specific CRTC.
    [[nodiscard]] KmsCrtcProps query_crtc_props(uint32_t crtc_id) const;

    /// Query atomic property IDs for a specific connector.
    [[nodiscard]] KmsConnectorProps query_connector_props(uint32_t connector_id) const;

    /// Create a property blob containing video mode information.
    [[nodiscard]] Result<uint32_t> create_mode_blob(const drmModeModeInfo& mode) const;

    /// Destroy a property blob.
    void destroy_mode_blob(uint32_t blob_id) const noexcept;

private:
    UniqueFd drm_fd_;
};

/// Presenter managing atomic page flips and presentation loop on a KMS display pipeline.
class KmsPresenter {
public:
    KmsPresenter(std::shared_ptr<KmsDevice> dev, KmsPipeline pipeline, uint32_t mode_blob_id) noexcept
        : dev_(std::move(dev)), pipeline_(pipeline), mode_blob_id_(mode_blob_id) {}
    ~KmsPresenter() noexcept;

    KmsPresenter(const KmsPresenter&) = delete;
    KmsPresenter& operator=(const KmsPresenter&) = delete;
    KmsPresenter(KmsPresenter&& other) noexcept;
    KmsPresenter& operator=(KmsPresenter&& other) noexcept;

    /// Create a presenter for the given device and pipeline.
    [[nodiscard]] static Result<std::unique_ptr<KmsPresenter>> create(
        std::shared_ptr<KmsDevice> dev, const KmsPipeline& pipeline);

    /// Initial modeset commit attaching the initial framebuffer to the pipeline.
    [[nodiscard]] Result<void> initialize_modeset(KmsFramebuffer& initial_fb);

    /// Atomic presentation flip.
    /// in_fence_fd: optional fence to wait on before scanout (-1 if none).
    /// blocking: if true, waits for vblank flip; if false, non-blocking asynchronous flip.
    /// Returns out-fence fd when explicit sync is supported and requested.
    [[nodiscard]] Result<UniqueFd> present(KmsFramebuffer& fb, int in_fence_fd = -1, bool blocking = false);

    /// Process page-flip events via `drmHandleEvent`. timeout_ms: milliseconds to wait.
    [[nodiscard]] bool handle_event(int timeout_ms = 100);

    [[nodiscard]] const KmsPipeline& pipeline() const noexcept { return pipeline_; }

private:
    std::shared_ptr<KmsDevice> dev_;
    KmsPipeline pipeline_;
    uint32_t mode_blob_id_ = 0;
};

}  // namespace brodmabuf
