#pragma once

#include <string>
#include <string_view>

namespace nw::toolset {

// RmlUi has no read-only text input. Keep the preview disabled until its caller
// defines inline editing; the supplied button remains an independent action.
// action_button is trusted, caller-owned RML (never user text).
inline std::string render_rml_action_line_edit(std::string_view value,
    std::string_view placeholder, std::string_view action_button)
{
    const auto escape_attribute = [](std::string_view text) {
        std::string escaped;
        escaped.reserve(text.size());
        for (const char ch : text) {
            switch (ch) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&#39;";
                break;
            default:
                escaped.push_back(ch);
                break;
            }
        }
        return escaped;
    };

    std::string markup;
    markup.reserve(value.size() + placeholder.size() + action_button.size() + 160);
    markup += "<span class=\"action_line_edit\"><input class=\"action_line_edit_input\" type=\"text\" disabled value=\"";
    markup += escape_attribute(value);
    markup += "\" placeholder=\"";
    markup += escape_attribute(placeholder);
    markup += "\"/><span class=\"action_line_edit_separator\"></span>";
    markup += action_button;
    markup += "</span>";
    return markup;
}

} // namespace nw::toolset
