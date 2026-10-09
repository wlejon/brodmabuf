// KMS scanout of DMA-BUFs (Linux).
//
// Framebuffer creation from a GBM-allocated DMA-BUF and atomic request
// building run on any KMS device. Modesetting needs DRM master, which a
// running display server holds: with master (CI's vkms card, or a bare VT)
// the test lights the first connected output with a DMA-BUF framebuffer,
// flips between two buffers and waits for the kernel's page-flip events and
// out-fences. Without it those steps skip and say why.
//
// BRODMABUF_KMS_DEVICE (or BRODMABUF_DRM_DEVICE) picks the card node.
#include "check.h"
#include "drm_node.h"
#include "brodmabuf/gbm.h"
#include "brodmabuf/kms.h"
#include "brodmabuf/sync.h"

#include <unistd.h>
#include <xf86drm.h>

#include <memory>
#include <string>

using namespace brodmabuf;

namespace {

std::string card_node() {
    std::string n = bstest::env("BRODMABUF_KMS_DEVICE");
    if (n.empty()) n = bstest::env("BRODMABUF_DRM_DEVICE");
    if (n.empty()) n = find_card_node();
    return n;
}

std::unique_ptr<GbmBuffer> scanout_buffer(GbmDevice& gbm, uint32_t w, uint32_t h, uint32_t argb) {
    auto bo = gbm.create_buffer(w, h, DRM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    if (!bo.ok()) bo = gbm.create_buffer(w, h, DRM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT);
    if (!bo.ok()) return nullptr;
    auto map = bo.value()->map(0, 0, w, h, GBM_BO_TRANSFER_WRITE);
    if (map && map->valid()) {
        for (uint32_t y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(map->data()) + size_t(y) * map->stride());
            for (uint32_t x = 0; x < w; ++x) row[x] = argb;
        }
    }
    return std::move(bo.value());
}

void test_atomic_request() {
    KmsAtomicReq req;
    CHECK(req.valid());
    KmsPlaneProps props{};
    props.fb_id = 1;
    props.crtc_id = 2;
    props.crtc_x = 3;
    props.crtc_y = 4;
    props.crtc_w = 5;
    props.crtc_h = 6;
    props.src_x = 7;
    props.src_y = 8;
    props.src_w = 9;
    props.src_h = 10;
    CHECK(req.set_plane(props, 100, 200, 300, 0, 0, 640, 480, 0, 0, 640, 480));
    CHECK(drmModeAtomicGetCursor(req.handle()) == 10);
    // A plane without IN_FENCE_FD, a CRTC without OUT_FENCE_PTR: refused.
    CHECK(!req.set_in_fence(KmsPlaneProps{}, 100, 5));
    int32_t out = -1;
    CHECK(!req.set_out_fence_ptr(KmsCrtcProps{}, 200, &out));
    CHECK(!req.add_property(1, 0, 0));  // property id 0

    KmsAtomicReq moved = std::move(req);
    CHECK(moved.valid());
    CHECK(!req.valid());
}

// Framebuffer creation from a DMA-BUF needs no master.
void test_framebuffer(KmsDevice& kms, GbmDevice& gbm) {
    auto bo = scanout_buffer(gbm, 256, 128, 0xff00ff00u);
    REQUIRE(bo != nullptr);
    auto attrs = bo->export_dmabuf();
    REQUIRE_OK(attrs);
    auto fb = KmsFramebuffer::create_from_dmabuf(kms.fd(), attrs.value());
    REQUIRE_OK(fb);
    CHECK(fb.value()->valid());
    CHECK(fb.value()->fb_id() != 0u);
    CHECK_EQ(fb.value()->width(), 256u);
    CHECK_EQ(fb.value()->height(), 128u);
    CHECK_EQ(fb.value()->format(), uint32_t(DRM_FORMAT_XRGB8888));
    // The kernel knows the framebuffer by that id.
    drmModeFB2Ptr info = drmModeGetFB2(kms.fd(), fb.value()->fb_id());
    if (info) {
        CHECK_EQ(info->width, 256u);
        CHECK_EQ(info->height, 128u);
        CHECK_EQ(info->pixel_format, uint32_t(DRM_FORMAT_XRGB8888));
        drmModeFreeFB2(info);
    } else {
        bstest::fail(__FILE__, __LINE__, "drmModeGetFB2 does not know the new framebuffer");
    }
    // Releasing it removes it from the kernel.
    const uint32_t id = fb.value()->fb_id();
    fb.value().reset();
    CHECK(drmModeGetFB2(kms.fd(), id) == nullptr);

    KmsFramebuffer empty(-1, 0, 0, 0, 0);
    CHECK(!empty.valid());
    DmaBufAttributes bad;
    CHECK(!KmsFramebuffer::create_from_dmabuf(kms.fd(), bad).ok());
}

// A dumb ARGB buffer (the cursor's) and the pipeline's cursor plane: neither
// needs master.
void test_cursor_buffer(KmsDevice& kms) {
    auto buf = KmsDumbBuffer::create(kms.fd(), 64, 64);
    REQUIRE_OK(buf);
    KmsDumbBuffer& b = *buf.value();
    CHECK(b.fb_id() != 0u);
    CHECK(b.pixels() != nullptr);
    CHECK(b.stride() >= 64u * 4u);
    b.pixels()[0] = 0xff;
    b.pixels()[size_t(63) * b.stride() + 63 * 4 + 3] = 0xff;
    drmModeFB2Ptr info = drmModeGetFB2(kms.fd(), b.fb_id());
    if (info) {
        CHECK_EQ(info->width, 64u);
        CHECK_EQ(info->pixel_format, uint32_t(DRM_FORMAT_ARGB8888));
        drmModeFreeFB2(info);
    } else {
        bstest::fail(__FILE__, __LINE__, "drmModeGetFB2 does not know the dumb framebuffer");
    }
    CHECK(!KmsDumbBuffer::create(kms.fd(), 0, 64).ok());

    auto pipe = kms.find_default_pipeline();
    if (!pipe.ok()) return;
    if (pipe.value().cursor_plane_id == 0) {
        bstest::skip_check("cursor plane", "the CRTC has no cursor plane");
        return;
    }
    std::printf("cursor plane %u, %ux%u\n", pipe.value().cursor_plane_id, pipe.value().cursor_width,
                pipe.value().cursor_height);
    CHECK(pipe.value().cursor_plane_props.fb_id != 0u);
    CHECK(pipe.value().cursor_plane_props.crtc_x != 0u);
    CHECK(pipe.value().cursor_width >= 32u);
    CHECK(pipe.value().cursor_height >= 32u);
}

void test_modeset(const std::shared_ptr<KmsDevice>& kms, GbmDevice& gbm) {
    auto pipe_res = kms->find_default_pipeline();
    if (!pipe_res.ok()) {
        bstest::skip_check("modeset", "no connected output: " + std::string(pipe_res.error_message()));
        return;
    }
    const KmsPipeline& pipe = pipe_res.value();
    std::printf("pipeline: connector %u, crtc %u, plane %u, mode %s %ux%u@%u\n", pipe.connector_id,
                pipe.crtc_id, pipe.plane_id, pipe.mode.name, pipe.mode.hdisplay, pipe.mode.vdisplay,
                pipe.mode.vrefresh);
    CHECK(pipe.crtc_id != 0u);
    REQUIRE(pipe.plane_id != 0u);
    CHECK(pipe.plane_props.fb_id != 0u);
    CHECK(pipe.crtc_props.mode_id != 0u);
    CHECK(pipe.crtc_props.active != 0u);
    CHECK(pipe.connector_props.crtc_id != 0u);

    const uint32_t w = pipe.mode.hdisplay, h = pipe.mode.vdisplay;
    auto red = scanout_buffer(gbm, w, h, 0xffff0000u);
    auto blue = scanout_buffer(gbm, w, h, 0xff0000ffu);
    REQUIRE(red && blue);
    auto red_attrs = red->export_dmabuf();
    auto blue_attrs = blue->export_dmabuf();
    REQUIRE_OK(red_attrs);
    REQUIRE_OK(blue_attrs);
    auto fb_red = KmsFramebuffer::create_from_dmabuf(kms->fd(), red_attrs.value());
    auto fb_blue = KmsFramebuffer::create_from_dmabuf(kms->fd(), blue_attrs.value());
    REQUIRE_OK(fb_red);
    REQUIRE_OK(fb_blue);

    // The kernel validates the whole configuration without applying it.
    KmsAtomicReq probe;
    CHECK(probe.add_property(pipe.connector_id, pipe.connector_props.crtc_id, pipe.crtc_id));
    auto blob = kms->create_mode_blob(pipe.mode);
    REQUIRE_OK(blob);
    CHECK(probe.add_property(pipe.crtc_id, pipe.crtc_props.mode_id, blob.value()));
    CHECK(probe.add_property(pipe.crtc_id, pipe.crtc_props.active, 1));
    CHECK(probe.set_plane(pipe.plane_props, pipe.plane_id, pipe.crtc_id, fb_red.value()->fb_id(), 0, 0, w, h, 0,
                          0, w, h));
    CHECK_OK(probe.commit(kms->fd(), DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET));
    kms->destroy_mode_blob(blob.value());

    auto presenter_res = KmsPresenter::create(kms, pipe);
    REQUIRE_OK(presenter_res);
    auto& presenter = presenter_res.value();
    CHECK_OK(presenter->initialize_modeset(*fb_red.value()));

    // Blocking flip to blue: the commit returns after it is on screen, and its
    // page-flip event is queued on the fd.
    auto out = presenter->present(*fb_blue.value(), -1, true);
    REQUIRE_OK(out);
    CHECK(presenter->handle_event(1000));
    if (pipe.crtc_props.out_fence_ptr != 0) {
        REQUIRE(out.value().valid());
        CHECK(SyncFile(std::move(out.value())).wait(1000));
    } else {
        bstest::skip_check("out-fence", "the CRTC has no OUT_FENCE_PTR property");
    }

    // Non-blocking flip back to red, gated on an (already signalled) in-fence:
    // the page-flip event arrives within a few frames.
    int in_fence = -1;
    UniqueFd in_fence_owner;
    uint64_t syncobj_cap = 0;
    if (drmGetCap(kms->fd(), DRM_CAP_SYNCOBJ, &syncobj_cap) == 0 && syncobj_cap) {
        auto so = SyncObj::create(kms->fd(), DRM_SYNCOBJ_CREATE_SIGNALED);
        if (so.ok()) {
            auto f = so.value()->export_sync_file();
            if (f.ok()) {
                in_fence_owner = std::move(f.value());
                in_fence = in_fence_owner.get();
            }
        }
    }
    if (in_fence < 0) bstest::skip_check("in-fence", "the driver has no syncobj to make a sync_file from");
    auto out2 = presenter->present(*fb_red.value(), in_fence, false);
    REQUIRE_OK(out2);
    CHECK(presenter->handle_event(1000));

    // The cursor plane: up with a frame, then moved alone.
    if (presenter->has_cursor_plane()) {
        auto cur = KmsDumbBuffer::create(kms->fd(), pipe.cursor_width, pipe.cursor_height);
        REQUIRE_OK(cur);
        KmsDumbBuffer& c = *cur.value();
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 16; ++x)
                reinterpret_cast<uint32_t*>(c.pixels() + size_t(y) * c.stride())[x] = 0xffffffffu;
        presenter->set_cursor(KmsCursor{c.fb_id(), 100, 100, c.width(), c.height()});
        auto out3 = presenter->present(*fb_blue.value(), -1, false);
        REQUIRE_OK(out3);
        CHECK(!presenter->cursor_refused());
        CHECK(presenter->handle_event(1000));
        presenter->set_cursor(KmsCursor{c.fb_id(), -4, 200, c.width(), c.height()});
        CHECK_OK(presenter->commit_cursor());
        CHECK(presenter->handle_event(1000));
        presenter->set_cursor(KmsCursor{});
        CHECK_OK(presenter->commit_cursor());
        CHECK(presenter->handle_event(1000));
    } else {
        bstest::skip_check("cursor plane", "the CRTC has no cursor plane");
    }

