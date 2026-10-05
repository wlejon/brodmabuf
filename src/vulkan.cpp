#include "brodmabuf/vulkan.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>

namespace brodmabuf {

namespace {

constexpr VkImageAspectFlagBits kPlaneAspects[4] = {
    VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
    VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT,
    VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT,
    VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT,
};

bool check_instance_extension(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

bool check_device_extension(VkPhysicalDevice pd, const char* name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, exts.data());
    for (const auto& e : exts) {
        if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

}  // namespace

// -----------------------------------------------------------------------------
// VulkanImage
// -----------------------------------------------------------------------------

VulkanImage::~VulkanImage() noexcept {
    if (device_) {
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }
        for (VkDeviceMemory mem : memories_) {
            if (mem != VK_NULL_HANDLE) {
                vkFreeMemory(device_, mem, nullptr);
            }
        }
        memories_.clear();
    }
}

VulkanImage::VulkanImage(VulkanImage&& other) noexcept
    : device_(other.device_), image_(other.image_), memories_(std::move(other.memories_)),
      format_(other.format_), extent_(other.extent_), modifier_(other.modifier_) {
    other.device_ = VK_NULL_HANDLE;
    other.image_ = VK_NULL_HANDLE;
    other.format_ = VK_FORMAT_UNDEFINED;
    other.extent_ = {0, 0};
    other.modifier_ = DRM_FORMAT_MOD_INVALID;
}

VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept {
    if (this != &other) {
        if (device_) {
            if (image_ != VK_NULL_HANDLE) {
                vkDestroyImage(device_, image_, nullptr);
            }
            for (VkDeviceMemory mem : memories_) {
                if (mem != VK_NULL_HANDLE) {
                    vkFreeMemory(device_, mem, nullptr);
                }
            }
        }
        device_ = other.device_;
        image_ = other.image_;
        memories_ = std::move(other.memories_);
        format_ = other.format_;
        extent_ = other.extent_;
        modifier_ = other.modifier_;

        other.device_ = VK_NULL_HANDLE;
        other.image_ = VK_NULL_HANDLE;
        other.format_ = VK_FORMAT_UNDEFINED;
        other.extent_ = {0, 0};
        other.modifier_ = DRM_FORMAT_MOD_INVALID;
    }
    return *this;
}

// -----------------------------------------------------------------------------
// VulkanContext
// -----------------------------------------------------------------------------

VulkanContext::VulkanContext(
    VkInstance instance, VkPhysicalDevice physical_device,
    VkDevice device, VkQueue queue, uint32_t queue_family,
    bool owns_resources) noexcept
    : instance_(instance), physical_device_(physical_device),
      device_(device), queue_(queue), queue_family_(queue_family),
      owns_resources_(owns_resources) {
    init_proc_addrs();
}

VulkanContext::~VulkanContext() noexcept {
    if (owns_resources_) {
        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }
}

VulkanContext::VulkanContext(VulkanContext&& other) noexcept
    : instance_(other.instance_), physical_device_(other.physical_device_),
      device_(other.device_), queue_(other.queue_), queue_family_(other.queue_family_),
      owns_resources_(other.owns_resources_),
      pfn_get_image_drm_props_(other.pfn_get_image_drm_props_),
      pfn_get_memory_fd_props_(other.pfn_get_memory_fd_props_),
      pfn_get_memory_fd_(other.pfn_get_memory_fd_),
      pfn_get_physical_device_props2_(other.pfn_get_physical_device_props2_),
      pfn_get_physical_device_image_format_props2_(other.pfn_get_physical_device_image_format_props2_),
      pfn_get_physical_device_format_props2_(other.pfn_get_physical_device_format_props2_),
      pfn_get_image_memory_req2_(other.pfn_get_image_memory_req2_),
      pfn_bind_image_memory2_(other.pfn_bind_image_memory2_) {
    other.instance_ = VK_NULL_HANDLE;
    other.physical_device_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
    other.queue_ = VK_NULL_HANDLE;
    other.owns_resources_ = false;
}

VulkanContext& VulkanContext::operator=(VulkanContext&& other) noexcept {
    if (this != &other) {
        if (owns_resources_) {
            if (device_ != VK_NULL_HANDLE) {
                vkDestroyDevice(device_, nullptr);
            }
            if (instance_ != VK_NULL_HANDLE) {
                vkDestroyInstance(instance_, nullptr);
            }
        }
        instance_ = other.instance_;
        physical_device_ = other.physical_device_;
        device_ = other.device_;
        queue_ = other.queue_;
        queue_family_ = other.queue_family_;
        owns_resources_ = other.owns_resources_;

        pfn_get_image_drm_props_ = other.pfn_get_image_drm_props_;
        pfn_get_memory_fd_props_ = other.pfn_get_memory_fd_props_;
        pfn_get_memory_fd_ = other.pfn_get_memory_fd_;
        pfn_get_physical_device_props2_ = other.pfn_get_physical_device_props2_;
        pfn_get_physical_device_image_format_props2_ = other.pfn_get_physical_device_image_format_props2_;
        pfn_get_physical_device_format_props2_ = other.pfn_get_physical_device_format_props2_;
        pfn_get_image_memory_req2_ = other.pfn_get_image_memory_req2_;
        pfn_bind_image_memory2_ = other.pfn_bind_image_memory2_;

        other.instance_ = VK_NULL_HANDLE;
        other.physical_device_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
        other.queue_ = VK_NULL_HANDLE;
        other.owns_resources_ = false;
    }
    return *this;
}

void VulkanContext::init_proc_addrs() {
    if (instance_ != VK_NULL_HANDLE) {
        pfn_get_physical_device_props2_ = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceProperties2"));
        pfn_get_physical_device_format_props2_ = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceFormatProperties2"));
        pfn_get_physical_device_image_format_props2_ = reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
            vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceImageFormatProperties2"));
    }
    if (device_ != VK_NULL_HANDLE) {
        pfn_get_image_drm_props_ = reinterpret_cast<PFN_vkGetImageDrmFormatModifierPropertiesEXT>(
            vkGetDeviceProcAddr(device_, "vkGetImageDrmFormatModifierPropertiesEXT"));
        pfn_get_memory_fd_props_ = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
            vkGetDeviceProcAddr(device_, "vkGetMemoryFdPropertiesKHR"));
        pfn_get_memory_fd_ = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
            vkGetDeviceProcAddr(device_, "vkGetMemoryFdKHR"));
        pfn_get_image_memory_req2_ = reinterpret_cast<PFN_vkGetImageMemoryRequirements2>(
            vkGetDeviceProcAddr(device_, "vkGetImageMemoryRequirements2"));
        pfn_bind_image_memory2_ = reinterpret_cast<PFN_vkBindImageMemory2>(
            vkGetDeviceProcAddr(device_, "vkBindImageMemory2"));
    }
}

