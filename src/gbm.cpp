#include "brodmabuf/gbm.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace brodmabuf {

std::string find_render_node() {
    char path[64];
    for (int i = 128; i < 192; ++i) {
        std::snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
        if (::access(path, R_OK | W_OK) == 0) {
            return std::string(path);
        }
    }
    return "";
}

std::string find_card_node() {
    char path[64];
    for (int i = 0; i < 64; ++i) {
        std::snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        if (::access(path, R_OK | W_OK) == 0) {
            return std::string(path);
        }
    }
    return "";
}

// -----------------------------------------------------------------------------
// GbmBuffer::Mapping
// -----------------------------------------------------------------------------

GbmBuffer::Mapping::~Mapping() noexcept {
    if (bo_ && map_data_) {
        gbm_bo_unmap(bo_, map_data_);
        bo_ = nullptr;
        map_data_ = nullptr;
        data_ = nullptr;
    }
}

GbmBuffer::Mapping::Mapping(GbmBuffer::Mapping&& other) noexcept
    : bo_(other.bo_), data_(other.data_), stride_(other.stride_), map_data_(other.map_data_) {
    other.bo_ = nullptr;
    other.data_ = nullptr;
    other.stride_ = 0;
    other.map_data_ = nullptr;
}

GbmBuffer::Mapping& GbmBuffer::Mapping::operator=(GbmBuffer::Mapping&& other) noexcept {
    if (this != &other) {
        if (bo_ && map_data_) {
            gbm_bo_unmap(bo_, map_data_);
        }
        bo_ = other.bo_;
        data_ = other.data_;
        stride_ = other.stride_;
        map_data_ = other.map_data_;
        other.bo_ = nullptr;
        other.data_ = nullptr;
        other.stride_ = 0;
        other.map_data_ = nullptr;
    }
    return *this;
}

// -----------------------------------------------------------------------------
// GbmBuffer
// -----------------------------------------------------------------------------

GbmBuffer::~GbmBuffer() noexcept {
    if (bo_) {
        gbm_bo_destroy(bo_);
        bo_ = nullptr;
    }
}

GbmBuffer::GbmBuffer(GbmBuffer&& other) noexcept : bo_(other.bo_) {
    other.bo_ = nullptr;
}

GbmBuffer& GbmBuffer::operator=(GbmBuffer&& other) noexcept {
    if (this != &other) {
        if (bo_) {
            gbm_bo_destroy(bo_);
        }
        bo_ = other.bo_;
        other.bo_ = nullptr;
    }
    return *this;
}

uint32_t GbmBuffer::width() const noexcept {
    return bo_ ? gbm_bo_get_width(bo_) : 0;
}

uint32_t GbmBuffer::height() const noexcept {
    return bo_ ? gbm_bo_get_height(bo_) : 0;
}

uint32_t GbmBuffer::format() const noexcept {
    return bo_ ? gbm_bo_get_format(bo_) : DRM_FORMAT_INVALID;
}

uint32_t GbmBuffer::stride() const noexcept {
    return bo_ ? gbm_bo_get_stride(bo_) : 0;
}

uint32_t GbmBuffer::stride(int plane) const noexcept {
    if (!bo_) return 0;
    uint32_t s = gbm_bo_get_stride_for_plane(bo_, plane);
    return (s > 0) ? s : gbm_bo_get_stride(bo_);
}

uint32_t GbmBuffer::offset(int plane) const noexcept {
    return bo_ ? gbm_bo_get_offset(bo_, plane) : 0;
}

uint64_t GbmBuffer::modifier() const noexcept {
    return bo_ ? gbm_bo_get_modifier(bo_) : DRM_FORMAT_MOD_INVALID;
}

int GbmBuffer::plane_count() const noexcept {
    if (!bo_) return 0;
    int count = gbm_bo_get_plane_count(bo_);
    return (count > 0) ? count : 1;
}

