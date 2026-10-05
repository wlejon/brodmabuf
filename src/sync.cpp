#include "brodmabuf/sync.h"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xf86drm.h>

#include <cerrno>
#include <cstring>

namespace brodmabuf {

// -----------------------------------------------------------------------------
// SyncObj
// -----------------------------------------------------------------------------

SyncObj::~SyncObj() noexcept {
    if (owns_handle_ && drm_fd_ >= 0 && handle_ != 0) {
        drmSyncobjDestroy(drm_fd_, handle_);
        handle_ = 0;
    }
}

SyncObj::SyncObj(SyncObj&& other) noexcept
    : drm_fd_(other.drm_fd_), handle_(other.handle_), owns_handle_(other.owns_handle_) {
    other.drm_fd_ = -1;
    other.handle_ = 0;
    other.owns_handle_ = false;
}

SyncObj& SyncObj::operator=(SyncObj&& other) noexcept {
    if (this != &other) {
        if (owns_handle_ && drm_fd_ >= 0 && handle_ != 0) {
            drmSyncobjDestroy(drm_fd_, handle_);
        }
        drm_fd_ = other.drm_fd_;
        handle_ = other.handle_;
        owns_handle_ = other.owns_handle_;
        other.drm_fd_ = -1;
        other.handle_ = 0;
        other.owns_handle_ = false;
    }
    return *this;
}

Result<std::unique_ptr<SyncObj>> SyncObj::create(int drm_fd, uint32_t flags) {
    if (drm_fd < 0) {
        return Status::invalid_argument("Invalid DRM fd");
    }
    uint32_t handle = 0;
    int ret = drmSyncobjCreate(drm_fd, flags, &handle);
    if (ret != 0 || handle == 0) {
        return Status::system_error("drmSyncobjCreate failed: " + std::string(std::strerror(errno)));
    }
    return std::make_unique<SyncObj>(drm_fd, handle, true);
}

Result<std::unique_ptr<SyncObj>> SyncObj::from_syncobj_fd(int drm_fd, int syncobj_fd) {
    if (drm_fd < 0 || syncobj_fd < 0) {
        return Status::invalid_argument("Invalid fd");
    }
    uint32_t handle = 0;
    int ret = drmSyncobjFDToHandle(drm_fd, syncobj_fd, &handle);
    if (ret != 0 || handle == 0) {
        return Status::system_error("drmSyncobjFDToHandle failed: " + std::string(std::strerror(errno)));
    }
    return std::make_unique<SyncObj>(drm_fd, handle, true);
}

std::unique_ptr<SyncObj> SyncObj::from_handle(int drm_fd, uint32_t handle, bool take_ownership) {
    return std::make_unique<SyncObj>(drm_fd, handle, take_ownership);
}

Result<UniqueFd> SyncObj::export_syncobj_fd() const {
    if (!valid()) return Status::invalid_argument("Invalid syncobj");
    int fd = -1;
    int ret = drmSyncobjHandleToFD(drm_fd_, handle_, &fd);
    if (ret != 0 || fd < 0) {
        return Status::system_error("drmSyncobjHandleToFD failed: " + std::string(std::strerror(errno)));
    }
    return UniqueFd(fd);
}

Result<UniqueFd> SyncObj::export_sync_file() const {
    if (!valid()) return Status::invalid_argument("Invalid syncobj");
    int sync_file_fd = -1;
    int ret = drmSyncobjExportSyncFile(drm_fd_, handle_, &sync_file_fd);
    if (ret != 0 || sync_file_fd < 0) {
        return Status::system_error("drmSyncobjExportSyncFile failed: " + std::string(std::strerror(errno)));
    }
    return UniqueFd(sync_file_fd);
}

Result<void> SyncObj::import_sync_file(int sync_file_fd) {
    if (!valid() || sync_file_fd < 0) return Status::invalid_argument("Invalid arguments");
    int ret = drmSyncobjImportSyncFile(drm_fd_, handle_, sync_file_fd);
    if (ret != 0) {
        return Status::system_error("drmSyncobjImportSyncFile failed: " + std::string(std::strerror(errno)));
    }
    return Status::ok();
}

Result<void> SyncObj::signal() {
    if (!valid()) return Status::invalid_argument("Invalid syncobj");
    int ret = drmSyncobjSignal(drm_fd_, &handle_, 1);
    if (ret != 0) {
        return Status::system_error("drmSyncobjSignal failed: " + std::string(std::strerror(errno)));
    }
    return Status::ok();
}

Result<void> SyncObj::reset() {
    if (!valid()) return Status::invalid_argument("Invalid syncobj");
    int ret = drmSyncobjReset(drm_fd_, &handle_, 1);
    if (ret != 0) {
        return Status::system_error("drmSyncobjReset failed: " + std::string(std::strerror(errno)));
    }
    return Status::ok();
}

Result<bool> SyncObj::wait(uint64_t timeout_nsec, uint32_t flags) {
    if (!valid()) return Status::invalid_argument("Invalid syncobj");
    uint32_t first_signaled = 0;
    if (flags == 0) {
        flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
    }
    int ret = drmSyncobjWait(drm_fd_, &handle_, 1, timeout_nsec, flags, &first_signaled);
    if (ret == 0) {
        return true;
    }
    if (ret == -ETIME || errno == ETIME || ret == -EBUSY || errno == EBUSY || ret == -EINVAL) {
        return false;
    }
    return Status::system_error("drmSyncobjWait failed: " + std::string(std::strerror(errno)));
}

Result<void> SyncObj::timeline_signal(uint64_t point) {
    if (!valid()) return Status::invalid_argument("Invalid syncobj");
    int ret = drmSyncobjTimelineSignal(drm_fd_, &handle_, &point, 1);
    if (ret != 0) {
        return Status::system_error("drmSyncobjTimelineSignal failed: " + std::string(std::strerror(errno)));
    }
    return Status::ok();
}

Result<bool> SyncObj::timeline_wait(uint64_t point, uint64_t timeout_nsec, uint32_t flags) {
    if (!valid()) return Status::invalid_argument("Invalid syncobj");
    uint32_t first_signaled = 0;
    if (flags == 0) {
        flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
    }
    int ret = drmSyncobjTimelineWait(drm_fd_, &handle_, &point, 1, timeout_nsec, flags, &first_signaled);
    if (ret == 0) {
        return true;
    }
    if (ret == -ETIME || errno == ETIME || ret == -EBUSY || errno == EBUSY || ret == -EINVAL) {
        return false;
    }
    return Status::system_error("drmSyncobjTimelineWait failed: " + std::string(std::strerror(errno)));
}

Result<uint64_t> SyncObj::timeline_query() const {
    if (!valid()) return Status::invalid_argument("Invalid syncobj");
    uint64_t point = 0;
    uint32_t h = handle_;
    int ret = drmSyncobjQuery(drm_fd_, &h, &point, 1);
    if (ret != 0) {
        return Status::system_error("drmSyncobjQuery failed: " + std::string(std::strerror(errno)));
    }
    return point;
}

Result<void> SyncObj::transfer(uint64_t point, const SyncObj& src_syncobj, uint64_t src_point) {
    if (!valid() || !src_syncobj.valid()) return Status::invalid_argument("Invalid syncobj");
    int ret = drmSyncobjTransfer(drm_fd_, handle_, point, src_syncobj.handle(), src_point, 0);
    if (ret != 0) {
        return Status::system_error("drmSyncobjTransfer failed: " + std::string(std::strerror(errno)));
    }
    return Status::ok();
}

// -----------------------------------------------------------------------------
// SyncFile
// -----------------------------------------------------------------------------

Result<std::unique_ptr<SyncFile>> SyncFile::merge(const char* name, int fence1, int fence2) {
    if (fence1 < 0 || fence2 < 0) {
        return Status::invalid_argument("Invalid fence file descriptors");
    }

    struct sync_merge_data data{};
    if (name) {
        std::strncpy(data.name, name, sizeof(data.name) - 1);
    }
    data.fence = fence1;
    data.fd2 = fence2;

    int ret = ::ioctl(fence1, SYNC_IOC_MERGE, &data);
    if (ret != 0 || data.fence < 0) {
        return Status::system_error("SYNC_IOC_MERGE failed: " + std::string(std::strerror(errno)));
    }

    return std::make_unique<SyncFile>(UniqueFd(data.fence));
}

bool SyncFile::wait(int timeout_ms) const {
    if (!fd_.valid()) return false;
    struct pollfd pfd{};
    pfd.fd = fd_.get();
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, timeout_ms);
    return (ret > 0 && (pfd.revents & POLLIN) != 0);
}

