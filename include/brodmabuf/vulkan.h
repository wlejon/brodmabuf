#pragma once

#include "brodmabuf/buffer.h"
#include "brodmabuf/formats.h"
#include "brodmabuf/types.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <vector>

namespace brodmabuf {

/// Properties of a DRM format modifier supported by Vulkan.
struct DrmModifierInfo {
    uint64_t modifier = DRM_FORMAT_MOD_INVALID;
    uint32_t plane_count = 1;
    VkFormatFeatureFlags tiling_features = 0;
};

/// RAII wrapper for an imported or exportable Vulkan image and its bound device memory.
class VulkanImage {
public:
    VulkanImage(VkDevice device, VkImage image, std::vector<VkDeviceMemory> memories,
                VkFormat format, VkExtent2D extent, uint64_t modifier) noexcept
        : device_(device), image_(image), memories_(std::move(memories)),
          format_(format), extent_(extent), modifier_(modifier) {}
    ~VulkanImage() noexcept;

    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;
    VulkanImage(VulkanImage&& other) noexcept;
    VulkanImage& operator=(VulkanImage&& other) noexcept;

    [[nodiscard]] VkImage handle() const noexcept { return image_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] uint32_t width() const noexcept { return extent_.width; }
    [[nodiscard]] uint32_t height() const noexcept { return extent_.height; }
    [[nodiscard]] uint64_t modifier() const noexcept { return modifier_; }

    [[nodiscard]] size_t memory_count() const noexcept { return memories_.size(); }
    [[nodiscard]] VkDeviceMemory memory(size_t index = 0) const noexcept {
        return index < memories_.size() ? memories_[index] : VK_NULL_HANDLE;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    std::vector<VkDeviceMemory> memories_;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{0, 0};
    uint64_t modifier_ = DRM_FORMAT_MOD_INVALID;
};

/// Helper managing a Vulkan instance and device initialized with external memory
/// and DRM format modifier extensions, or wrapping an existing external context.
class VulkanContext {
public:
    VulkanContext(VkInstance instance, VkPhysicalDevice physical_device,
                  VkDevice device, VkQueue queue, uint32_t queue_family,
                  bool owns_resources) noexcept;
    ~VulkanContext() noexcept;

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&& other) noexcept;
    VulkanContext& operator=(VulkanContext&& other) noexcept;

    /// Create a headless Vulkan context with required external memory and DRM modifier extensions enabled.
    [[nodiscard]] static Result<std::unique_ptr<VulkanContext>> create_headless();

    /// Wrap an existing Vulkan context without taking ownership of device or instance destruction.
    [[nodiscard]] static std::unique_ptr<VulkanContext> wrap(
        VkInstance instance, VkPhysicalDevice physical_device,
        VkDevice device, VkQueue queue, uint32_t queue_family) noexcept;

    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept { return physical_device_; }
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
    [[nodiscard]] uint32_t queue_family() const noexcept { return queue_family_; }

    /// Find suitable memory type index matching requirements and property flags.
    [[nodiscard]] uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    /// Query modifiers supported by this physical device for a given Vulkan format.
    [[nodiscard]] std::vector<DrmModifierInfo> query_format_modifiers(VkFormat format) const;

    /// Check if importing a DMA-BUF with the given parameters is supported.
    [[nodiscard]] bool is_import_supported(
        VkFormat format, uint64_t modifier, VkImageUsageFlags usage, VkImageCreateFlags flags = 0) const;

    /// Import a DMA-BUF into this Vulkan context as a VkImage.
    [[nodiscard]] Result<std::unique_ptr<VulkanImage>> import_dmabuf(
        const DmaBufAttributes& attrs,
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) const;

    /// Create an exportable VkImage backed by dedicated external memory and DRM format modifier.
    [[nodiscard]] Result<std::unique_ptr<VulkanImage>> create_exportable_image(
        uint32_t width, uint32_t height, VkFormat format,
        const std::vector<uint64_t>& modifiers,
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) const;

    /// Export an exportable VulkanImage to DMA-BUF attributes.
    [[nodiscard]] Result<DmaBufAttributes> export_dmabuf(const VulkanImage& image) const;

    /// Query subresource layouts for each plane of an image.
    [[nodiscard]] std::vector<VkSubresourceLayout> query_subresource_layouts(
        VkImage image, uint32_t plane_count) const;

private:
    void init_proc_addrs();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    bool owns_resources_ = false;

    PFN_vkGetImageDrmFormatModifierPropertiesEXT pfn_get_image_drm_props_ = nullptr;
    PFN_vkGetMemoryFdPropertiesKHR pfn_get_memory_fd_props_ = nullptr;
    PFN_vkGetMemoryFdKHR pfn_get_memory_fd_ = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 pfn_get_physical_device_props2_ = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 pfn_get_physical_device_image_format_props2_ = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties2 pfn_get_physical_device_format_props2_ = nullptr;
    PFN_vkGetImageMemoryRequirements2 pfn_get_image_memory_req2_ = nullptr;
    PFN_vkBindImageMemory2 pfn_bind_image_memory2_ = nullptr;
};

/// Standalone query: modifiers supported by a physical device for a given format.
std::vector<DrmModifierInfo> query_drm_format_modifiers(
    VkInstance instance, VkPhysicalDevice physical_device, VkFormat format);

/// Standalone import: import DMA-BUF attributes as a VulkanImage.
Result<std::unique_ptr<VulkanImage>> import_dmabuf_to_vulkan(
    VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
    const DmaBufAttributes& attrs, VkImageUsageFlags usage);

/// Standalone export: export a VulkanImage as DMA-BUF attributes.
Result<DmaBufAttributes> export_vulkan_to_dmabuf(
    VkInstance instance, VkDevice device, const VulkanImage& image);

/// Query plane layouts via vkGetImageSubresourceLayout.
std::vector<VkSubresourceLayout> query_image_subresource_layouts(
    VkDevice device, VkImage image, uint32_t plane_count);

}  // namespace brodmabuf
