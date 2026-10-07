#include "brodmabuf/kms.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace brodmabuf {

namespace {

uint32_t find_property_id(int fd, uint32_t obj_id, uint32_t obj_type, const char* name) {
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (!props) return 0;

    uint32_t found_id = 0;
    for (uint32_t i = 0; i < props->count_props; ++i) {
        drmModePropertyPtr p = drmModeGetProperty(fd, props->props[i]);
        if (!p) continue;
        if (std::strcmp(p->name, name) == 0) {
            found_id = p->prop_id;
            drmModeFreeProperty(p);
            break;
        }
        drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    return found_id;
}

}  // namespace

// -----------------------------------------------------------------------------
// KmsFramebuffer
// -----------------------------------------------------------------------------

KmsFramebuffer::~KmsFramebuffer() noexcept {
    if (fb_id_ != 0 && drm_fd_ >= 0) {
        drmModeRmFB(drm_fd_, fb_id_);
        fb_id_ = 0;
    }
}

KmsFramebuffer::KmsFramebuffer(KmsFramebuffer&& other) noexcept
    : drm_fd_(other.drm_fd_), fb_id_(other.fb_id_),
      width_(other.width_), height_(other.height_), format_(other.format_) {
    other.drm_fd_ = -1;
    other.fb_id_ = 0;
}

KmsFramebuffer& KmsFramebuffer::operator=(KmsFramebuffer&& other) noexcept {
    if (this != &other) {
        if (fb_id_ != 0 && drm_fd_ >= 0) {
            drmModeRmFB(drm_fd_, fb_id_);
        }
        drm_fd_ = other.drm_fd_;
        fb_id_ = other.fb_id_;
        width_ = other.width_;
        height_ = other.height_;
        format_ = other.format_;

        other.drm_fd_ = -1;
        other.fb_id_ = 0;
    }
    return *this;
}

Result<std::unique_ptr<KmsFramebuffer>> KmsFramebuffer::create_from_dmabuf(
    int drm_fd, const DmaBufAttributes& attrs) {
    if (drm_fd < 0 || !attrs.is_valid()) {
        return Status::invalid_argument("Invalid DRM fd or DmaBufAttributes");
    }

    uint32_t bo_handles[4]{0, 0, 0, 0};
    uint32_t pitches[4]{0, 0, 0, 0};
    uint32_t offsets[4]{0, 0, 0, 0};
    uint64_t modifiers[4]{0, 0, 0, 0};

    const size_t n_planes = attrs.planes.size();
    for (size_t i = 0; i < n_planes; ++i) {
        uint32_t handle = 0;
        int ret = drmPrimeFDToHandle(drm_fd, attrs.planes[i].fd.get(), &handle);
        if (ret != 0 || handle == 0) {
            for (size_t j = 0; j < i; ++j) {
                if (bo_handles[j] != 0) drmCloseBufferHandle(drm_fd, bo_handles[j]);
            }
            return Status::system_error("drmPrimeFDToHandle failed: " + std::string(std::strerror(errno)));
        }
        bo_handles[i] = handle;
        pitches[i] = attrs.planes[i].stride;
        offsets[i] = attrs.planes[i].offset;
        modifiers[i] = attrs.modifier;
    }

    uint32_t fb_id = 0;
    int ret = -1;

    if (attrs.modifier != DRM_FORMAT_MOD_INVALID) {
        ret = drmModeAddFB2WithModifiers(
            drm_fd, attrs.width, attrs.height, attrs.drm_format,
            bo_handles, pitches, offsets, modifiers, &fb_id, DRM_MODE_FB_MODIFIERS);
    }

    if (ret != 0 && (attrs.modifier == DRM_FORMAT_MOD_LINEAR || attrs.modifier == DRM_FORMAT_MOD_INVALID)) {
        ret = drmModeAddFB2(
            drm_fd, attrs.width, attrs.height, attrs.drm_format,
            bo_handles, pitches, offsets, &fb_id, 0);
    }

    // Close imported buffer handles; the kernel framebuffer object retains internal references
    for (size_t i = 0; i < n_planes; ++i) {
        if (bo_handles[i] != 0) {
            drmCloseBufferHandle(drm_fd, bo_handles[i]);
        }
    }

    if (ret != 0 || fb_id == 0) {
        return Status::system_error("drmModeAddFB2WithModifiers failed: " + std::string(std::strerror(errno)));
    }

    return std::make_unique<KmsFramebuffer>(drm_fd, fb_id, attrs.width, attrs.height, attrs.drm_format);
}

// -----------------------------------------------------------------------------
// KmsAtomicReq
// -----------------------------------------------------------------------------

KmsAtomicReq::KmsAtomicReq() noexcept {
    req_ = drmModeAtomicAlloc();
}

KmsAtomicReq::~KmsAtomicReq() noexcept {
    if (req_) {
        drmModeAtomicFree(req_);
        req_ = nullptr;
    }
}

KmsAtomicReq::KmsAtomicReq(KmsAtomicReq&& other) noexcept : req_(other.req_) {
    other.req_ = nullptr;
}

KmsAtomicReq& KmsAtomicReq::operator=(KmsAtomicReq&& other) noexcept {
    if (this != &other) {
        if (req_) drmModeAtomicFree(req_);
        req_ = other.req_;
        other.req_ = nullptr;
    }
    return *this;
}

bool KmsAtomicReq::add_property(uint32_t object_id, uint32_t property_id, uint64_t value) noexcept {
    if (!req_ || property_id == 0) return false;
    return drmModeAtomicAddProperty(req_, object_id, property_id, value) >= 0;
}

bool KmsAtomicReq::set_plane(
    const KmsPlaneProps& props, uint32_t plane_id, uint32_t crtc_id, uint32_t fb_id,
    int32_t crtc_x, int32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h,
    uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h) noexcept {
    if (!req_) return false;

    bool ok = true;
    ok &= add_property(plane_id, props.fb_id, fb_id);
    ok &= add_property(plane_id, props.crtc_id, crtc_id);
    ok &= add_property(plane_id, props.crtc_x, static_cast<uint64_t>(crtc_x));
    ok &= add_property(plane_id, props.crtc_y, static_cast<uint64_t>(crtc_y));
    ok &= add_property(plane_id, props.crtc_w, crtc_w);
    ok &= add_property(plane_id, props.crtc_h, crtc_h);
    // Source coordinates are in 16.16 fixed-point format
    ok &= add_property(plane_id, props.src_x, static_cast<uint64_t>(src_x) << 16);
    ok &= add_property(plane_id, props.src_y, static_cast<uint64_t>(src_y) << 16);
    ok &= add_property(plane_id, props.src_w, static_cast<uint64_t>(src_w) << 16);
    ok &= add_property(plane_id, props.src_h, static_cast<uint64_t>(src_h) << 16);

    return ok;
}

bool KmsAtomicReq::set_in_fence(const KmsPlaneProps& props, uint32_t plane_id, int fence_fd) noexcept {
    if (!req_ || props.in_fence_fd == 0) return false;
    return add_property(plane_id, props.in_fence_fd, static_cast<uint64_t>(fence_fd));
}

bool KmsAtomicReq::set_out_fence_ptr(const KmsCrtcProps& props, uint32_t crtc_id, int32_t* out_fence_fd) noexcept {
    if (!req_ || props.out_fence_ptr == 0 || !out_fence_fd) return false;
    return add_property(crtc_id, props.out_fence_ptr, reinterpret_cast<uintptr_t>(out_fence_fd));
}

Result<void> KmsAtomicReq::commit(int drm_fd, uint32_t flags, void* user_data) noexcept {
    if (!req_ || drm_fd < 0) return Status::invalid_argument("Invalid request or drm_fd");

    int ret = drmModeAtomicCommit(drm_fd, req_, flags, user_data);
    if (ret != 0) {
        return Status::system_error("drmModeAtomicCommit failed: " + std::string(std::strerror(errno)));
    }

    return Status::ok();
}

// -----------------------------------------------------------------------------
// KmsDevice
// -----------------------------------------------------------------------------

Result<std::unique_ptr<KmsDevice>> KmsDevice::open(const std::string& path) {
    std::string node = path;
    if (node.empty()) {
        for (int i = 0; i < 64; ++i) {
            char p[64];
            std::snprintf(p, sizeof(p), "/dev/dri/card%d", i);
            if (::access(p, R_OK | W_OK) == 0) {
                node = p;
                break;
            }
        }
    }
    if (node.empty()) {
        return Status::not_found("No accessible DRM card node found");
    }

    int fd = ::open(node.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return Status::system_error("Failed to open " + node + ": " + std::strerror(errno));
    }

    return wrap_fd(UniqueFd(fd));
}

Result<std::unique_ptr<KmsDevice>> KmsDevice::wrap_fd(UniqueFd drm_fd) {
    if (!drm_fd.valid()) return Status::invalid_argument("Invalid drm fd");

    drmSetClientCap(drm_fd.get(), DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(drm_fd.get(), DRM_CLIENT_CAP_ATOMIC, 1);

    return std::make_unique<KmsDevice>(std::move(drm_fd));
}

bool KmsDevice::is_atomic_supported() const noexcept {
    if (!drm_fd_.valid()) return false;
    return drmSetClientCap(drm_fd_.get(), DRM_CLIENT_CAP_ATOMIC, 1) == 0;
}

KmsPlaneProps KmsDevice::query_plane_props(uint32_t plane_id) const {
    KmsPlaneProps props;
    int fd = drm_fd_.get();
    props.fb_id = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
    props.crtc_id = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    props.crtc_x = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    props.crtc_y = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    props.crtc_w = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    props.crtc_h = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
    props.src_x = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
    props.src_y = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    props.src_w = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
    props.src_h = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
    props.in_fence_fd = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "IN_FENCE_FD");
    props.type = find_property_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, "type");
    return props;
}

