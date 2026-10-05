#include "brodmabuf/allocator.h"
#include "brodmabuf/gbm.h"

#include <fcntl.h>
#include <unistd.h>

namespace brodmabuf {

namespace {

class GbmAllocator final : public DmaBufAllocator {
public:
    explicit GbmAllocator(std::unique_ptr<GbmDevice> device) noexcept
        : device_(std::move(device)) {}

    Result<DmaBufAttributes> allocate(
        uint32_t width, uint32_t height, uint32_t drm_format,
        const std::vector<uint64_t>& modifiers) override {
        auto buf_res = allocate_buffer(width, height, drm_format, modifiers);
        if (!buf_res.ok()) {
            return buf_res.status();
        }
        auto attrs_res = buf_res.value()->export_dmabuf();
        if (!attrs_res.ok()) {
            return attrs_res.status();
        }
        DmaBufAttributes attrs = std::move(attrs_res.value());
        if (attrs.modifier == DRM_FORMAT_MOD_INVALID) {
            attrs.modifier = DRM_FORMAT_MOD_LINEAR;
        }
        return attrs;
    }

    Result<std::unique_ptr<GbmBuffer>> allocate_buffer(
        uint32_t width, uint32_t height, uint32_t drm_format,
        const std::vector<uint64_t>& modifiers) override {
        if (!device_ || !device_->valid()) {
            return Status::device_error("GbmDevice is not initialized or invalid");
        }

        Result<std::unique_ptr<GbmBuffer>> bo_res(Status::device_error("Allocation failed"));

        if (!modifiers.empty()) {
            bo_res = device_->create_buffer_with_modifiers(width, height, drm_format, modifiers);
        } else {
            std::vector<uint64_t> default_mods = {DRM_FORMAT_MOD_LINEAR};
            bo_res = device_->create_buffer_with_modifiers(width, height, drm_format, default_mods);
        }

        if (!bo_res.ok()) {
            bo_res = device_->create_buffer(width, height, drm_format,
                                            GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
            if (!bo_res.ok()) {
                bo_res = device_->create_buffer(width, height, drm_format, GBM_BO_USE_RENDERING);
            }
        }

        return bo_res;
    }

    int drm_fd() const noexcept override {
        return device_ ? device_->drm_fd() : -1;
    }

    bool is_valid() const noexcept override {
        return device_ && device_->valid();
    }

private:
    std::unique_ptr<GbmDevice> device_;
};

}  // namespace

Result<std::unique_ptr<DmaBufAllocator>> DmaBufAllocator::create_default() {
    std::string node = find_render_node();
    if (node.empty()) {
        node = find_card_node();
    }
    if (node.empty()) {
        return Status::not_found("No accessible DRM render or card node found");
    }
    return create_gbm(node);
}

Result<std::unique_ptr<DmaBufAllocator>> DmaBufAllocator::create_gbm(const std::string& node) {
    auto dev_res = GbmDevice::open(node);
    if (!dev_res.ok()) {
        return dev_res.status();
    }
    std::unique_ptr<DmaBufAllocator> alloc = std::make_unique<GbmAllocator>(std::move(dev_res.value()));
    return alloc;
}

}  // namespace brodmabuf