std::unique_ptr<VulkanContext> VulkanContext::wrap(
    VkInstance instance, VkPhysicalDevice physical_device,
    VkDevice device, VkQueue queue, uint32_t queue_family) noexcept {
    return std::make_unique<VulkanContext>(instance, physical_device, device, queue, queue_family, false);
}

Result<std::unique_ptr<VulkanContext>> VulkanContext::create_headless() {
    std::vector<const char*> instance_exts;
    if (check_instance_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
        instance_exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }
    if (check_instance_extension(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME)) {
        instance_exts.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    }
    if (check_instance_extension(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME)) {
        instance_exts.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "brodmabuf";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo inst_ci{};
    inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_ci.pApplicationInfo = &app_info;
    inst_ci.enabledExtensionCount = static_cast<uint32_t>(instance_exts.size());
    inst_ci.ppEnabledExtensionNames = instance_exts.data();

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = vkCreateInstance(&inst_ci, nullptr, &instance);
    if (res != VK_SUCCESS) {
        return Status::device_error("vkCreateInstance failed: " + std::to_string(res));
    }

    uint32_t pd_count = 0;
    vkEnumeratePhysicalDevices(instance, &pd_count, nullptr);
    if (pd_count == 0) {
        vkDestroyInstance(instance, nullptr);
        return Status::not_found("No Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> pds(pd_count);
    vkEnumeratePhysicalDevices(instance, &pd_count, pds.data());

    VkPhysicalDevice selected_pd = VK_NULL_HANDLE;
    for (VkPhysicalDevice pd : pds) {
        if (check_device_extension(pd, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME) &&
            check_device_extension(pd, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME) &&
            check_device_extension(pd, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME)) {
            selected_pd = pd;
            break;
        }
    }

    if (selected_pd == VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        return Status::unsupported("No Vulkan physical device supports required DMA-BUF and modifier extensions");
    }

    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(selected_pd, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qf_props(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(selected_pd, &qf_count, qf_props.data());

    uint32_t queue_family = UINT32_MAX;
    for (uint32_t i = 0; i < qf_count; ++i) {
        if (qf_props[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            queue_family = i;
            break;
        }
    }
    if (queue_family == UINT32_MAX) {
        queue_family = 0;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    std::vector<const char*> dev_exts = {
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
    };
    if (check_device_extension(selected_pd, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME)) {
        dev_exts.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
    }
    if (check_device_extension(selected_pd, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME)) {
        dev_exts.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    }
    if (check_device_extension(selected_pd, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME)) {
        dev_exts.push_back(VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
    }
    if (check_device_extension(selected_pd, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)) {
        dev_exts.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
    }

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(dev_exts.size());
    dci.ppEnabledExtensionNames = dev_exts.data();

    VkDevice device = VK_NULL_HANDLE;
    res = vkCreateDevice(selected_pd, &dci, nullptr, &device);
    if (res != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return Status::device_error("vkCreateDevice failed: " + std::to_string(res));
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    return std::make_unique<VulkanContext>(instance, selected_pd, device, queue, queue_family, true);
}

uint32_t VulkanContext::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if (type_filter & (1 << i)) {
            return i;
        }
    }
    return UINT32_MAX;
}

std::vector<DrmModifierInfo> VulkanContext::query_format_modifiers(VkFormat format) const {
    if (!pfn_get_physical_device_format_props2_) return {};

    VkDrmFormatModifierPropertiesListEXT list{};
    list.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;

    VkFormatProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    props.pNext = &list;
    pfn_get_physical_device_format_props2_(physical_device_, format, &props);

    if (list.drmFormatModifierCount == 0) return {};

    std::vector<VkDrmFormatModifierPropertiesEXT> mod_props(list.drmFormatModifierCount);
    list.pDrmFormatModifierProperties = mod_props.data();
    pfn_get_physical_device_format_props2_(physical_device_, format, &props);

    std::vector<DrmModifierInfo> results;
    results.reserve(list.drmFormatModifierCount);
    for (uint32_t i = 0; i < list.drmFormatModifierCount; ++i) {
        DrmModifierInfo info;
        info.modifier = mod_props[i].drmFormatModifier;
        info.plane_count = mod_props[i].drmFormatModifierPlaneCount;
        info.tiling_features = mod_props[i].drmFormatModifierTilingFeatures;
        results.push_back(info);
    }
    return results;
}

bool VulkanContext::is_import_supported(
    VkFormat format, uint64_t modifier, VkImageUsageFlags usage, VkImageCreateFlags flags) const {
    if (!pfn_get_physical_device_image_format_props2_) return false;

    VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod_info{};
    mod_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
    mod_info.drmFormatModifier = modifier;
    mod_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkPhysicalDeviceExternalImageFormatInfo ext_info{};
    ext_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    ext_info.pNext = &mod_info;
    ext_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkPhysicalDeviceImageFormatInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    info.pNext = &ext_info;
    info.format = format;
    info.type = VK_IMAGE_TYPE_2D;
    info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    info.usage = usage;
    info.flags = flags;

    VkExternalImageFormatProperties ext_props{};
    ext_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;

    VkImageFormatProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    props.pNext = &ext_props;

    if (pfn_get_physical_device_image_format_props2_(physical_device_, &info, &props) != VK_SUCCESS) {
        return false;
    }

    return (ext_props.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
}

Result<std::unique_ptr<VulkanImage>> VulkanContext::import_dmabuf(
    const DmaBufAttributes& attrs, VkImageUsageFlags usage) const {
    if (!attrs.is_valid()) {
        return Status::invalid_argument("Invalid DmaBufAttributes for Vulkan import");
    }
    if (attrs.modifier == DRM_FORMAT_MOD_INVALID) {
        return Status::invalid_argument("DMA-BUF import requires an explicit format modifier");
    }

    VkFormat format = drm_format_to_vk_format(attrs.drm_format);
    if (format == VK_FORMAT_UNDEFINED) {
        return Status::unsupported("No matching VkFormat for DRM format " + std::to_string(attrs.drm_format));
    }

    const size_t n_planes = attrs.planes.size();
    const bool disjoint = attrs.is_disjoint();

    VkSubresourceLayout layouts[4]{};
    for (size_t i = 0; i < n_planes; ++i) {
        layouts[i].offset = attrs.planes[i].offset;
        layouts[i].rowPitch = attrs.planes[i].stride;
    }

    VkImageDrmFormatModifierExplicitCreateInfoEXT mod_info{};
    mod_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    mod_info.drmFormatModifier = attrs.modifier;
    mod_info.drmFormatModifierPlaneCount = static_cast<uint32_t>(n_planes);
    mod_info.pPlaneLayouts = layouts;

    VkExternalMemoryImageCreateInfo ext_info{};
    ext_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    ext_info.pNext = &mod_info;
    ext_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.pNext = &ext_info;
    ici.flags = disjoint ? VK_IMAGE_CREATE_DISJOINT_BIT : 0;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {attrs.width, attrs.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    ici.usage = usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    VkResult res = vkCreateImage(device_, &ici, nullptr, &image);
    if (res != VK_SUCCESS) {
        return Status::device_error("vkCreateImage failed for DMA-BUF: " + std::to_string(res));
    }

    const size_t mem_planes = disjoint ? n_planes : 1;
    std::vector<VkDeviceMemory> memories(mem_planes, VK_NULL_HANDLE);

    auto cleanup = [&]() {
        for (VkDeviceMemory mem : memories) {
            if (mem != VK_NULL_HANDLE) vkFreeMemory(device_, mem, nullptr);
        }
        vkDestroyImage(device_, image, nullptr);
    };

    for (size_t i = 0; i < mem_planes; ++i) {
        int dup_fd = attrs.planes[i].fd.dup().release();
        if (dup_fd < 0) {
            cleanup();
            return Status::system_error("Failed to duplicate plane fd for Vulkan memory import");
        }

        VkMemoryFdPropertiesKHR fdp{};
        fdp.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
        if (!pfn_get_memory_fd_props_) {
            ::close(dup_fd);
            cleanup();
            return Status::unsupported("vkGetMemoryFdPropertiesKHR unavailable");
        }

        res = pfn_get_memory_fd_props_(device_, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dup_fd, &fdp);
        if (res != VK_SUCCESS) {
            ::close(dup_fd);
            cleanup();
            return Status::device_error("vkGetMemoryFdPropertiesKHR failed: " + std::to_string(res));
        }

        VkImagePlaneMemoryRequirementsInfo plane_req{};
        plane_req.sType = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO;
        plane_req.planeAspect = kPlaneAspects[i];

        VkImageMemoryRequirementsInfo2 req_info{};
        req_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
        req_info.pNext = disjoint ? &plane_req : nullptr;
        req_info.image = image;

        VkMemoryRequirements2 req{};
        req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        if (pfn_get_image_memory_req2_) {
            pfn_get_image_memory_req2_(device_, &req_info, &req);
        } else {
            vkGetImageMemoryRequirements(device_, image, &req.memoryRequirements);
        }

        uint32_t mem_type = find_memory_type(req.memoryRequirements.memoryTypeBits & fdp.memoryTypeBits, 0);
        if (mem_type == UINT32_MAX) {
            ::close(dup_fd);
            cleanup();
            return Status::device_error("No suitable memory type matches DMA-BUF requirements");
        }

        VkMemoryDedicatedAllocateInfo dedicated{};
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated.image = image;

        VkImportMemoryFdInfoKHR import_info{};
        import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
        import_info.pNext = disjoint ? nullptr : &dedicated;
        import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        import_info.fd = dup_fd;

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.pNext = &import_info;
        mai.allocationSize = (attrs.planes[i].size > 0) ? attrs.planes[i].size : req.memoryRequirements.size;
        mai.memoryTypeIndex = mem_type;

        res = vkAllocateMemory(device_, &mai, nullptr, &memories[i]);
        if (res != VK_SUCCESS) {
            ::close(dup_fd);
            cleanup();
            return Status::device_error("vkAllocateMemory failed for DMA-BUF plane: " + std::to_string(res));
        }
    }

    VkBindImageMemoryInfo binds[4]{};
    VkBindImagePlaneMemoryInfo plane_binds[4]{};
    for (size_t i = 0; i < mem_planes; ++i) {
        binds[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        binds[i].image = image;
        binds[i].memory = memories[i];
        binds[i].memoryOffset = 0;
        if (disjoint) {
            plane_binds[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
            plane_binds[i].planeAspect = kPlaneAspects[i];
            binds[i].pNext = &plane_binds[i];
        }
    }

    if (pfn_bind_image_memory2_) {
        res = pfn_bind_image_memory2_(device_, static_cast<uint32_t>(mem_planes), binds);
    } else {
        res = vkBindImageMemory(device_, image, memories[0], 0);
    }

    if (res != VK_SUCCESS) {
        cleanup();
        return Status::device_error("Failed to bind image memory: " + std::to_string(res));
    }

    return std::make_unique<VulkanImage>(
        device_, image, std::move(memories), format,
        VkExtent2D{attrs.width, attrs.height}, attrs.modifier);
}

Result<std::unique_ptr<VulkanImage>> VulkanContext::create_exportable_image(
    uint32_t width, uint32_t height, VkFormat format,
    const std::vector<uint64_t>& modifiers, VkImageUsageFlags usage) const {
    if (width == 0 || height == 0 || format == VK_FORMAT_UNDEFINED) {
        return Status::invalid_argument("Invalid image dimensions or format");
    }

    VkImageDrmFormatModifierListCreateInfoEXT mod_list{};
    mod_list.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
    mod_list.drmFormatModifierCount = static_cast<uint32_t>(modifiers.size());
    mod_list.pDrmFormatModifiers = modifiers.data();

    VkExternalMemoryImageCreateInfo ext_info{};
    ext_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    ext_info.pNext = modifiers.empty() ? nullptr : &mod_list;
    ext_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.pNext = &ext_info;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {width, height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = modifiers.empty() ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    ici.usage = usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    VkResult res = vkCreateImage(device_, &ici, nullptr, &image);
    if (res != VK_SUCCESS) {
        return Status::device_error("vkCreateImage failed for exportable image: " + std::to_string(res));
    }

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, image, &req);

    uint32_t mem_type = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        vkDestroyImage(device_, image, nullptr);
        return Status::device_error("No suitable memory type found for exportable image");
    }

    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.pNext = &export_info;
    dedicated.image = image;

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.pNext = &dedicated;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mem_type;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    res = vkAllocateMemory(device_, &mai, nullptr, &memory);
    if (res != VK_SUCCESS) {
        vkDestroyImage(device_, image, nullptr);
        return Status::device_error("vkAllocateMemory failed for exportable image: " + std::to_string(res));
    }

    res = vkBindImageMemory(device_, image, memory, 0);
    if (res != VK_SUCCESS) {
        vkFreeMemory(device_, memory, nullptr);
        vkDestroyImage(device_, image, nullptr);
        return Status::device_error("vkBindImageMemory failed for exportable image: " + std::to_string(res));
    }

    uint64_t actual_modifier = DRM_FORMAT_MOD_INVALID;
    if (pfn_get_image_drm_props_) {
        VkImageDrmFormatModifierPropertiesEXT props{};
        props.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT;
        if (pfn_get_image_drm_props_(device_, image, &props) == VK_SUCCESS) {
            actual_modifier = props.drmFormatModifier;
        }
    }

    std::vector<VkDeviceMemory> memories = {memory};
    return std::make_unique<VulkanImage>(
        device_, image, std::move(memories), format,
        VkExtent2D{width, height}, actual_modifier);
}

Result<DmaBufAttributes> VulkanContext::export_dmabuf(const VulkanImage& image) const {
    if (!pfn_get_memory_fd_) {
        return Status::unsupported("vkGetMemoryFdKHR unavailable");
    }

    uint64_t modifier = image.modifier();
    if (modifier == DRM_FORMAT_MOD_INVALID && pfn_get_image_drm_props_) {
        VkImageDrmFormatModifierPropertiesEXT props{};
        props.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT;
        if (pfn_get_image_drm_props_(device_, image.handle(), &props) == VK_SUCCESS) {
            modifier = props.drmFormatModifier;
        }
    }

    uint32_t drm_format = vk_format_to_drm_format(image.format());
    uint32_t plane_count = drm_format_plane_count(drm_format);
    if (plane_count == 0) plane_count = 1;

    std::vector<VkSubresourceLayout> layouts = query_subresource_layouts(image.handle(), plane_count);

    DmaBufAttributes attrs;
    attrs.width = image.width();
    attrs.height = image.height();
    attrs.drm_format = drm_format;
    attrs.modifier = modifier;

    for (size_t i = 0; i < plane_count; ++i) {
        size_t mem_idx = (image.memory_count() > i) ? i : 0;
        VkMemoryGetFdInfoKHR get_fd_info{};
        get_fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        get_fd_info.memory = image.memory(mem_idx);
        get_fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

        int fd = -1;
        VkResult res = pfn_get_memory_fd_(device_, &get_fd_info, &fd);
        if (res != VK_SUCCESS || fd < 0) {
            return Status::device_error("vkGetMemoryFdKHR failed: " + std::to_string(res));
        }

        uint64_t sz = 0;
        off_t end = ::lseek(fd, 0, SEEK_END);
        if (end > 0) {
            sz = static_cast<uint64_t>(end);
            ::lseek(fd, 0, SEEK_SET);
        }

        uint32_t stride = (i < layouts.size()) ? static_cast<uint32_t>(layouts[i].rowPitch) : 0;
        uint32_t offset = (i < layouts.size()) ? static_cast<uint32_t>(layouts[i].offset) : 0;
        attrs.planes.emplace_back(UniqueFd(fd), stride, offset, sz);
    }

    return attrs;
}

std::vector<VkSubresourceLayout> VulkanContext::query_subresource_layouts(
    VkImage image, uint32_t plane_count) const {
    return query_image_subresource_layouts(device_, image, plane_count);
}

// -----------------------------------------------------------------------------
// Standalone Functions
// -----------------------------------------------------------------------------

std::vector<DrmModifierInfo> query_drm_format_modifiers(
    VkInstance instance, VkPhysicalDevice physical_device, VkFormat format) {
    auto pfn = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties2>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFormatProperties2"));
    if (!pfn) return {};

    VkDrmFormatModifierPropertiesListEXT list{};
    list.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;

    VkFormatProperties2 props{};
    props.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    props.pNext = &list;
    pfn(physical_device, format, &props);

    if (list.drmFormatModifierCount == 0) return {};

    std::vector<VkDrmFormatModifierPropertiesEXT> mod_props(list.drmFormatModifierCount);
    list.pDrmFormatModifierProperties = mod_props.data();
    pfn(physical_device, format, &props);

    std::vector<DrmModifierInfo> results;
    results.reserve(list.drmFormatModifierCount);
    for (uint32_t i = 0; i < list.drmFormatModifierCount; ++i) {
        DrmModifierInfo info;
        info.modifier = mod_props[i].drmFormatModifier;
        info.plane_count = mod_props[i].drmFormatModifierPlaneCount;
        info.tiling_features = mod_props[i].drmFormatModifierTilingFeatures;
        results.push_back(info);
    }
    return results;
}

std::vector<VkSubresourceLayout> query_image_subresource_layouts(
    VkDevice device, VkImage image, uint32_t plane_count) {
    std::vector<VkSubresourceLayout> layouts(plane_count);
    for (uint32_t i = 0; i < plane_count; ++i) {
        VkImageSubresource subresource{};
        subresource.aspectMask = (plane_count > 1) ? kPlaneAspects[i] : VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = 0;
        subresource.arrayLayer = 0;
        vkGetImageSubresourceLayout(device, image, &subresource, &layouts[i]);
    }
    return layouts;
}

Result<std::unique_ptr<VulkanImage>> import_dmabuf_to_vulkan(
    VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
    const DmaBufAttributes& attrs, VkImageUsageFlags usage) {
    auto ctx = VulkanContext::wrap(instance, physical_device, device, VK_NULL_HANDLE, 0);
    return ctx->import_dmabuf(attrs, usage);
}

Result<DmaBufAttributes> export_vulkan_to_dmabuf(
    VkInstance instance, VkDevice device, const VulkanImage& image) {
    auto ctx = VulkanContext::wrap(instance, VK_NULL_HANDLE, device, VK_NULL_HANDLE, 0);
    return ctx->export_dmabuf(image);
}

}  // namespace brodmabuf