KmsCrtcProps KmsDevice::query_crtc_props(uint32_t crtc_id) const {
    KmsCrtcProps props;
    int fd = drm_fd_.get();
    props.active = find_property_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    props.mode_id = find_property_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    props.out_fence_ptr = find_property_id(fd, crtc_id, DRM_MODE_OBJECT_CRTC, "OUT_FENCE_PTR");
    return props;
}

KmsConnectorProps KmsDevice::query_connector_props(uint32_t connector_id) const {
    KmsConnectorProps props;
    int fd = drm_fd_.get();
    props.crtc_id = find_property_id(fd, connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    props.link_status = find_property_id(fd, connector_id, DRM_MODE_OBJECT_CONNECTOR, "link-status");
    return props;
}

Result<KmsPipeline> KmsDevice::find_default_pipeline() const {
    if (!drm_fd_.valid()) return Status::device_error("KmsDevice fd invalid");

    drmModeResPtr res = drmModeGetResources(drm_fd_.get());
    if (!res) {
        return Status::system_error("drmModeGetResources failed");
    }

    drmModeConnectorPtr conn = nullptr;
    for (int i = 0; i < res->count_connectors; ++i) {
        drmModeConnectorPtr c = drmModeGetConnector(drm_fd_.get(), res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            conn = c;
            break;
        }
        if (c) drmModeFreeConnector(c);
    }

    if (!conn) {
        drmModeFreeResources(res);
        return Status::not_found("No connected display connector found");
    }

    drmModeModeInfo mode = conn->modes[0];
    for (int i = 0; i < conn->count_modes; ++i) {
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            mode = conn->modes[i];
            break;
        }
    }

    uint32_t crtc_id = 0;
    if (conn->encoder_id != 0) {
        drmModeEncoderPtr enc = drmModeGetEncoder(drm_fd_.get(), conn->encoder_id);
        if (enc) {
            crtc_id = enc->crtc_id;
            drmModeFreeEncoder(enc);
        }
    }
    if (crtc_id == 0 && res->count_crtcs > 0) {
        crtc_id = res->crtcs[0];
    }

    int crtc_index = -1;
    for (int i = 0; i < res->count_crtcs; ++i) {
        if (res->crtcs[i] == crtc_id) {
            crtc_index = i;
            break;
        }
    }

    uint32_t primary_plane_id = 0;
    drmModePlaneResPtr plane_res = drmModeGetPlaneResources(drm_fd_.get());
    if (plane_res) {
        for (uint32_t i = 0; i < plane_res->count_planes; ++i) {
            uint32_t pid = plane_res->planes[i];
            drmModePlanePtr plane = drmModeGetPlane(drm_fd_.get(), pid);
            if (!plane) continue;
            const bool crtc_compatible = (crtc_index < 0) || (plane->possible_crtcs & (1u << crtc_index));
            drmModeFreePlane(plane);
            if (!crtc_compatible) continue;

            KmsPlaneProps p_props = query_plane_props(pid);
            if (p_props.type != 0) {
                drmModeObjectPropertiesPtr obj_props = drmModeObjectGetProperties(
                    drm_fd_.get(), pid, DRM_MODE_OBJECT_PLANE);
                if (obj_props) {
                    for (uint32_t j = 0; j < obj_props->count_props; ++j) {
                        if (obj_props->props[j] == p_props.type) {
                            if (obj_props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY) {
                                primary_plane_id = pid;
                            }
                            break;
                        }
                    }
                    drmModeFreeObjectProperties(obj_props);
                }
            }
            if (primary_plane_id != 0) break;
        }
        drmModeFreePlaneResources(plane_res);
    }

    KmsPipeline pipeline;
    pipeline.connector_id = conn->connector_id;
    pipeline.crtc_id = crtc_id;
    pipeline.plane_id = primary_plane_id;
    pipeline.mode = mode;
    pipeline.plane_props = query_plane_props(primary_plane_id);
    pipeline.crtc_props = query_crtc_props(crtc_id);
    pipeline.connector_props = query_connector_props(conn->connector_id);

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return pipeline;
}

