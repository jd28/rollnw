#pragma once

#include <nw/render/render_context.hpp>

#include <optional>
#include <string_view>

namespace nw::toolset {

[[nodiscard]] const char* forward_plus_debug_mode_label(nw::render::ForwardPlusDebugMode mode) noexcept;
[[nodiscard]] nw::render::ForwardPlusDebugMode next_forward_plus_debug_mode(
    nw::render::ForwardPlusDebugMode mode) noexcept;
[[nodiscard]] std::optional<nw::render::ForwardPlusDebugMode> parse_forward_plus_debug_mode(
    std::string_view value);

} // namespace nw::toolset
