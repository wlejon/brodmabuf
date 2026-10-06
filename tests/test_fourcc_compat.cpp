// drm_fourcc_compat.h (the codes off Linux) against the kernel's own
// <drm_fourcc.h>: every value must match, so a Windows or macOS build names
// the same formats and modifiers a Linux one does. Linux only.
#include "check.h"
#include "fourcc_list.h"

#include <drm_fourcc.h>

// Codes an older libdrm does not define yet: nothing to compare them with,
// so they take the value formats.h supplies and are reported as skipped.
#ifndef DRM_FORMAT_MOD_VENDOR_MTK
#define DRM_FORMAT_MOD_VENDOR_MTK 0x0b
#define BRODMABUF_LIBDRM_LACKS_MTK 1
#endif
#ifndef DRM_FORMAT_MOD_VENDOR_APPLE
#define DRM_FORMAT_MOD_VENDOR_APPLE 0x0c
#define BRODMABUF_LIBDRM_LACKS_APPLE 1
#endif

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

std::vector<std::pair<const char*, uint64_t>> compat_fourcc_values();

int main() {
#define BRODMABUF_ENTRY(name) {#name, static_cast<uint64_t>(name)},
    const std::vector<std::pair<const char*, uint64_t>> kernel = {
        BRODMABUF_FOURCC_LIST(BRODMABUF_ENTRY)};
#undef BRODMABUF_ENTRY
    const auto compat = compat_fourcc_values();
    CHECK_EQ(compat.size(), kernel.size());
    for (size_t i = 0; i < kernel.size() && i < compat.size(); ++i) {
        if (compat[i].second != kernel[i].second) {
            bstest::fail(__FILE__, __LINE__,
                         std::string(kernel[i].first) + ": compat " + std::to_string(compat[i].second) +
                             ", kernel " + std::to_string(kernel[i].second));
        }
    }
#ifdef BRODMABUF_LIBDRM_LACKS_MTK
    bstest::skip_check("DRM_FORMAT_MOD_VENDOR_MTK", "this libdrm's <drm_fourcc.h> does not define it");
#endif
#ifdef BRODMABUF_LIBDRM_LACKS_APPLE
    bstest::skip_check("DRM_FORMAT_MOD_VENDOR_APPLE", "this libdrm's <drm_fourcc.h> does not define it");
#endif
    std::printf("%zu codes compared\n", kernel.size());
    return bstest::finish("test_fourcc_compat");
}