Result<uint32_t> KmsDevice::create_mode_blob(const drmModeModeInfo& mode) const {
    if (!drm_fd_.valid()) return Status::device_error("KmsDevice fd invalid");
    uint32_t blob_id = 0;
    int ret = drmModeCreatePropertyBlob(drm_fd_.get(), &mode, sizeof(mode), &blob_id);
    if (ret != 0 || blob_id == 0) {
        return Status::system_error("drmModeCreatePropertyBlob failed: " + std::string(std::strerror(errno)));
    }
    return blob_id;
}

void KmsDevice::destroy_mode_blob(uint32_t blob_id) const noexcept {
    if (drm_fd_.valid() && blob_id != 0) {
        drmModeDestroyPropertyBlob(drm_fd_.get(), blob_id);
    }
}

// -----------------------------------------------------------------------------
// KmsPresenter
// -----------------------------------------------------------------------------

KmsPresenter::~KmsPresenter() noexcept {
    if (dev_ && mode_blob_id_ != 0) {
        dev_->destroy_mode_blob(mode_blob_id_);
        mode_blob_id_ = 0;
    }
}

KmsPresenter::KmsPresenter(KmsPresenter&& other) noexcept
    : dev_(std::move(other.dev_)), pipeline_(other.pipeline_), mode_blob_id_(other.mode_blob_id_) {
    other.mode_blob_id_ = 0;
}

