# brodmabuf

[![CI](https://github.com/wlejon/brodmabuf/actions/workflows/ci.yml/badge.svg)](https://github.com/wlejon/brodmabuf/actions/workflows/ci.yml)
[![CodeQL](https://github.com/wlejon/brodmabuf/actions/workflows/codeql.yml/badge.svg)](https://github.com/wlejon/brodmabuf/actions/workflows/codeql.yml)

A small C++20 library for sharing GPU buffers between processes and APIs on
Linux: DMA-BUF descriptions, GBM allocation, Vulkan import and export,
explicit and implicit synchronization (`drm_syncobj`, `sync_file`), and KMS
scanout of DMA-BUFs. Every handle is RAII, every fallible call returns a
`Result<T>` carrying the reason it failed.

Part of the **[bro](https://github.com/wlejon/bro)** ecosystem, built in the
mould of [brodisplays](https://github.com/wlejon/brodisplays) and
[brocompositor](https://github.com/wlejon/brocompositor).

## Platform support

DMA-BUF, GBM, DRM syncobjs and KMS are Linux kernel interfaces, so the library
does its job **on Linux only**. On Windows and macOS it still configures and
builds with no Linux dependencies, and reports that honestly:

| | Linux | Windows / macOS |
| :--- | :--- | :--- |
| `types.h` (`UniqueFd`, `Status`, `Result<T>`) | yes | yes |
| `formats.h` (FourCC codes, modifiers, format properties) | yes, with `<drm_fourcc.h>` | yes, with the bundled `drm_fourcc_compat.h` (checked value for value against the kernel header on Linux) |
| `formats.h` `VkFormat` mapping | yes | not compiled |
| `buffer.h` (`DmaBufAttributes`, plane layout math) | yes | yes |
| `allocator.h` (`DmaBufAllocator`) | yes | `create_default()` / `create_gbm()` fail with `Status::Unsupported` and say why; `platform_status()` says the same up front |
| `gbm.h`, `vulkan.h`, `sync.h`, `kms.h` | yes | `#error`: Linux-only headers |

Linux needs libdrm, Mesa's GBM and the Vulkan loader + headers. Kernel
features used only where present: `DMA_BUF_IOCTL_EXPORT/IMPORT_SYNC_FILE`
(Linux 6.0+), `DRM_CAP_SYNCOBJ` and `DRM_CAP_SYNCOBJ_TIMELINE` (driver
dependent; vkms has neither), atomic KMS.

## Build

```bash
cmake -B build                       # Windows (Visual Studio generator)
cmake --build build --config Release
ctest --test-dir build -C Release

cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release   # Linux / macOS
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

Options: `BRODMABUF_BUILD_TESTS` (on when top level), `BRODMABUF_COVERAGE`
(gcov instrumentation, GCC/Clang). Consumers use the `brodmabuf::brodmabuf`
target via `add_subdirectory`.

Debian/Ubuntu packages: `libdrm-dev libgbm-dev libvulkan-dev`, plus
`mesa-vulkan-drivers libgl1-mesa-dri` to run the device tests without a GPU
(lavapipe, GBM's `kms_swrast`).

## API

```
include/brodmabuf/
  types.h       UniqueFd, Status, Result<T>, kMaxPlanes
  formats.h     FourCC <-> string, modifier vendor/description, format properties,
                DRM <-> VkFormat (Linux)
  buffer.h      DmaBufPlane, DmaBufAttributes (validate, dup, total size),
                plane layout math (subsampling, minimum strides)
  allocator.h   DmaBufAllocator: allocate a DMA-BUF with GBM; platform_status()
  gbm.h         GbmDevice, GbmBuffer: create (with modifiers), map, export, import
  vulkan.h      VulkanContext: import a DMA-BUF as a VkImage, query modifiers and
                plane layouts, create exportable images and export them
  sync.h        SyncObj (binary + timeline), SyncFile, DMA-BUF implicit sync,
                Vulkan semaphore <-> syncobj / sync_file
  kms.h         KmsDevice, KmsFramebuffer (AddFB2 from a DMA-BUF), KmsAtomicReq,
                KmsPresenter (modeset, page flips with in/out fences)
```

Allocate with GBM, hand the buffer to Vulkan:

```cpp
#include <brodmabuf/gbm.h>
#include <brodmabuf/vulkan.h>

auto gbm = brodmabuf::GbmDevice::open();            // first render node
if (!gbm.ok()) return fail(gbm.error_message());
auto bo = gbm.value()->create_buffer_with_modifiers(1920, 1080, DRM_FORMAT_ARGB8888,
                                                    {DRM_FORMAT_MOD_LINEAR});
auto attrs = bo.value()->export_dmabuf();           // fds, strides, offsets, modifier

auto vk = brodmabuf::VulkanContext::create_headless();
auto image = vk.value()->import_dmabuf(attrs.value(), VK_IMAGE_USAGE_SAMPLED_BIT);
```

Synchronize across the boundary:

```cpp
#include <brodmabuf/sync.h>

auto so = brodmabuf::SyncObj::create(drm_fd);
auto fence = so.value()->export_sync_file();        // a sync_file to pass on

// Implicit sync on a DMA-BUF (Linux 6.0+): the fence its readers must wait for.
auto write_fence = brodmabuf::export_dmabuf_sync_file(dmabuf_fd, /*write_fence=*/true);

// Vulkan: a semaphore whose payload is a drm_syncobj.
auto sem = brodmabuf::create_exportable_semaphore(device, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
auto syncobj_fd = brodmabuf::export_semaphore_to_syncobj(device, sem.value());
```

Scan a DMA-BUF out (needs DRM master, which a running display server holds):

```cpp
#include <brodmabuf/kms.h>

auto kms = brodmabuf::KmsDevice::open();             // first card node
auto pipe = kms.value()->find_default_pipeline();    // connected output, CRTC, primary plane
auto fb = brodmabuf::KmsFramebuffer::create_from_dmabuf(kms.value()->fd(), attrs.value());
auto presenter = brodmabuf::KmsPresenter::create(std::move(kms.value()), pipe.value());
presenter.value()->initialize_modeset(*fb.value());
auto out_fence = presenter.value()->present(*fb.value(), in_fence_fd);
presenter.value()->handle_event(16);                 // page-flip event
```

Timeouts on `SyncObj::wait` / `timeline_wait` are relative nanoseconds; the
library converts them to the absolute `CLOCK_MONOTONIC` deadline the kernel
takes.

## Tests

`tests/check.h` holds the checks (real in every configuration, no `assert()`).
A test that cannot run exits 77 with the reason, and ctest reports it as
skipped, never passed; a check inside a running test that cannot run prints
`SKIP check <what>: <why>`. The oracle is the kernel or the GPU, not the
library's own bookkeeping:

| Test | Where | Oracle |
| :--- | :--- | :--- |
| `test_types` | everywhere | fd ownership through real pipes (dup, move, close, `EPIPE` once the reader is gone) |
| `test_formats` | everywhere | FourCC round trips, format properties, subsampling and modifier decoding against the published layouts; the `VkFormat` table on Linux |
| `test_buffer` | everywhere | layout math against hand-computed plane sizes; validity rules |
| `test_unavailable` | Windows, macOS | the factories fail `Unsupported` with the platform's reason |
| `test_fourcc_compat` | Linux | every code in `drm_fourcc_compat.h` equals the kernel's `<drm_fourcc.h>` |
| `test_gbm` | Linux, DRM node | pixels written through `gbm_bo_map` read back by `mmap`ing the exported DMA-BUF (bracketed by `DMA_BUF_IOCTL_SYNC`), and the reverse; the exported fd outlives the bo; re-import keeps geometry |
| `test_vulkan` | Linux, Vulkan with DMA-BUF import | a GBM buffer's pixels read back by a GPU copy out of the imported `VkImage`; Vulkan's subresource layout equals the exported stride; an exported image re-imported and copied out holds the colour it was cleared to (any tiling); linear exports also read on the CPU through GBM |
| `test_sync` | Linux, DRM node | syncobj signal/wait/reset/transfer and timeline points through the kernel (including a 50 ms wait taking 50 ms); `sync_file` export/merge/import; DMA-BUF implicit fences; Vulkan semaphores signalled on the GPU read as signalled through the kernel object, and a kernel-signalled syncobj releases a GPU wait |
| `test_kms` | Linux, DRM card | framebuffers created from DMA-BUFs are known to the kernel (`drmModeGetFB2`) and gone after release; with DRM master: an atomic `TEST_ONLY` commit, a modeset, a blocking flip whose page-flip event and out-fence arrive, a non-blocking flip gated on an in-fence, and the output turned off again |

Environment: `BRODMABUF_DRM_DEVICE` picks the DRM node, `BRODMABUF_KMS_DEVICE`
the card for `test_kms`.

What runs where (CI: `.github/workflows/ci.yml`): GitHub's runners have no
GPU, so the Linux jobs load **vkms** (a virtual KMS card, on which `test_kms`
takes DRM master and modesets) and **udmabuf** (which lavapipe allocates
exportable memory from) on the host and run the tests in `debian:trixie`.
vkms implements no `drm_syncobj`, so the syncobj and timeline parts of
`test_sync` run only on real GPU drivers; they are verified on amdgpu (Arch,
Linux 7.2). Modesetting is not exercised on a machine whose display server
holds master. Each job's log ends with the tests and checks it skipped, and
why (`.github/ci/ctest.sh`).

## License

MIT, see [LICENSE](LICENSE).
