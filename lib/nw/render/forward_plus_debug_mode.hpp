#pragma once

#include <cstdint>

namespace nw::render {

enum class ForwardPlusDebugMode : uint32_t {
    off = 0,
    cluster_light_count = 1,
    depth_slice = 2,
};

} // namespace nw::render