UniqueFd GbmBuffer::export_fd() const noexcept {
    if (!bo_) return UniqueFd();
    int fd = gbm_bo_get_fd(bo_);
    return UniqueFd(fd);
}

UniqueFd GbmBuffer::export_fd(int plane) const noexcept {
    if (!bo_) return UniqueFd();
    int fd = gbm_bo_get_fd_for_plane(bo_, plane);
    if (fd < 0 && plane == 0) {
        fd = gbm_bo_get_fd(bo_);
    }
    return UniqueFd(fd);
}

Result<DmaBufAttributes> GbmBuffer::export_dmabuf() const {
    if (!bo_) {
        return Status::invalid_argument("GbmBuffer is null");
    }

    DmaBufAttributes attrs;
    attrs.width = width();
    attrs.height = height();
    attrs.drm_format = format();
    attrs.modifier = modifier();

    int n_planes = plane_count();
    attrs.planes.reserve(n_planes);

    for (int i = 0; i < n_planes; ++i) {
        UniqueFd fd = export_fd(i);
        if (!fd.valid()) {
            if (i > 0 && !attrs.planes.empty() && attrs.planes[0].fd.valid()) {
                // Shared single-buffer multi-plane fallback: dup plane 0's fd
                fd = attrs.planes[0].fd.dup();
            } else {
                return Status::device_error("Failed to export fd for plane " + std::to_string(i));
            }
        }

        uint64_t sz = 0;
        off_t end = ::lseek(fd.get(), 0, SEEK_END);
        if (end > 0) {
            sz = static_cast<uint64_t>(end);
            ::lseek(fd.get(), 0, SEEK_SET);
        }

        uint32_t s = stride(i);
        uint32_t off = offset(i);
        attrs.planes.emplace_back(std::move(fd), s, off, sz);
    }

    return attrs;
}

std::unique_ptr<GbmBuffer::Mapping> GbmBuffer::map(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                                  uint32_t transfer_flags) {
    if (!bo_) return nullptr;
    uint32_t map_stride = 0;
    void* map_data = nullptr;
    void* ptr = gbm_bo_map(bo_, x, y, w, h, transfer_flags, &map_stride, &map_data);
    if (!ptr) {
        return nullptr;
    }
    return std::make_unique<Mapping>(bo_, ptr, map_stride, map_data);
}

bool GbmBuffer::write(const void* data, size_t count) noexcept {
    if (!bo_ || !data) return false;
    return gbm_bo_write(bo_, data, count) == 0;
}

// -----------------------------------------------------------------------------
// GbmDevice
// -----------------------------------------------------------------------------

GbmDevice::~GbmDevice() noexcept {
    if (device_) {
        gbm_device_destroy(device_);
        device_ = nullptr;
    }
}

GbmDevice::GbmDevice(GbmDevice&& other) noexcept
    : device_(other.device_), drm_fd_(std::move(other.drm_fd_)) {
    other.device_ = nullptr;
}

GbmDevice& GbmDevice::operator=(GbmDevice&& other) noexcept {
    if (this != &other) {
        if (device_) {
            gbm_device_destroy(device_);
        }
        device_ = other.device_;
        drm_fd_ = std::move(other.drm_fd_);
        other.device_ = nullptr;
    }
    return *this;
}

Result<std::unique_ptr<GbmDevice>> GbmDevice::open(const std::string& node) {
    std::string path = node;
    if (path.empty()) {
        path = find_render_node();
        if (path.empty()) {
            path = find_card_node();
        }
    }
    if (path.empty()) {
        return Status::not_found("No accessible DRM device node found");
    }

    int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return Status::system_error("Failed to open DRM device node " + path + ": " + std::strerror(errno));
    }

    return wrap_fd(UniqueFd(fd));
}

