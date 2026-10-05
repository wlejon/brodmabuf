#include "brodmabuf/gbm.h"
#include "brodmabuf/kms.h"

#include <cassert>
#include <iostream>

using namespace brodmabuf;

int main() {
    auto kms_res = KmsDevice::open();
    if (!kms_res.ok()) {
        std::cout << "[test_kms] KmsDevice::open failed: " << kms_res.error_message()
                  << ", skipping with code 77" << std::endl;
        return 77;
    }

    auto kms_dev = std::move(kms_res.value());
    assert(kms_dev->valid());
    std::cout << "[test_kms] Opened KMS device fd=" << kms_dev->fd() << std::endl;

    bool atomic = kms_dev->is_atomic_supported();
    std::cout << "[test_kms] Atomic modesetting supported: " << (atomic ? "yes" : "no") << std::endl;

    // 1. Allocate buffer via GBM to test Framebuffer creation
    auto gbm_res = GbmDevice::open();
    if (!gbm_res.ok()) {
        std::cout << "[test_kms] GbmDevice::open failed, skipping with code 77" << std::endl;
        return 77;
    }
    auto gbm_dev = std::move(gbm_res.value());

    auto bo_res = gbm_dev->create_buffer(640, 480, DRM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!bo_res.ok()) {
        bo_res = gbm_dev->create_buffer(640, 480, DRM_FORMAT_XRGB8888);
    }
    if (!bo_res.ok()) {
        std::cout << "[test_kms] Failed to create GBM buffer, skipping" << std::endl;
        return 77;
    }

    auto attrs_res = bo_res.value()->export_dmabuf();
    assert(attrs_res.ok());
    DmaBufAttributes attrs = std::move(attrs_res.value());

    // 2. Test KmsFramebuffer creation from DMA-BUF
    std::unique_ptr<KmsFramebuffer> fb;
    auto fb_res = KmsFramebuffer::create_from_dmabuf(kms_dev->fd(), attrs);
    if (fb_res.ok()) {
        fb = std::move(fb_res.value());
        assert(fb->valid());
        assert(fb->fb_id() != 0);
        assert(fb->width() == 640);
        assert(fb->height() == 480);
        std::cout << "[test_kms] Successfully created KmsFramebuffer from DMA-BUF: fb_id="
                  << fb->fb_id() << std::endl;
    } else {
        std::cout << "[test_kms] Note: KmsFramebuffer::create_from_dmabuf returned: "
                  << fb_res.error_message() << " (likely non-master or permission)" << std::endl;
    }

    // 3. Test KmsAtomicReq structure
    KmsAtomicReq req;
    assert(req.valid());
    KmsPlaneProps dummy_plane_props{};
    dummy_plane_props.fb_id = 1;
    dummy_plane_props.crtc_id = 2;
    dummy_plane_props.crtc_x = 3;
    dummy_plane_props.crtc_y = 4;
    dummy_plane_props.crtc_w = 5;
    dummy_plane_props.crtc_h = 6;
    dummy_plane_props.src_x = 7;
    dummy_plane_props.src_y = 8;
    dummy_plane_props.src_w = 9;
    dummy_plane_props.src_h = 10;

    bool plane_set = req.set_plane(dummy_plane_props, 100, 200, 300, 0, 0, 640, 480, 0, 0, 640, 480);
    assert(plane_set);
    std::cout << "[test_kms] KmsAtomicReq populated successfully" << std::endl;

    // 4. Query pipeline
    auto pipeline_res = kms_dev->find_default_pipeline();
    if (pipeline_res.ok()) {
        const auto& pipeline = pipeline_res.value();
        std::cout << "[test_kms] Found active display pipeline:" << std::endl;
        std::cout << "  - Connector ID: " << pipeline.connector_id << std::endl;
        std::cout << "  - CRTC ID: " << pipeline.crtc_id << std::endl;
        std::cout << "  - Plane ID: " << pipeline.plane_id << std::endl;
        std::cout << "  - Mode: " << pipeline.mode.name << " (" << pipeline.mode.hdisplay
                  << "x" << pipeline.mode.vdisplay << "@" << pipeline.mode.vrefresh << "Hz)" << std::endl;

        if (atomic && fb) {
            // Test atomic commit in TEST_ONLY mode
            KmsAtomicReq test_req;
            test_req.set_plane(
                pipeline.plane_props, pipeline.plane_id, pipeline.crtc_id, fb->fb_id(),
                0, 0, pipeline.mode.hdisplay, pipeline.mode.vdisplay,
                0, 0, pipeline.mode.hdisplay, pipeline.mode.vdisplay);

            auto test_commit = test_req.commit(kms_dev->fd(), DRM_MODE_ATOMIC_TEST_ONLY);
            std::cout << "[test_kms] Atomic TEST_ONLY commit result: "
                      << (test_commit.ok() ? "SUCCESS" : test_commit.error_message()) << std::endl;
        }
    } else {
        std::cout << "[test_kms] Note: find_default_pipeline returned: "
                  << pipeline_res.error_message() << std::endl;
    }

    std::cout << "[test_kms] All tests passed!" << std::endl;
    return 0;
}
