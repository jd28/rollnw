#include "play_preview_view.hpp"
#include "project.hpp"
#include "shell_controller.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <fmt/format.h>

#include <cmath>

namespace nw::toolset {
namespace {

Rml::Element* find_el(Rml::ElementDocument* doc, const char* id)
{
    return doc ? doc->GetElementById(id) : nullptr;
}
std::string get_input_value(Rml::ElementDocument* doc, const char* id)
{
    if (!doc) {
        return {};
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            return control->GetValue();
        }
        return input->GetAttribute<Rml::String>("value", "");
    }
    return {};
}

void set_input_value(Rml::ElementDocument* doc, const char* id, std::string_view value)
{
    if (!doc) {
        return;
    }
    if (auto* input = doc->GetElementById(id)) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            control->SetValue(Rml::String(value));
            return;
        }
        input->SetAttribute("value", Rml::String(value));
    }
}

} // namespace

void request_play_preview_actor(Rml::ElementDocument* doc, PlayPreviewState& preview, ShellController& shell,
    std::string_view reason)
{
    if (!reason.empty()) shell.append_output("warn", reason);
    if (!preview.selecting_actor) {
        preview.selecting_actor = true;
        preview.picker_was_showing_project_tree
            = shell.showing_project_tree;
        preview.picker_was_showing_areas = shell.showing_areas;
        preview.picker_previous_query = get_input_value(doc, "recent_search");
        set_input_value(doc, "recent_search", "");
        shell.set_showing_project_tree(true);
    }
}

bool restore_play_preview_picker_shell(Rml::ElementDocument* doc, PlayPreviewState& preview, ShellController& shell)
{
    if (!preview.selecting_actor) return false;
    preview.selecting_actor = false;
    if (preview.picker_was_showing_project_tree) {
        shell.set_showing_project_tree(true);
    } else if (preview.picker_was_showing_areas) {
        shell.set_showing_areas(true);
    } else {
        shell.set_showing_project_tree(false);
    }
    set_input_value(doc, "recent_search", preview.picker_previous_query);
    preview.picker_previous_query.clear();
    return true;
}

float play_preview_yaw(const ClientViewportRay& ray) noexcept
{
    const glm::vec2 direction{ray.displacement.x, ray.displacement.y};
    const float length_squared = glm::dot(direction, direction);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-8f) {
        return 0.0f;
    }
    return std::atan2(direction.y, direction.x);
}

PlayPreviewActorResult resolve_play_preview_actor(const std::filesystem::path& project,
    const std::filesystem::path& selected_actor)
{
    auto actor_path = selected_actor;
    if (actor_path.empty()) {
        const auto settings = load_project_preview_settings(project);
        if (!settings.ok || settings.test_actor.empty()) {
            return {.diagnostic = settings.message.empty()
                    ? "Choose a Creature blueprint for play preview"
                    : settings.message};
        }
        actor_path = settings.test_actor;
    }
    const auto actor = Resource::from_path(actor_path, false);
    if (actor.type != ResourceType::utc || !actor.valid()) {
        return {.diagnostic = "Play-preview test actor must be a Creature blueprint"};
    }
    return {.actor = actor};
}

void sync_play_preview_viewport_overlay(Rml::ElementDocument* fps_doc,
    const std::optional<ClientViewportRect>& viewer_viewport,
    const PlayPreviewState& preview)
{
    auto* overlay = find_el(fps_doc, "play_preview_viewport_overlay");
    if (!overlay) return;

    const bool visible = viewer_viewport && viewer_viewport->valid()
        && (preview.session.active()
            || preview.placement_pending()
            || preview.selecting_actor);
    if (!visible) {
        overlay->SetProperty("display", "none");
        return;
    }

    constexpr int kOverlayMargin = 8;
    const auto& rect = *viewer_viewport;
    const bool navigation_debug
        = nw::toolset::toolset_preview_navigation_debug(
            preview.session)
              .enabled;
    const bool placement_failed = preview.placement_pending()
        && !preview.placement_diagnostic.empty();
    overlay->SetInnerRML(
        preview.selecting_actor
            ? "<div class=\"play_preview_viewport_title\">Area Preview — Choose Creature</div>"
              "<div class=\"play_preview_viewport_help\">Select a Creature blueprint in the left panel | F9 or Escape to cancel</div>"
            : preview.placement_pending()
            ? placement_failed
                ? fmt::format(
                      "<div class=\"play_preview_viewport_title\">Area Preview</div>"
                      "<div class=\"play_preview_viewport_error\">{}</div>"
                      "<div class=\"play_preview_viewport_help\">Click another walkable point | F9 or Escape to cancel</div>",
                      Rml::StringUtilities::EncodeRml(preview.placement_diagnostic))
                : "<div class=\"play_preview_viewport_title\">Area Preview</div>"
                  "<div class=\"play_preview_viewport_help\">Click a walkable point to enter | F9 or Escape to cancel</div>"
            : navigation_debug
            ? "<div class=\"play_preview_viewport_title\">Area Preview</div>"
              "<div class=\"play_preview_viewport_help\">Nav: walkable green | blockers red | route cyan | F8 hide | F9/Escape return</div>"
            : "<div class=\"play_preview_viewport_title\">Area Preview</div>"
              "<div class=\"play_preview_viewport_help\">F9 or Escape to return | F8 navigation debug</div>");
    overlay->SetProperty("display", "block");
    overlay->SetProperty("width", placement_failed || preview.selecting_actor ? "500px" : "320px");
    overlay->SetProperty("height", placement_failed || preview.selecting_actor ? "66px" : "50px");
    overlay->SetProperty("left", std::to_string(rect.x + kOverlayMargin) + "px");
    overlay->SetProperty("top", std::to_string(rect.y + kOverlayMargin) + "px");
}

} // namespace nw::toolset
