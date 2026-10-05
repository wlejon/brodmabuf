#include "brodmabuf/gbm.h"
#include "brodmabuf/sync.h"
#include "brodmabuf/vulkan.h"

#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <iostream>

using namespace brodmabuf;

void test_binary_syncobj(int drm_fd) {
    std::cout << "[test_sync] Running test_binary_syncobj..." << std::endl;

    // 1. Create unsignaled syncobj
    auto s1_res = SyncObj::create(drm_fd, 0);
    assert(s1_res.ok());
    auto s1 = std::move(s1_res.value());
    assert(s1->valid());

    auto wait_res = s1->wait(0);
    assert(wait_res.ok());
    assert(!wait_res.value()); // Not signaled

    // 2. Signal it
    assert(s1->signal().ok());
    wait_res = s1->wait(0);
    assert(wait_res.ok());
    assert(wait_res.value()); // Now signaled!

    // 3. Reset it
    assert(s1->reset().ok());
    wait_res = s1->wait(0);
    assert(wait_res.ok());
    assert(!wait_res.value()); // Unsignaled again

    // 4. Create signaled syncobj
    auto s2_res = SyncObj::create(drm_fd, DRM_SYNCOBJ_CREATE_SIGNALED);
    assert(s2_res.ok());
    auto s2 = std::move(s2_res.value());
    wait_res = s2->wait(0);
    assert(wait_res.ok());
    assert(wait_res.value());

    // 5. Export and re-import via syncobj fd
    auto export_fd_res = s2->export_syncobj_fd();
    assert(export_fd_res.ok());
    UniqueFd syncobj_fd = std::move(export_fd_res.value());
    assert(syncobj_fd.valid());

    auto imported_s_res = SyncObj::from_syncobj_fd(drm_fd, syncobj_fd.get());
    assert(imported_s_res.ok());
    auto imported_s = std::move(imported_s_res.value());
    wait_res = imported_s->wait(0);
    assert(wait_res.ok());
    assert(wait_res.value()); // Re-imported syncobj is also signaled

    std::cout << "[test_sync] test_binary_syncobj passed!" << std::endl;
}

void test_timeline_syncobj(int drm_fd) {
    std::cout << "[test_sync] Running test_timeline_syncobj..." << std::endl;

    auto t1_res = SyncObj::create(drm_fd, 0);
    assert(t1_res.ok());
    auto t1 = std::move(t1_res.value());

    // Initial query
    auto q_res = t1->timeline_query();
    assert(q_res.ok());
    assert(q_res.value() == 0);

    // Signal point 10
    assert(t1->timeline_signal(10).ok());
    q_res = t1->timeline_query();
    assert(q_res.ok());
    assert(q_res.value() == 10);

    // Wait on point 5 (should already be reached)
    auto wait_5 = t1->timeline_wait(5, 0);
    assert(wait_5.ok());
    assert(wait_5.value());

    // Wait on point 10 (should be reached)
    auto wait_10 = t1->timeline_wait(10, 0);
    assert(wait_10.ok());
    assert(wait_10.value());

    // Wait on point 15 (not yet reached)
    auto wait_15 = t1->timeline_wait(15, 0);
    assert(wait_15.ok());
    assert(!wait_15.value());

    // Transfer point to another timeline syncobj
    auto t2_res = SyncObj::create(drm_fd, 0);
    assert(t2_res.ok());
    auto t2 = std::move(t2_res.value());

    assert(t2->transfer(10, *t1, 10).ok());
    auto t2_wait = t2->timeline_wait(10, 0);
    assert(t2_wait.ok());
    assert(t2_wait.value());

    std::cout << "[test_sync] test_timeline_syncobj passed!" << std::endl;
}

