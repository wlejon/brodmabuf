// Explicit and implicit synchronization on a real DRM device (Linux).
//
// Oracle: the kernel's own state. drm_syncobj signal/reset/wait and timeline
// points are read back through DRM_IOCTL_SYNCOBJ_WAIT / QUERY; sync_files are
// polled; a Vulkan semaphore signalled by a real queue submission is exported
// and must read as signalled through the kernel object it became.
//
// Not every DRM driver implements syncobjs (vkms does not); timeline points
// need DRM_CAP_SYNCOBJ_TIMELINE. Those parts skip and say so.
#include "check.h"
#include "drm_node.h"
#include "brodmabuf/gbm.h"
#include "brodmabuf/sync.h"
#include "brodmabuf/vulkan.h"

#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>

#include <chrono>
#include <string>

using namespace brodmabuf;

namespace {

void test_binary(int drm_fd) {
    auto s1_res = SyncObj::create(drm_fd, 0);
    REQUIRE_OK(s1_res);
    auto& s1 = s1_res.value();
    CHECK(s1->valid());
    CHECK(s1->handle() != 0u);

    auto w = s1->wait(0);
    REQUIRE_OK(w);
    CHECK(!w.value());

    // A relative timeout really waits: an unsignalled syncobj times out after
    // about that long, not immediately.
    auto t0 = std::chrono::steady_clock::now();
    w = s1->wait(50'000'000);
    auto waited = std::chrono::steady_clock::now() - t0;
    REQUIRE_OK(w);
    CHECK(!w.value());
    CHECK(waited >= std::chrono::milliseconds(40));

    CHECK_OK(s1->signal());
    w = s1->wait(0);
    REQUIRE_OK(w);
    CHECK(w.value());

    CHECK_OK(s1->reset());
    w = s1->wait(0);
    REQUIRE_OK(w);
    CHECK(!w.value());

    auto s2_res = SyncObj::create(drm_fd, DRM_SYNCOBJ_CREATE_SIGNALED);
    REQUIRE_OK(s2_res);
    auto& s2 = s2_res.value();
    w = s2->wait(0);
    REQUIRE_OK(w);
    CHECK(w.value());

    // Export as an opaque syncobj fd and import it back: the same kernel object.
    auto fd_res = s2->export_syncobj_fd();
    REQUIRE_OK(fd_res);
    auto imported = SyncObj::from_syncobj_fd(drm_fd, fd_res.value().get());
    REQUIRE_OK(imported);
    CHECK(imported.value()->handle() != s2->handle());
    w = imported.value()->wait(0);
    REQUIRE_OK(w);
    CHECK(w.value());
    CHECK_OK(imported.value()->reset());
    w = s2->wait(0);
    REQUIRE_OK(w);
    CHECK(!w.value());  // reset through the import is visible through the original

    // Wrapping without ownership leaves the handle alive.
    {
        auto view = SyncObj::from_handle(drm_fd, s2->handle(), false);
        CHECK(view->valid());
    }
    CHECK_OK(s2->signal());
}

void test_timeline(int drm_fd) {
    uint64_t cap = 0;
    if (drmGetCap(drm_fd, DRM_CAP_SYNCOBJ_TIMELINE, &cap) != 0 || cap == 0) {
        bstest::skip_check("timeline syncobj", "the driver does not report DRM_CAP_SYNCOBJ_TIMELINE");
        return;
    }
    auto t1_res = SyncObj::create(drm_fd, 0);
    REQUIRE_OK(t1_res);
    auto& t1 = t1_res.value();

    auto q = t1->timeline_query();
    REQUIRE_OK(q);
    CHECK_EQ(q.value(), uint64_t(0));

    CHECK_OK(t1->timeline_signal(10));
    q = t1->timeline_query();
    REQUIRE_OK(q);
    CHECK_EQ(q.value(), uint64_t(10));

    auto w = t1->timeline_wait(5, 0);
    REQUIRE_OK(w);
    CHECK(w.value());
    w = t1->timeline_wait(10, 0);
    REQUIRE_OK(w);
    CHECK(w.value());
    w = t1->timeline_wait(15, 0);
    REQUIRE_OK(w);
    CHECK(!w.value());

    auto t2_res = SyncObj::create(drm_fd, 0);
    REQUIRE_OK(t2_res);
    auto& t2 = t2_res.value();
    CHECK_OK(t2->transfer(3, *t1, 10));
    w = t2->timeline_wait(3, 0);
    REQUIRE_OK(w);
    CHECK(w.value());
    q = t2->timeline_query();
    REQUIRE_OK(q);
    CHECK_EQ(q.value(), uint64_t(3));
}

void test_sync_file(int drm_fd) {
    auto s_res = SyncObj::create(drm_fd, DRM_SYNCOBJ_CREATE_SIGNALED);
    REQUIRE_OK(s_res);
    auto sf_fd = s_res.value()->export_sync_file();
    REQUIRE_OK(sf_fd);
    SyncFile sf(std::move(sf_fd.value()));
    CHECK(sf.valid());
    CHECK(sf.is_signaled());
    CHECK(sf.wait(100));

    auto dup = sf.dup();
    CHECK(dup.valid());
    auto merged = SyncFile::merge("brodmabuf-test", sf.fd(), dup.get());
    REQUIRE_OK(merged);
    CHECK(merged.value()->is_signaled());

    // A sync_file imported into a fresh syncobj carries its (signalled) fence.
    auto target = SyncObj::create(drm_fd, 0);
    REQUIRE_OK(target);
    CHECK_OK(target.value()->import_sync_file(merged.value()->fd()));
    auto w = target.value()->wait(0);
    REQUIRE_OK(w);
    CHECK(w.value());

    // An unsignalled syncobj has no fence to export.
    auto empty = SyncObj::create(drm_fd, 0);
    REQUIRE_OK(empty);
    CHECK(!empty.value()->export_sync_file().ok());
}

void test_implicit_sync(GbmDevice& gbm) {
    auto bo = gbm.create_buffer(64, 64, DRM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    REQUIRE_OK(bo);
    auto attrs = bo.value()->export_dmabuf();
    REQUIRE_OK(attrs);
    const int dmabuf = attrs.value().planes[0].fd.get();

    auto exported = export_dmabuf_sync_file(dmabuf, false);
    if (!exported.ok()) {
        // DMA_BUF_IOCTL_EXPORT_SYNC_FILE arrived in Linux 6.0.
        bstest::skip_check("DMA_BUF_IOCTL_EXPORT_SYNC_FILE", std::string(exported.error_message()));
        return;
    }
    SyncFile fence(std::move(exported.value()));
    CHECK(fence.wait(1000));  // nothing is rendering into a fresh buffer
    CHECK_OK(import_dmabuf_sync_file(dmabuf, fence.fd(), true));
    auto again = export_dmabuf_sync_file(dmabuf, true);
    REQUIRE_OK(again);
    CHECK(SyncFile(std::move(again.value())).wait(1000));
}

// Signals `sem` from the GPU with an empty submission and waits for the queue.
bool submit_signal(VulkanContext& vk, VkSemaphore sem) {
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &sem;
    if (vkQueueSubmit(vk.queue(), 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) return false;
    return vkQueueWaitIdle(vk.queue()) == VK_SUCCESS;
}

// SYNC_FD: a signalled semaphore exports a sync_file that polls signalled.
// Needs no drm_syncobj, so it runs on any Vulkan driver (lavapipe included).
void test_vulkan_sync_fd(VulkanContext& vk) {
    auto sem = create_exportable_semaphore(vk.device(), VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT);
    if (!sem.ok()) {
        bstest::skip_check("Vulkan SYNC_FD semaphores", std::string(sem.error_message()));
    } else {
        REQUIRE(submit_signal(vk, sem.value()));
        auto fd = export_semaphore_to_sync_file(vk.device(), sem.value());
        if (fd.ok()) {
            // -1 is a valid export of an already-signalled payload.
            if (fd.value().valid()) CHECK(SyncFile(std::move(fd.value())).wait(1000));
        } else {
            bstest::skip_check("vkGetSemaphoreFdKHR(SYNC_FD)", std::string(fd.error_message()));
        }
        vkDestroySemaphore(vk.device(), sem.value(), nullptr);
    }
}

// OPAQUE_FD: on a DRM-backed Vulkan driver this is a drm_syncobj. Signalled
// on the GPU, it must read as signalled through the kernel object.
void test_vulkan_syncobj(VulkanContext& vk, int drm_fd) {
    auto opaque = create_exportable_semaphore(vk.device(), VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
    REQUIRE_OK(opaque);
    REQUIRE(submit_signal(vk, opaque.value()));
    auto syncobj_fd = export_semaphore_to_syncobj(vk.device(), opaque.value());
    REQUIRE_OK(syncobj_fd);
    auto as_syncobj = SyncObj::from_syncobj_fd(drm_fd, syncobj_fd.value().get());
    if (!as_syncobj.ok()) {
        bstest::skip_check("Vulkan OPAQUE_FD as drm_syncobj",
                           "this Vulkan driver's opaque fd is not a syncobj of " + bstest::drm_node() + ": " +
                               std::string(as_syncobj.error_message()));
    } else {
        auto w = as_syncobj.value()->wait(1'000'000'000);
        REQUIRE_OK(w);
        CHECK(w.value());

        // And back: a syncobj signalled by the kernel, imported into a fresh
        // semaphore, lets a GPU wait on it complete.
        auto kernel_signalled = SyncObj::create(drm_fd, DRM_SYNCOBJ_CREATE_SIGNALED);
        REQUIRE_OK(kernel_signalled);
        auto kfd = kernel_signalled.value()->export_syncobj_fd();
        REQUIRE_OK(kfd);
        auto sem2 = create_exportable_semaphore(vk.device(), VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
        REQUIRE_OK(sem2);
        CHECK_OK(import_syncobj_to_semaphore(vk.device(), sem2.value(), kfd.value().get()));
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &sem2.value();
        si.pWaitDstStageMask = &stage;
        CHECK_EQ(vkQueueSubmit(vk.queue(), 1, &si, VK_NULL_HANDLE), VK_SUCCESS);
        CHECK_EQ(vkQueueWaitIdle(vk.queue()), VK_SUCCESS);
        vkDestroySemaphore(vk.device(), sem2.value(), nullptr);
    }
    vkDestroySemaphore(vk.device(), opaque.value(), nullptr);
}

}  // namespace

int main() {
    const std::string node = bstest::drm_node();
    if (node.empty()) bstest::skip("test_sync", "no accessible DRM render or card node");
    std::printf("DRM node: %s\n", node.c_str());

    int drm_fd = ::open(node.c_str(), O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) bstest::skip("test_sync", "cannot open " + node);
    UniqueFd owned(drm_fd);

    uint64_t syncobj_cap = 0;
    const bool have_syncobj = drmGetCap(drm_fd, DRM_CAP_SYNCOBJ, &syncobj_cap) == 0 && syncobj_cap != 0;
    if (have_syncobj) {
        test_binary(drm_fd);
        test_timeline(drm_fd);
        test_sync_file(drm_fd);
    } else {
        bstest::skip_check("drm_syncobj", "the driver behind " + node + " does not implement DRM_CAP_SYNCOBJ");
    }

    auto gbm = GbmDevice::wrap_fd(owned.dup());
    if (gbm.ok()) {
        test_implicit_sync(*gbm.value());
    } else {
        bstest::skip_check("implicit sync", "GBM has no backend for " + node);
    }

    auto vk = VulkanContext::create_headless();
    if (!vk.ok()) {
        bstest::skip_check("Vulkan semaphore interop", std::string(vk.error_message()));
    } else {
        test_vulkan_sync_fd(*vk.value());
        if (have_syncobj) {
            test_vulkan_syncobj(*vk.value(), drm_fd);
        } else {
            bstest::skip_check("Vulkan semaphores as drm_syncobj", "needs drm_syncobj on " + node);
        }
    }

    if (!have_syncobj && !gbm.ok() && !vk.ok()) {
        bstest::skip("test_sync", "neither drm_syncobj, GBM nor Vulkan is available on " + node);
    }
    return bstest::finish("test_sync");
}
