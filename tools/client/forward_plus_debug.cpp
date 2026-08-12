#include "forward_plus_debug.hpp"

#include <string>

namespace nw::toolset {

const char* forward_plus_debug_mode_label(nw::render::ForwardPlusDebugMode mode) noexcept
{
    switch (mode) {
    case nw::render::ForwardPlusDebugMode::off:
        return "off";
    case nw::render::ForwardPlusDebugMode::cluster_light_count:
        return "cluster-lights";
    case nw::render::ForwardPlusDebugMode::depth_slice:
        return "depth-slices";
    }
    return "unknown";
}

nw::render::ForwardPlusDebugMode next_forward_plus_debug_mode(nw::render::ForwardPlusDebugMode mode) noexcept
{
    switch (mode) {
    case nw::render::ForwardPlusDebugMode::off:
        return nw::render::ForwardPlusDebugMode::cluster_light_count;
    case nw::render::ForwardPlusDebugMode::cluster_light_count:
        return nw::render::ForwardPlusDebugMode::depth_slice;
    case nw::render::ForwardPlusDebugMode::depth_slice:
        return nw::render::ForwardPlusDebugMode::off;
    }
    return nw::render::ForwardPlusDebugMode::off;
}

std::optional<nw::render::ForwardPlusDebugMode> parse_forward_plus_debug_mode(std::string_view value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char ch : value) {
        if (ch == '_') {
            normalized.push_back('-');
        } else if (ch >= 'A' && ch <= 'Z') {
            normalized.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            normalized.push_back(static_cast<char>(ch));
        }
    }

    if (normalized == "off" || normalized == "none" || normalized == "0") {
        return nw::render::ForwardPlusDebugMode::off;
    }
    if (normalized == "cluster-lights" || normalized == "cluster-light-count"
        || normalized == "clusters" || normalized == "lights" || normalized == "1") {
        return nw::render::ForwardPlusDebugMode::cluster_light_count;
    }
    if (normalized == "depth-slices" || normalized == "depth-slice" || normalized == "depth"
        || normalized == "slices" || normalized == "z" || normalized == "2") {
        return nw::render::ForwardPlusDebugMode::depth_slice;
    }
    return std::nullopt;
}

} // namespace nw::toolset
