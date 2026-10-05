#include "brodmabuf/gbm.h"

#include <sys/stat.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace brodmabuf;

int main() {
    std::string node = find_render_node();
    if (node.empty()) {
        node = find_card_node();
    }
    if (node.empty()) {
        std::cout << "[test_gbm] No accessible DRM node found, skipping with code 77" << std::endl;
        return 77;
    }

    std::cout << "[test_gbm] Using DRM node: " << node << std::endl;

    auto dev_res = GbmDevice::open(node);
    if (!dev_res.ok()) {
        std::cout << "[test_gbm] Failed to open GBM device: " << dev_res.error_message()
                  << ", skipping with code 77" << std::endl;
        return 77;
    }

    auto dev = std::move(dev_res.value());
    assert(dev->valid());
    assert(dev->drm_fd() >= 0);

    // 1. Create standard buffer
    auto bo_res = dev->create_buffer(640, 480, DRM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    if (!bo_res.ok()) {
        std::cout << "[test_gbm] Failed to allocate GBM buffer: " << bo_res.error_message() << std::endl;
        return 77;
    }

    auto bo = std::move(bo_res.value());
    assert(bo->valid());
    assert(bo->width() == 640);
    assert(bo->height() == 480);
    assert(bo->format() == DRM_FORMAT_XRGB8888);
    assert(bo->stride() >= 640 * 4);
    assert(bo->plane_count() >= 1);

    // 2. Export DMA-BUF
    auto attrs_res = bo->export_dmabuf();
    assert(attrs_res.ok());

    DmaBufAttributes attrs = std::move(attrs_res.value());
    assert(attrs.is_valid());
    assert(attrs.width == 640);
    assert(attrs.height == 480);
    assert(attrs.drm_format == DRM_FORMAT_XRGB8888);
    assert(attrs.planes.size() >= 1);
    assert(attrs.planes[0].fd.valid());
    assert(attrs.planes[0].stride >= 640 * 4);

    // 3. Test RAII lifetime: destroy bo, check that plane fd stays valid
    int plane0_fd = attrs.planes[0].fd.get();
    bo.reset();

    struct stat st{};
    assert(::fstat(plane0_fd, &st) == 0);
    std::cout << "[test_gbm] DMA-BUF fd survived bo destruction (inode=" << st.st_ino << ")" << std::endl;

    // 4. Import DMA-BUF back into GBM
    auto import_res = dev->import_buffer(attrs);
    if (import_res.ok()) {
        auto imported = std::move(import_res.value());
        assert(imported->valid());
        assert(imported->width() == 640);
        assert(imported->height() == 480);
        assert(imported->format() == DRM_FORMAT_XRGB8888);
        std::cout << "[test_gbm] Successfully re-imported DMA-BUF into GBM" << std::endl;
    } else {
        std::cout << "[test_gbm] Note: gbm_bo_import returned: " << import_res.error_message() << std::endl;
    }

    // 5. Test create_buffer_with_modifiers
    std::vector<uint64_t> mods = {DRM_FORMAT_MOD_LINEAR};
    auto mod_bo_res = dev->create_buffer_with_modifiers(640, 480, DRM_FORMAT_ARGB8888, mods);
    if (mod_bo_res.ok()) {
        auto mod_bo = std::move(mod_bo_res.value());
        assert(mod_bo->valid());
        assert(mod_bo->width() == 640);
        assert(mod_bo->height() == 480);
        std::cout << "[test_gbm] Created buffer with modifiers, actual modifier="
                  << drm_modifier_to_string(mod_bo->modifier()) << std::endl;

        // Test mapping
        auto map = mod_bo->map(0, 0, 64, 64, GBM_BO_TRANSFER_READ_WRITE);
        if (map && map->valid()) {
            std::memset(map->data(), 0xaa, 64 * 4);
            std::cout << "[test_gbm] Successfully mapped and wrote to buffer" << std::endl;
        }
    }

    std::cout << "[test_gbm] All tests passed!" << std::endl;
    return 0;
}