Result<std::unique_ptr<GbmDevice>> GbmDevice::wrap_fd(UniqueFd drm_fd) {
    if (!drm_fd.valid()) {
        return Status::invalid_argument("Invalid DRM file descriptor");
    }

    struct gbm_device* dev = gbm_create_device(drm_fd.get());
    if (!dev) {
        return Status::device_error("gbm_create_device failed");
    }

    return std::make_unique<GbmDevice>(dev, std::move(drm_fd));
}

Result<std::unique_ptr<GbmBuffer>> GbmDevice::create_buffer(
    uint32_t width, uint32_t height, uint32_t drm_format, uint32_t flags) {
    if (!device_) return Status::device_error("GBM device is null");

    struct gbm_bo* bo = gbm_bo_create(device_, width, height, drm_format, flags);
    if (!bo) {
        return Status::device_error("gbm_bo_create failed");
    }

    return std::make_unique<GbmBuffer>(bo);
}

Result<std::unique_ptr<GbmBuffer>> GbmDevice::create_buffer_with_modifiers(
    uint32_t width, uint32_t height, uint32_t drm_format,
    const std::vector<uint64_t>& modifiers, uint32_t flags) {
    if (!device_) return Status::device_error("GBM device is null");

    if (modifiers.empty()) {
        return create_buffer(width, height, drm_format, flags);
    }

    struct gbm_bo* bo = gbm_bo_create_with_modifiers2(
        device_, width, height, drm_format, modifiers.data(),
        static_cast<unsigned int>(modifiers.size()), flags);

    if (!bo && flags == 0) {
        bo = gbm_bo_create_with_modifiers(
            device_, width, height, drm_format, modifiers.data(),
            static_cast<unsigned int>(modifiers.size()));
    }

    if (!bo) {
        return Status::device_error("gbm_bo_create_with_modifiers2 failed");
    }

    return std::make_unique<GbmBuffer>(bo);
}

Result<std::unique_ptr<GbmBuffer>> GbmDevice::import_buffer(
    const DmaBufAttributes& attrs, uint32_t flags) {
    if (!device_) return Status::device_error("GBM device is null");
    if (!attrs.is_valid()) {
        return Status::invalid_argument("Invalid DmaBufAttributes provided for GBM import");
    }

    struct gbm_bo* bo = nullptr;

    if (attrs.modifier != DRM_FORMAT_MOD_INVALID) {
        struct gbm_import_fd_modifier_data mod_data{};
        mod_data.width = attrs.width;
        mod_data.height = attrs.height;
        mod_data.format = attrs.drm_format;
        mod_data.num_fds = static_cast<uint32_t>(attrs.planes.size());
        mod_data.modifier = attrs.modifier;

        for (size_t i = 0; i < attrs.planes.size() && i < GBM_MAX_PLANES; ++i) {
            mod_data.fds[i] = attrs.planes[i].fd.get();
            mod_data.strides[i] = static_cast<int>(attrs.planes[i].stride);
            mod_data.offsets[i] = static_cast<int>(attrs.planes[i].offset);
        }

        bo = gbm_bo_import(device_, GBM_BO_IMPORT_FD_MODIFIER, &mod_data, flags);
    } else if (attrs.planes.size() == 1) {
        struct gbm_import_fd_data fd_data{};
        fd_data.fd = attrs.planes[0].fd.get();
        fd_data.width = attrs.width;
        fd_data.height = attrs.height;
        fd_data.stride = attrs.planes[0].stride;
        fd_data.format = attrs.drm_format;

        bo = gbm_bo_import(device_, GBM_BO_IMPORT_FD, &fd_data, flags);
    }

    if (!bo) {
        return Status::device_error("gbm_bo_import failed");
    }

    return std::make_unique<GbmBuffer>(bo);
}

bool GbmDevice::is_format_supported(uint32_t drm_format, uint32_t flags) const noexcept {
    if (!device_) return false;
    return gbm_device_is_format_supported(device_, drm_format, flags) != 0;
}

}  // namespace brodmabuf
