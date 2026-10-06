// The values as drm_fourcc_compat.h spells them. This translation unit must
// not see the kernel's <drm_fourcc.h>; test_fourcc_compat.cpp has those.
#include "brodmabuf/drm_fourcc_compat.h"
#include "fourcc_list.h"

#include <cstdint>
#include <utility>
#include <vector>

std::vector<std::pair<const char*, uint64_t>> compat_fourcc_values() {
#define BRODMABUF_ENTRY(name) {#name, static_cast<uint64_t>(name)},
    return {BRODMABUF_FOURCC_LIST(BRODMABUF_ENTRY)};
#undef BRODMABUF_ENTRY
}
