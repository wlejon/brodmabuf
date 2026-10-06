// Which DRM node a Linux test uses. BRODMABUF_DRM_DEVICE names one
// explicitly (CI points it at a vkms card); otherwise the first accessible
// render node, then the first accessible card node.
#pragma once

#include "check.h"
#include "brodmabuf/gbm.h"

#include <string>

namespace bstest {

inline std::string drm_node() {
    std::string node = env("BRODMABUF_DRM_DEVICE");
    if (!node.empty()) return node;
    node = brodmabuf::find_render_node();
    if (!node.empty()) return node;
    return brodmabuf::find_card_node();
}

}  // namespace bstest