    // Turn the output off again so the test leaves the CRTC as it found it.
    KmsAtomicReq off;
    off.add_property(pipe.plane_id, pipe.plane_props.fb_id, 0);
    off.add_property(pipe.plane_id, pipe.plane_props.crtc_id, 0);
    off.add_property(pipe.crtc_id, pipe.crtc_props.active, 0);
    off.add_property(pipe.crtc_id, pipe.crtc_props.mode_id, 0);
    off.add_property(pipe.connector_id, pipe.connector_props.crtc_id, 0);
    CHECK_OK(off.commit(kms->fd(), DRM_MODE_ATOMIC_ALLOW_MODESET));
}

}  // namespace

int main() {
    test_atomic_request();

    const std::string node = card_node();
    if (node.empty()) bstest::skip("test_kms", "no accessible DRM card node");
    std::printf("KMS node: %s\n", node.c_str());
    auto kms_res = KmsDevice::open(node);
    if (!kms_res.ok()) bstest::skip("test_kms", "cannot open " + node + ": " + std::string(kms_res.error_message()));
    std::shared_ptr<KmsDevice> kms = std::move(kms_res.value());
    CHECK(kms->valid());
    if (!kms->is_atomic_supported()) bstest::skip("test_kms", node + " has no atomic modesetting");

    // GBM on the card node itself, so the buffers are scanout-capable there.
    auto gbm_res = GbmDevice::wrap_fd(UniqueFd(::dup(kms->fd())));
    if (!gbm_res.ok()) bstest::skip("test_kms", "GBM has no backend for " + node);
    auto& gbm = *gbm_res.value();

    test_framebuffer(*kms, gbm);
    test_cursor_buffer(*kms);

    if (drmIsMaster(kms->fd())) {
        test_modeset(kms, gbm);
    } else {
        bstest::skip_check("modeset", "not DRM master on " + node + " (a display server owns it)");
    }
    return bstest::finish("test_kms");
}