// -----------------------------------------------------------------------------
// DMA-BUF Implicit Sync
// -----------------------------------------------------------------------------

Result<UniqueFd> export_dmabuf_sync_file(int dmabuf_fd, bool write_fence) {
    if (dmabuf_fd < 0) return Status::invalid_argument("Invalid dmabuf fd");

    struct dma_buf_export_sync_file req{};
    req.flags = write_fence ? DMA_BUF_SYNC_WRITE : DMA_BUF_SYNC_READ;
    req.fd = -1;

    int ret = ::ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &req);
    if (ret != 0 || req.fd < 0) {
        return Status::system_error("DMA_BUF_IOCTL_EXPORT_SYNC_FILE failed: " + std::string(std::strerror(errno)));
    }

    return UniqueFd(req.fd);
}

Result<void> import_dmabuf_sync_file(int dmabuf_fd, int sync_fd, bool write_fence) {
    if (dmabuf_fd < 0 || sync_fd < 0) return Status::invalid_argument("Invalid fd");

    struct dma_buf_import_sync_file req{};
    req.flags = write_fence ? DMA_BUF_SYNC_WRITE : DMA_BUF_SYNC_READ;
    req.fd = sync_fd;

    int ret = ::ioctl(dmabuf_fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &req);
    if (ret != 0) {
        return Status::system_error("DMA_BUF_IOCTL_IMPORT_SYNC_FILE failed: " + std::string(std::strerror(errno)));
    }

    return Status::ok();
}

