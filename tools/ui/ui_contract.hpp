#pragma once

#include <string_view>

namespace nw::toolset::ui_contract {

inline constexpr std::string_view kVersion = "core.ui.v0";

namespace event {
inline constexpr std::string_view command_execute = "command.execute";
inline constexpr std::string_view console_execute = "console.execute";
inline constexpr std::string_view fallback_ui_event = "on_ui_event";
} // namespace event

namespace command {
inline constexpr std::string_view help = "help";
inline constexpr std::string_view clear = "clear";
inline constexpr std::string_view open = "open";
} // namespace command

namespace list_v1 {
inline constexpr std::string_view module = "core.ui.v1";
inline constexpr std::string_view areas_main = "areas.main";
inline constexpr std::string_view modules_main = "modules.main";

inline constexpr std::string_view event_hover = "hover";
inline constexpr std::string_view event_select = "select";
inline constexpr std::string_view event_activate = "activate";
inline constexpr std::string_view event_scroll = "scroll";
} // namespace list_v1

} // namespace nw::toolset::ui_contract