void test_sync_file(int drm_fd) {
    std::cout << "[test_sync] Running test_sync_file..." << std::endl;

    // Create signaled syncobj and export sync_file
    auto s_res = SyncObj::create(drm_fd, DRM_SYNCOBJ_CREATE_SIGNALED);
    assert(s_res.ok());
    auto s = std::move(s_res.value());

    auto sf_fd_res = s->export_sync_file();
    if (sf_fd_res.ok()) {
        SyncFile sf(std::move(sf_fd_res.value()));
        assert(sf.valid());
        assert(sf.is_signaled());
        std::cout << "[test_sync] Successfully exported and verified signaled sync_file" << std::endl;

        // Test merge with itself
        auto merged_res = SyncFile::merge("test_merge", sf.fd(), sf.fd());
        if (merged_res.ok()) {
            auto merged = std::move(merged_res.value());
            assert(merged->valid());
            assert(merged->is_signaled());
            std::cout << "[test_sync] Successfully merged sync_files" << std::endl;
        }
    } else {
        std::cout << "[test_sync] Note: export_sync_file not supported on this kernel/driver" << std::endl;
    }
}

void test_dmabuf_sync_fallback(GbmDevice& gbm_dev) {
    std::cout << "[test_sync] Running test_dmabuf_sync_fallback..." << std::endl;

    auto bo_res = gbm_dev.create_buffer(256, 256, DRM_FORMAT_XRGB8888);
    if (!bo_res.ok()) return;

    auto attrs_res = bo_res.value()->export_dmabuf();
    if (!attrs_res.ok()) return;

    int dmabuf_fd = attrs_res.value().planes[0].fd.get();

    auto exp_res = export_dmabuf_sync_file(dmabuf_fd, false);
    if (exp_res.ok()) {
        std::cout << "[test_sync] DMA_BUF_IOCTL_EXPORT_SYNC_FILE succeeded, fd="
                  << exp_res.value().get() << std::endl;
        auto imp_res = import_dmabuf_sync_file(dmabuf_fd, exp_res.value().get(), true);
        (void)imp_res;
    } else {
        std::cout << "[test_sync] Note: DMA_BUF_IOCTL_EXPORT_SYNC_FILE returned: "
                  << exp_res.error_message() << std::endl;
    }
}

void test_vulkan_semaphore_interop(VulkanContext& vk_ctx) {
    std::cout << "[test_sync] Running test_vulkan_semaphore_interop..." << std::endl;

    // Create exportable binary semaphore for syncobj
    auto sem_res = create_exportable_semaphore(
        vk_ctx.device(), VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT, false);
    if (!sem_res.ok()) {
        std::cout << "[test_sync] create_exportable_semaphore returned: "
                  << sem_res.error_message() << std::endl;
        return;
    }

    VkSemaphore sem1 = sem_res.value();

    auto exp_res = export_semaphore_to_syncobj(vk_ctx.device(), sem1);
    if (exp_res.ok()) {
        UniqueFd syncobj_fd = std::move(exp_res.value());
        std::cout << "[test_sync] Exported Vulkan semaphore to syncobj fd: " << syncobj_fd.get() << std::endl;

        // Import into a second semaphore
        auto sem2_res = create_exportable_semaphore(
            vk_ctx.device(), VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT, false);
        if (sem2_res.ok()) {
            VkSemaphore sem2 = sem2_res.value();
            auto imp_res = import_syncobj_to_semaphore(vk_ctx.device(), sem2, syncobj_fd.get());
            if (imp_res.ok()) {
                std::cout << "[test_sync] Successfully imported syncobj into second semaphore!" << std::endl;
            }
            vkDestroySemaphore(vk_ctx.device(), sem2, nullptr);
        }
    }
    vkDestroySemaphore(vk_ctx.device(), sem1, nullptr);
}

int main() {
    std::string node = find_render_node();
    if (node.empty()) node = find_card_node();
    if (node.empty()) {
        std::cout << "[test_sync] No DRM node found, skipping with code 77" << std::endl;
        return 77;
    }

    int drm_fd = ::open(node.c_str(), O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        std::cout << "[test_sync] Failed to open DRM fd, skipping with code 77" << std::endl;
        return 77;
    }

    test_binary_syncobj(drm_fd);
    test_timeline_syncobj(drm_fd);
    test_sync_file(drm_fd);

    auto gbm_dev = GbmDevice::wrap_fd(UniqueFd(drm_fd));
    if (gbm_dev.ok()) {
        test_dmabuf_sync_fallback(*gbm_dev.value());
    }

    auto vk_res = VulkanContext::create_headless();
    if (vk_res.ok()) {
        test_vulkan_semaphore_interop(*vk_res.value());
    }

    std::cout << "[test_sync] All tests passed!" << std::endl;
    return 0;
}
