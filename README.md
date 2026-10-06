# brodmabuf

[![CI](https://github.com/wlejon/brodmabuf/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/brodmabuf/actions/workflows/ci.yml)
[![CodeQL](https://github.com/wlejon/brodmabuf/actions/workflows/codeql.yml/badge.svg)](https://github.com/wlejon/brodmabuf/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A standalone C++20 library for sharing GPU buffers between processes and graphics
APIs on Linux: DMA-BUF descriptions, GBM allocation, Vulkan import and export,
explicit and implicit synchronization (`drm_syncobj`, `sync_file`), and KMS scanout
of DMA-BUFs. Every resource handle is RAII, and every fallible operation returns a
`Result<T>` carrying the detailed error reason upon failure.

Part of the **[bro ecosystem](https://github.com/wlejon/bro/blob/main/docs/ecosystem.md)**,
built in the mould of [brodisplays](https://github.com/wlejon/brodisplays) and
[brocompositor](https://github.com/wlejon/brocompositor).

## Platform Support

Stated honestly, what was verified where:

DMA-BUF, GBM, DRM syncobjs, and KMS are Linux kernel interfaces, so the library
executes its GPU and kernel operations **on Linux only**. On Windows and macOS,
it still configures and builds with zero Linux dependencies, reporting unsupported
operations honestly:

| Feature / Header | Linux | Windows & macOS |
| :--- | :--- | :--- |
| `types.h` (`UniqueFd`, `Status`, `Result<T>`) | Supported | Supported |
| `formats.h` (FourCC codes, modifiers, format properties) | Supported (with `<drm_fourcc.h>`) | Supported (with bundled `drm_fourcc_compat.h`, checked against kernel headers) |
| `formats.h` (`VkFormat` mappings) | Supported | Not compiled |
| `buffer.h` (`DmaBufAttributes`, plane layout calculations) | Supported | Supported |
| `allocator.h` (`DmaBufAllocator`) | Supported | `create_default()` / `create_gbm()` fail with `Status::Unsupported`; `platform_status()` reports unsupported up front |
| `gbm.h`, `vulkan.h`, `sync.h`, `kms.h` | Supported | `#error` (Linux-only headers) |

### Verified Configurations

- **Linux**: Debian trixie (Mesa 25, vkms, lavapipe), Ubuntu 24.04 (GCC 14, Clang 18), Arch Linux (Linux kernel 7.2, amdgpu + RADV).
- **Windows**: Windows Server 2022 (x64), MSVC 2022 (v143), Release and Debug CRT (portable models).
- **macOS**: macOS 15 Sequoia (arm64), Apple Clang 16 (portable models).

## API Overview

```
include/brodmabuf/
  types.h       UniqueFd, Status, Result<T>, kMaxPlanes
  formats.h     FourCC <-> string, modifier vendor/description, format properties,
                DRM <-> VkFormat mappings (Linux)
  buffer.h      DmaBufPlane, DmaBufAttributes (validation, dup, total size),
                plane layout calculations (subsampling, minimum strides)
  allocator.h   DmaBufAllocator: allocate a DMA-BUF with GBM; platform_status()
  gbm.h         GbmDevice, GbmBuffer: create (with modifiers), map, export, import
  vulkan.h      VulkanContext: import DMA-BUF as VkImage, query modifiers and
                plane layouts, create and export images
  sync.h        SyncObj (binary + timeline), SyncFile, DMA-BUF implicit sync,
                Vulkan semaphore <-> syncobj / sync_file
  kms.h         KmsDevice, KmsFramebuffer (AddFB2 from DMA-BUF), KmsAtomicReq,
                KmsPresenter (modeset, page flips with in/out fences)
```

### Usage Examples

#### 1. Allocate with GBM and Import into Vulkan

```cpp
#include <brodmabuf/gbm.h>
#include <brodmabuf/vulkan.h>

// Open first available DRM render node (/dev/dri/renderD128)
auto gbm = brodmabuf::GbmDevice::open();
if (!gbm.ok()) return fail(gbm.error_message());

auto bo = gbm.value()->create_buffer_with_modifiers(
    1920, 1080, DRM_FORMAT_ARGB8888, {DRM_FORMAT_MOD_LINEAR});
auto attrs = bo.value()->export_dmabuf(); // fds, strides, offsets, modifier

// Import DMA-BUF into headless Vulkan context
auto vk = brodmabuf::VulkanContext::create_headless();
auto image = vk.value()->import_dmabuf(attrs.value(), VK_IMAGE_USAGE_SAMPLED_BIT);
```

#### 2. Synchronization Across API Boundaries

```cpp
#include <brodmabuf/sync.h>

// Create DRM syncobj and export sync_file
auto so = brodmabuf::SyncObj::create(drm_fd);
auto fence = so.value()->export_sync_file();

// Implicit synchronization on DMA-BUF (Linux 6.0+ ioctl)
auto write_fence = brodmabuf::export_dmabuf_sync_file(dmabuf_fd, /*write_fence=*/true);

// Vulkan semaphore export to drm_syncobj
auto sem = brodmabuf::create_exportable_semaphore(
    device, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
auto syncobj_fd = brodmabuf::export_semaphore_to_syncobj(device, sem.value());
```

#### 3. KMS Direct Scanout

```cpp
#include <brodmabuf/kms.h>

// Open KMS card (/dev/dri/card0) — requires DRM master
auto kms = brodmabuf::KmsDevice::open();
auto pipe = kms.value()->find_default_pipeline(); // connected connector, CRTC, primary plane

auto fb = brodmabuf::KmsFramebuffer::create_from_dmabuf(kms.value()->fd(), attrs.value());
auto presenter = brodmabuf::KmsPresenter::create(std::move(kms.value()), pipe.value());

presenter.value()->initialize_modeset(*fb.value());
auto out_fence = presenter.value()->present(*fb.value(), in_fence_fd);
presenter.value()->handle_event(16); // page-flip event
```

## Building

### Sibling vs. Submodule Layout

Downstream consumers resolve `brodmabuf` using standard ecosystem discovery:
1. An existing `brodmabuf::brodmabuf` target in CMake;
2. Sibling checkout beside the consumer (`../brodmabuf`, overridable via `-DBRODMABUF_DIR=<path>`);
3. Submodule fallback under `third_party/brodmabuf`.

#### Sibling Layout (Recommended for dev)

```bash
git clone https://github.com/wlejon/brocompositor
git clone https://github.com/wlejon/brodmabuf
```

#### Submodule Layout (Standalone clone)

```bash
git clone --recursive https://github.com/wlejon/brodmabuf
# Or in an existing clone:
git submodule update --init --recursive
```

### Consuming `brodmabuf` in CMake

```cmake
if(NOT TARGET brodmabuf::brodmabuf)
    set(BRODMABUF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../brodmabuf" CACHE PATH "Path to brodmabuf")
    if(EXISTS "${BRODMABUF_DIR}/CMakeLists.txt")
        add_subdirectory("${BRODMABUF_DIR}" "${CMAKE_BINARY_DIR}/brodmabuf" EXCLUDE_FROM_ALL)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/brodmabuf/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/brodmabuf" "${CMAKE_BINARY_DIR}/brodmabuf" EXCLUDE_FROM_ALL)
    endif()
endif()

target_link_libraries(my_app PRIVATE brodmabuf::brodmabuf)
```

### Build Commands

#### Linux (GCC 12+ or Clang, Ninja)

```bash
# Debian / Ubuntu
sudo apt install cmake ninja-build pkg-config libdrm-dev libgbm-dev libvulkan-dev \
    mesa-vulkan-drivers libgl1-mesa-dri
# Arch Linux
sudo pacman -S cmake ninja pkgconf libdrm mesa vulkan-headers vulkan-icd-loader

cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel 4
ctest --test-dir build-release --output-on-failure
```

#### Windows (MSVC 2022)

```powershell
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

#### macOS (Apple Clang 15+, Ninja)

```bash
brew install cmake ninja
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel 4
ctest --test-dir build-release --output-on-failure
```

### Build Options

- `-DBRODMABUF_BUILD_TESTS=ON|OFF` (default ON when top-level): build test suites.
- `-DBRODMABUF_COVERAGE=ON|OFF` (default OFF): instrument GCC/Clang with gcov for code coverage (`--coverage -fprofile-update=atomic -O0 -g`).

## Tests

The test suite runs real system operations against kernel interfaces and drivers (no `assert()` reliance):

| Test | Platforms | Oracle & Coverage |
| :--- | :--- | :--- |
| `test_types` | Everywhere | File descriptor ownership via real pipes (dup, move, close, `EPIPE` on closed reader) |
| `test_formats` | Everywhere | FourCC round-trips, modifier vendor/description decoding, format properties, subsampling math; Linux `VkFormat` mappings |
| `test_buffer` | Everywhere | Plane layout math vs. hand-computed plane sizes and validation bounds |
| `test_unavailable` | Windows, macOS | Verifies that allocator factories fail with `Status::Unsupported` and clear error text |
| `test_fourcc_compat` | Linux | Validates that every FourCC code in `drm_fourcc_compat.h` matches `<drm_fourcc.h>` |
| `test_gbm` | Linux (DRM node) | Pixels written via `gbm_bo_map` verified through `mmap` on exported DMA-BUF, and reverse |
| `test_vulkan` | Linux (Vulkan + DMA-BUF) | GBM buffer pixels copied out of imported `VkImage`; layout vs. exported stride; export/import roundtrip |
| `test_sync` | Linux (DRM node) | `drm_syncobj` signal/wait/reset/transfer and timeline points; `sync_file` merge/import; implicit sync; GPU semaphore synchronization |
| `test_kms` | Linux (DRM card) | Framebuffer creation via `drmModeGetFB2`; atomic `TEST_ONLY` commit, modeset, page flips with in/out fences |

### Environment Variables

- `BRODMABUF_DRM_DEVICE`: Specifies DRM render node path (default `/dev/dri/renderD128`).
- `BRODMABUF_KMS_DEVICE`: Specifies KMS card device path for `test_kms` (default `/dev/dri/card0`).

### CI Environment & Hardware Limitations

GitHub-hosted runners lack physical GPUs. The CI workflow (`.github/workflows/ci.yml`) tests the Linux stack using software emulation:

- **vkms (Virtual KMS)**: Loaded on the host to provide a virtual KMS card. GBM allocates dumb buffers on it via Mesa's `kms_swrast`, and `test_kms` acquires DRM master to test atomic commits and modesets.
- **udmabuf limitation**: Mesa lavapipe requires the kernel `udmabuf` module to support DMA-BUF import and export. Cloud runner kernels often omit `udmabuf`, which causes `test_vulkan` and the Vulkan portions of `test_sync` to skip (exit `77`).
- **drm_syncobj limitation**: `vkms` does not implement `drm_syncobj` ioctls; timeline and syncobj tests skip under `vkms`.
- **Bare-metal verification**: Full Vulkan DMA-BUF import/export, DRM syncobj, and KMS presentation paths are verified on physical GPUs (e.g. AMD Radeon with RADV / amdgpu).

## License

[MIT](LICENSE)