KmsPresenter& KmsPresenter::operator=(KmsPresenter&& other) noexcept {
    if (this != &other) {
        if (dev_ && mode_blob_id_ != 0) {
            dev_->destroy_mode_blob(mode_blob_id_);
        }
        dev_ = std::move(other.dev_);
        pipeline_ = other.pipeline_;
        mode_blob_id_ = other.mode_blob_id_;
        other.mode_blob_id_ = 0;
    }
    return *this;
}

Result<std::unique_ptr<KmsPresenter>> KmsPresenter::create(
    std::shared_ptr<KmsDevice> dev, const KmsPipeline& pipeline) {
    if (!dev || !dev->valid()) return Status::invalid_argument("Invalid KmsDevice");

    auto blob_res = dev->create_mode_blob(pipeline.mode);
    if (!blob_res) {
        return blob_res.status();
    }

    return std::make_unique<KmsPresenter>(std::move(dev), pipeline, blob_res.value());
}

Result<void> KmsPresenter::initialize_modeset(KmsFramebuffer& initial_fb) {
    KmsAtomicReq req;
    req.add_property(pipeline_.connector_id, pipeline_.connector_props.crtc_id, pipeline_.crtc_id);
    req.add_property(pipeline_.crtc_id, pipeline_.crtc_props.mode_id, mode_blob_id_);
    req.add_property(pipeline_.crtc_id, pipeline_.crtc_props.active, 1);

    req.set_plane(
        pipeline_.plane_props, pipeline_.plane_id, pipeline_.crtc_id, initial_fb.fb_id(),
        0, 0, pipeline_.mode.hdisplay, pipeline_.mode.vdisplay,
        0, 0, pipeline_.mode.hdisplay, pipeline_.mode.vdisplay);

    return req.commit(dev_->fd(), DRM_MODE_ATOMIC_ALLOW_MODESET);
}

Result<UniqueFd> KmsPresenter::present(KmsFramebuffer& fb, int in_fence_fd, bool blocking) {
    KmsAtomicReq req;

    req.set_plane(
        pipeline_.plane_props, pipeline_.plane_id, pipeline_.crtc_id, fb.fb_id(),
        0, 0, pipeline_.mode.hdisplay, pipeline_.mode.vdisplay,
        0, 0, pipeline_.mode.hdisplay, pipeline_.mode.vdisplay);

    if (in_fence_fd >= 0) {
        req.set_in_fence(pipeline_.plane_props, pipeline_.plane_id, in_fence_fd);
    }

    int32_t out_fence_fd = -1;
    if (pipeline_.crtc_props.out_fence_ptr != 0) {
        req.set_out_fence_ptr(pipeline_.crtc_props, pipeline_.crtc_id, &out_fence_fd);
    }

    uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT;
    if (!blocking) {
        flags |= DRM_MODE_ATOMIC_NONBLOCK;
    }

    auto status = req.commit(dev_->fd(), flags);
    if (!status) return status.status();

    if (out_fence_fd >= 0) {
        return UniqueFd(out_fence_fd);
    }

    return UniqueFd();
}

bool KmsPresenter::handle_event(int timeout_ms) {
    if (!dev_ || !dev_->valid()) return false;
    struct pollfd pfd{};
    pfd.fd = dev_->fd();
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        drmEventContext evctx{};
        evctx.version = DRM_EVENT_CONTEXT_VERSION;
        drmHandleEvent(dev_->fd(), &evctx);
        return true;
    }
    return false;
}

}  // namespace brodmabuf
