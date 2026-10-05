# brodmabuf

`brodmabuf` is a standalone, lightweight C++20 library for sharing GPU buffers and synchronizing rendering pipelines across Linux processes. It provides zero-overhead, RAII-safe abstractions for:

1. **DMA-BUF & DRM Format Model**: Comprehensive DRM format FourCC conversions, format modifier inspection (Intel, AMD, NVIDIA, ARM), and multi-planar DMA-BUF representation.
2. **GBM Buffer Allocation & Export**: Allocate buffer objects with explicit format modifiers (`gbm_bo_create_with_modifiers2`), export plane file descriptors, strides, offsets, and import DMA-BUFs into GBM.
3. **Vulkan External Memory Import & Export**: Import DMA-BUFs with explicit format modifiers into Vulkan (`VK_EXT_image_drm_format_modifier`, `VK_EXT_external_memory_dma_buf`, `VK_KHR_external_memory_fd`), query multi-planar subresource layouts, and export dedicated Vulkan images to DMA-BUFs.
4. **DRM Synchronization**: Complete explicit sync via `drm_syncobj` (both binary and timeline syncobjs), timeline queries and points, conversion to/from Linux `sync_file` (dma_fence), implicit sync fallback (`DMA_BUF_IOCTL_EXPORT_SYNC_FILE`), and Vulkan semaphore interop (`VK_KHR_external_semaphore_fd`).
5. **Direct KMS Scanout**: Direct kernel modesetting presentation, creating DRM framebuffers from DMA-BUFs (`drmModeAddFB2WithModifiers`), atomic commit building (`KmsAtomicReq`), in/out fence integration, and presentation loops.

---

## Architecture Overview

```
brodmabuf/
├── include/brodmabuf/
│   ├── types.h        # UniqueFd, Status, Result<T>, plane constants
│   ├── formats.h      # FourCC codes, DRM format modifiers, VkFormat mapping
│   ├── buffer.h       # DmaBufPlane, DmaBufAttributes, layout validation
│   ├── gbm.h          # GbmDevice, GbmBuffer, bo creation, mapping, export/import
│   ├── sync.h         # SyncObj (binary/timeline), SyncFile, Vulkan semaphore interop
│   ├── vulkan.h       # VulkanContext, VulkanImage, external memory import/export
│   └── kms.h          # KmsDevice, KmsFramebuffer, KmsAtomicReq, KmsPresenter
├── src/               # Implementations strictly decomposed (<1,000 lines each)
└── tests/             # Comprehensive ctest test suite
```

---

## Key Features & APIs

### 1. Buffer & Format Model (`formats.h`, `buffer.h`)
- FourCC translation and stringification (`drm_format_to_string`, `drm_format_from_string`).
- DRM Format Modifier inspection: vendor names (Intel, AMD, NVIDIA, Apple, ARM, etc.), human-readable modifier descriptions.
- Bidirectional `VkFormat` <-> DRM format mapping.
- `DmaBufAttributes`:
  - Per-plane `UniqueFd`, `stride`, `offset`, `size`.
  - Up to 4 planes (`kMaxPlanes = 4`).
  - `.is_valid()`, `.is_disjoint()`, `.dup()`, `.total_size()`.

### 2. GBM Buffer Allocation (`gbm.h`)
```cpp
#include <brodmabuf/gbm.h>

// Open render node (/dev/dri/renderD128)
auto dev = brodmabuf::GbmDevice::open().value();

// Allocate with explicit modifiers
std::vector<uint64_t> modifiers = {DRM_FORMAT_MOD_LINEAR};
auto bo = dev->create_buffer_with_modifiers(1920, 1080, DRM_FORMAT_ARGB8888, modifiers).value();

// Export DMA-BUF attributes
brodmabuf::DmaBufAttributes attrs = bo->export_dmabuf().value();
```

### 3. Vulkan DMA-BUF Import & Export (`vulkan.h`)
```cpp
#include <brodmabuf/vulkan.h>

// Initialize headless Vulkan context with required extensions
auto vk_ctx = brodmabuf::VulkanContext::create_headless().value();

// Import DMA-BUF as VkImage
auto vk_img = vk_ctx->import_dmabuf(attrs).value();

// Query plane layouts
auto layouts = vk_ctx->query_subresource_layouts(vk_img->handle(), attrs.plane_count());
```

### 4. Synchronization (`sync.h`)
```cpp
#include <brodmabuf/sync.h>

// Create DRM syncobj
auto syncobj = brodmabuf::SyncObj::create(dev->drm_fd()).value();

// Timeline syncobj
syncobj->timeline_signal(42);
syncobj->timeline_wait(42);

// Export to sync_file (dma_fence)
brodmabuf::UniqueFd fence_fd = syncobj->export_sync_file().value();

// Vulkan semaphore interop
auto sem = brodmabuf::create_exportable_semaphore(
    vk_ctx->device(), VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT).value();
brodmabuf::UniqueFd sem_syncobj_fd = brodmabuf::export_semaphore_to_syncobj(vk_ctx->device(), sem).value();
```

### 5. KMS Direct Scanout (`kms.h`)
```cpp
#include <brodmabuf/kms.h>

// Open KMS card node (/dev/dri/card0)
auto kms_dev = brodmabuf::KmsDevice::open().value();

// Create DRM Framebuffer from DMA-BUF
auto fb = brodmabuf::KmsFramebuffer::create_from_dmabuf(kms_dev->fd(), attrs).value();

// Atomic modesetting commit with explicit sync in/out fences
brodmabuf::KmsAtomicReq req;
req.set_plane(pipeline.plane_props, pipeline.plane_id, pipeline.crtc_id, fb->fb_id(),
              0, 0, 1920, 1080, 0, 0, 1920, 1080);
req.set_in_fence(pipeline.plane_props, pipeline.plane_id, in_fence.get());
req.commit(kms_dev->fd(), DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT);
```

---

## Building and Testing

### Prerequisites
- C++20 compliant compiler (GCC 12+, Clang 15+)
- CMake 3.24+
- `libdrm` (2.4.100+)
- `gbm` (Mesa)
- `vulkan` loader and headers

### Build
```bash
cmake -B build -S .
cmake --build build -j 2
```

### Run Tests
```bash
ctest --test-dir build --output-on-failure
```
Tests automatically skip with return code 77 when specific hardware features (e.g. connected KMS display or DRM master) are not present in headless environments.
