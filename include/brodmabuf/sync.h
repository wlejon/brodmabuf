#pragma once

#if !defined(__linux__)
#error "brodmabuf/sync.h is Linux-only (drm_syncobj, sync_file). Off Linux use allocator.h, whose factories report why DMA-BUF is unavailable."
#endif

#include "brodmabuf/types.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace brodmabuf {

/// RAII wrapper for a DRM Synchronization Object (`drm_syncobj`).
/// Supports both binary and timeline synchronization models.
class SyncObj {
public:
    SyncObj(int drm_fd, uint32_t handle, bool owns_handle = true) noexcept
        : drm_fd_(drm_fd), handle_(handle), owns_handle_(owns_handle) {}
    ~SyncObj() noexcept;

    SyncObj(const SyncObj&) = delete;
    SyncObj& operator=(const SyncObj&) = delete;
    SyncObj(SyncObj&& other) noexcept;
    SyncObj& operator=(SyncObj&& other) noexcept;

    /// Create a new syncobj on the specified DRM device fd.
    /// Flags may include DRM_SYNCOBJ_CREATE_SIGNALED.
    [[nodiscard]] static Result<std::unique_ptr<SyncObj>> create(int drm_fd, uint32_t flags = 0);

    /// Import a syncobj from an opaque syncobj file descriptor.
    [[nodiscard]] static Result<std::unique_ptr<SyncObj>> from_syncobj_fd(int drm_fd, int syncobj_fd);

    /// Wrap an existing syncobj handle.
    [[nodiscard]] static std::unique_ptr<SyncObj> from_handle(int drm_fd, uint32_t handle, bool take_ownership = true);

    [[nodiscard]] int drm_fd() const noexcept { return drm_fd_; }
    [[nodiscard]] uint32_t handle() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept { return drm_fd_ >= 0 && handle_ != 0; }
    explicit operator bool() const noexcept { return valid(); }

    /// Export this syncobj as an opaque syncobj file descriptor.
    [[nodiscard]] Result<UniqueFd> export_syncobj_fd() const;

    /// Export the current fence of this syncobj as a `sync_file` file descriptor.
    [[nodiscard]] Result<UniqueFd> export_sync_file() const;

    /// Import a `sync_file` fence into this syncobj.
    [[nodiscard]] Result<void> import_sync_file(int sync_file_fd);

    /// Signal this binary syncobj.
    [[nodiscard]] Result<void> signal();

    /// Reset this binary syncobj to unsignaled state.
    [[nodiscard]] Result<void> reset();

    /// Wait for this binary syncobj to signal within timeout_nsec.
    /// Returns true if signaled, or false if timeout elapsed.
    [[nodiscard]] Result<bool> wait(uint64_t timeout_nsec = 0, uint32_t flags = 0);

    /// Signal a point on this timeline syncobj.
    [[nodiscard]] Result<void> timeline_signal(uint64_t point);

    /// Wait for a point on this timeline syncobj to signal within timeout_nsec.
    /// Returns true if signaled, or false if timeout elapsed.
    [[nodiscard]] Result<bool> timeline_wait(uint64_t point, uint64_t timeout_nsec = 0, uint32_t flags = 0);

    /// Query the highest currently signaled point on this timeline syncobj.
    [[nodiscard]] Result<uint64_t> timeline_query() const;

    /// Transfer a timeline point fence from src_syncobj into this syncobj.
    [[nodiscard]] Result<void> transfer(uint64_t point, const SyncObj& src_syncobj, uint64_t src_point);

private:
    int drm_fd_ = -1;
    uint32_t handle_ = 0;
    bool owns_handle_ = false;
};

/// RAII wrapper for a Linux `sync_file` (dma_fence) file descriptor.
class SyncFile {
public:
    explicit SyncFile(UniqueFd fd) noexcept : fd_(std::move(fd)) {}
    ~SyncFile() noexcept = default;

    SyncFile(const SyncFile&) = delete;
    SyncFile& operator=(const SyncFile&) = delete;
    SyncFile(SyncFile&&) noexcept = default;
    SyncFile& operator=(SyncFile&&) noexcept = default;

    /// Merge two sync_files into a new composite fence via SYNC_IOC_MERGE.
    [[nodiscard]] static Result<std::unique_ptr<SyncFile>> merge(const char* name, int fence1, int fence2);

    [[nodiscard]] int fd() const noexcept { return fd_.get(); }
    [[nodiscard]] bool valid() const noexcept { return fd_.valid(); }
    explicit operator bool() const noexcept { return valid(); }

    /// Duplicate the underlying file descriptor.
    [[nodiscard]] UniqueFd dup() const { return fd_.dup(); }

    /// Wait for the sync_file to signal. timeout_ms: <0 infinite, 0 non-blocking poll, >0 milliseconds.
    /// Returns true if signaled, false if timed out.
    [[nodiscard]] bool wait(int timeout_ms = -1) const;

    /// Non-blocking check whether the fence is signaled.
    [[nodiscard]] bool is_signaled() const { return wait(0); }

private:
    UniqueFd fd_;
};

// -----------------------------------------------------------------------------
// DMA-BUF Implicit Sync Helpers
// -----------------------------------------------------------------------------

/// Export current write (or read) fence of a DMA-BUF as a `sync_file` fd.
[[nodiscard]] Result<UniqueFd> export_dmabuf_sync_file(int dmabuf_fd, bool write_fence = false);

/// Import a `sync_file` fd into a DMA-BUF as its pending write (or read) fence.
[[nodiscard]] Result<void> import_dmabuf_sync_file(int dmabuf_fd, int sync_fd, bool write_fence = true);

// -----------------------------------------------------------------------------
// Vulkan External Semaphore Interop Helpers
// -----------------------------------------------------------------------------

/// Create a Vulkan semaphore exportable to external file descriptors.
/// For OPAQUE_FD (drm_syncobj), timeline can be true or false.
/// For SYNC_FD, timeline must be false.
[[nodiscard]] Result<VkSemaphore> create_exportable_semaphore(
    VkDevice device, VkExternalSemaphoreHandleTypeFlagBits handle_type,
    bool timeline = false, uint64_t initial_value = 0);

/// Export a Vulkan semaphore to a file descriptor.
[[nodiscard]] Result<UniqueFd> export_semaphore_fd(
    VkDevice device, VkSemaphore semaphore, VkExternalSemaphoreHandleTypeFlagBits handle_type);

/// Import an external file descriptor into a Vulkan semaphore.
[[nodiscard]] Result<void> import_semaphore_fd(
    VkDevice device, VkSemaphore semaphore, VkExternalSemaphoreHandleTypeFlagBits handle_type,
    int fd, bool temporary = false);

/// Convenience: Export semaphore to DRM syncobj fd (VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT).
[[nodiscard]] Result<UniqueFd> export_semaphore_to_syncobj(VkDevice device, VkSemaphore semaphore);

/// Convenience: Export semaphore to sync_file fd (VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT).
[[nodiscard]] Result<UniqueFd> export_semaphore_to_sync_file(VkDevice device, VkSemaphore semaphore);

/// Convenience: Import DRM syncobj fd into Vulkan semaphore.
[[nodiscard]] Result<void> import_syncobj_to_semaphore(VkDevice device, VkSemaphore semaphore, int syncobj_fd);

/// Convenience: Import sync_file fd into Vulkan semaphore.
[[nodiscard]] Result<void> import_sync_file_to_semaphore(VkDevice device, VkSemaphore semaphore, int sync_file_fd);

}  // namespace brodmabuf
