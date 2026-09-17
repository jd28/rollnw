#pragma once

#include <RmlUi/Core/Types.h>

#include <string_view>

namespace Rml {
class Context;
class ElementDocument;
}

namespace nw::toolset {

// DOM/focus borrows last only for the call. Hidden ancestors exclude targets.
// These query one current UI context; they do not cache facts across callbacks.
[[nodiscard]] bool point_within_element(
    Rml::ElementDocument* doc, std::string_view id, Rml::Vector2f point);
[[nodiscard]] bool focused_text_input(Rml::Context* context);
[[nodiscard]] bool focused_element_has_id(Rml::Context* context, const char* id);

} // namespace nw::toolset
