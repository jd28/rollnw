#include "client_input.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <string>

namespace nw::toolset {

bool point_within_element(Rml::ElementDocument* doc, std::string_view id, Rml::Vector2f point)
{
    auto* element = doc ? doc->GetElementById(std::string(id)) : nullptr;
    return element && element->IsVisible(true)
        && element->IsPointWithinElement(point);
}

bool focused_text_input(Rml::Context* context)
{
    auto* focus = context ? context->GetFocusElement() : nullptr;
    if (!focus) {
        return false;
    }

    if (!focus->IsVisible(true)) {
        return false;
    }

    for (auto* cursor = focus; cursor; cursor = cursor->GetParentNode()) {
        const Rml::String id = cursor->GetId();
        if (id == "command_input"
            || id == "terminal_input"
            || id == "recent_search"
            || id == "output_filter") {
            return true;
        }
        if (cursor->GetTagName() == "input"
            || cursor->GetTagName() == "textarea") {
            return true;
        }
    }
    return false;
}

bool focused_element_has_id(Rml::Context* context, const char* id)
{
    auto* focus = context ? context->GetFocusElement() : nullptr;
    if (!focus || !id || !focus->IsVisible(true)) {
        return false;
    }

    for (auto* cursor = focus; cursor; cursor = cursor->GetParentNode()) {
        if (cursor->GetId() == id) {
            return true;
        }
    }
    return false;
}

} // namespace nw::toolset
