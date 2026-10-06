// drm_fourcc_compat.h (the codes off Linux) against the kernel's own
// <drm_fourcc.h>: every value must match, so a Windows or macOS build names
// the same formats and modifiers a Linux one does. Linux only.
#include "check.h"
#include "fourcc_list.h"

#include <drm_fourcc.h>

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
    std::printf("%zu codes compared\n", kernel.size());
    return bstest::finish("test_fourcc_compat");
}