// -----------------------------------------------------------------------------
// Vulkan Semaphore Interop
// -----------------------------------------------------------------------------

Result<VkSemaphore> create_exportable_semaphore(
    VkDevice device, VkExternalSemaphoreHandleTypeFlagBits handle_type,
    bool timeline, uint64_t initial_value) {
    if (!device) return Status::invalid_argument("Null VkDevice");

    VkExportSemaphoreCreateInfo exp_info{};
    exp_info.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exp_info.handleTypes = handle_type;

    VkSemaphoreTypeCreateInfo timeline_info{};
    if (timeline) {
        timeline_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timeline_info.pNext = &exp_info;
        timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_info.initialValue = initial_value;
    }

    VkSemaphoreCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    create_info.pNext = timeline ? static_cast<const void*>(&timeline_info) : static_cast<const void*>(&exp_info);

    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkResult res = vkCreateSemaphore(device, &create_info, nullptr, &semaphore);
    if (res != VK_SUCCESS) {
        return Status::device_error("vkCreateSemaphore failed: " + std::to_string(res));
    }

    return semaphore;
}

Result<UniqueFd> export_semaphore_fd(
    VkDevice device, VkSemaphore semaphore, VkExternalSemaphoreHandleTypeFlagBits handle_type) {
    if (!device || semaphore == VK_NULL_HANDLE) {
        return Status::invalid_argument("Invalid device or semaphore handle");
    }

    auto pfn_get = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
        vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));
    if (!pfn_get) {
        return Status::unsupported("vkGetSemaphoreFdKHR not available on device");
    }

    VkSemaphoreGetFdInfoKHR get_info{};
    get_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR;
    get_info.semaphore = semaphore;
    get_info.handleType = handle_type;

    int fd = -1;
    VkResult res = pfn_get(device, &get_info, &fd);
    if (res != VK_SUCCESS || fd < 0) {
        return Status::device_error("vkGetSemaphoreFdKHR failed: " + std::to_string(res));
    }

    return UniqueFd(fd);
}

Result<void> import_semaphore_fd(
    VkDevice device, VkSemaphore semaphore, VkExternalSemaphoreHandleTypeFlagBits handle_type,
    int fd, bool temporary) {
    if (!device || semaphore == VK_NULL_HANDLE || fd < 0) {
        return Status::invalid_argument("Invalid device, semaphore, or fd");
    }

    auto pfn_import = reinterpret_cast<PFN_vkImportSemaphoreFdKHR>(
        vkGetDeviceProcAddr(device, "vkImportSemaphoreFdKHR"));
    if (!pfn_import) {
        return Status::unsupported("vkImportSemaphoreFdKHR not available on device");
    }

    // vkImportSemaphoreFdKHR takes ownership of the fd on success, so import a dup
    int dup_fd = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (dup_fd < 0) {
        return Status::system_error("Failed to duplicate fd for semaphore import");
    }

    VkImportSemaphoreFdInfoKHR import_info{};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
    import_info.semaphore = semaphore;
    import_info.handleType = handle_type;
    import_info.fd = dup_fd;
    import_info.flags = temporary ? VK_SEMAPHORE_IMPORT_TEMPORARY_BIT : 0;

    VkResult res = pfn_import(device, &import_info);
    if (res != VK_SUCCESS) {
        ::close(dup_fd);
        return Status::device_error("vkImportSemaphoreFdKHR failed: " + std::to_string(res));
    }

    return Status::ok();
}

Result<UniqueFd> export_semaphore_to_syncobj(VkDevice device, VkSemaphore semaphore) {
    return export_semaphore_fd(device, semaphore, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
}

Result<UniqueFd> export_semaphore_to_sync_file(VkDevice device, VkSemaphore semaphore) {
    return export_semaphore_fd(device, semaphore, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT);
}

Result<void> import_syncobj_to_semaphore(VkDevice device, VkSemaphore semaphore, int syncobj_fd) {
    return import_semaphore_fd(device, semaphore, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT, syncobj_fd, false);
}

Result<void> import_sync_file_to_semaphore(VkDevice device, VkSemaphore semaphore, int sync_file_fd) {
    return import_semaphore_fd(device, semaphore, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT, sync_file_fd, true);
}

}  // namespace brodmabuf
