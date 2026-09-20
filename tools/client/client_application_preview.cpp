#include "client_application_preview.hpp"
#include "client_application_editor.hpp"
#include "client_application_input.hpp"
#include "client_application_shell.hpp"
#include "client_application_workspace.hpp"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/toolset_visual.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <utility>

namespace nw::toolset::client_application_detail {
void restore_play_preview_picker_shell(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    if (!nw::toolset::restore_play_preview_picker_shell(doc, state.play_preview, state.shell)) { return; }
    refresh_recent_list(doc, state);
    focus_workspace_viewport(doc, state);
}

void stop_play_preview(ClientRenderer& renderer, SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc, ClientApplicationState& state)
{
    nw::toolset::stop_play_preview(renderer, state.play_preview, state.runtime_input, state.shell);
    state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none;
    apply_shell_layout(doc, state);
    system_interface.SetMouseCursor("arrow");
}

void request_play_preview_actor(Rml::ElementDocument* doc, ClientApplicationState& state, std::string_view reason)
{
    nw::toolset::request_play_preview_actor(doc, state.play_preview, state.shell, reason);
    refresh_recent_list(doc, state);
    state.viewer_viewport_focused = false;
    if (auto* search = find_el(doc, "recent_search")) { search->Focus(); }
}

bool start_play_preview_from_ray(ClientRenderer& renderer, SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc, ClientApplicationState& state, const ClientViewportRay& ray)
{
    const auto result = nw::toolset::start_play_preview_from_ray(renderer, state.play_preview,
        state.runtime_input, state.shell, ray);
    using nw::toolset::PlayPreviewStartStatus;
    if (result.status == PlayPreviewStartStatus::unavailable) { return false; }
    if (result.status == PlayPreviewStartStatus::spawn_failed) {
        system_interface.SetMouseCursor("cross");
        append_output(state, "error", result.message);
        return false;
    }
    apply_shell_layout(doc, state);
    system_interface.SetMouseCursor("arrow");
    const bool started = result.status == PlayPreviewStartStatus::started;
    if (!started) { state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none; }
    append_output(state, started ? "info" : "error", result.message);
    return started;
}

bool prepare_play_preview(ClientRenderer& renderer,
    SystemInterface_SDL& system_interface,
    Rml::ElementDocument* doc,
    ClientApplicationState& state,
    const std::filesystem::path& selected_actor)
{
    if (state.play_preview.session.active()
        || state.play_preview.placement_pending()) {
        return true;
    }
    const auto warn = [&](std::string_view message) {
        append_output(state, "warn", message);
        state.shell.set_output_panel_visible(true);
        refresh_bottom_dock_view(doc, state);
    };
    // Startup needs the active document, not a laid-out viewport rectangle.
    // The DOM may have just been rebuilt; geometry is only needed on placement.
    const auto* tab = state.workspace.active_tab();
    const auto project_dir = state.backend.current_project_dir();
    if (!tab || tab->kind != nw::toolset::WorkspaceTabKind::area
        || tab->detail.empty() || project_dir.empty()) {
        warn("F9 play preview requires an open project area");
        return false;
    }
    if (!synchronize_area_viewport_structure(renderer, state, true)) {
        warn("F9 play preview is blocked until the area viewport rebuilds");
        return false;
    }

    const auto resolved = nw::toolset::resolve_play_preview_actor(project_dir, selected_actor);
    if (!resolved.actor.valid()) {
        request_play_preview_actor(doc, state, resolved.diagnostic);
        return false;
    }
    if (!renderer.area_viewer_matches_resource(tab->detail)) {
        warn("Area viewport is still loading; press F9 again when it is visible");
        return false;
    }
    const nw::ObjectHandle area = renderer.area_viewer_object();
    if (area.type != nw::ObjectType::area) {
        warn("Area viewport is not ready for play preview");
        return false;
    }

    nw::toolset::arm_play_preview(state.play_preview, state.runtime_input, resolved.actor,
        area, state.backend.module_generation(), state.workspace.active_tab_id());
    state.viewer_viewport_pointer_owner = nw::toolset::ClientPointerOwner::none;
    restore_play_preview_picker_shell(doc, state);
    focus_workspace_viewport(doc, state);
    apply_shell_layout(doc, state);
    system_interface.SetMouseCursor("cross");
    append_output(state, "info", "Click a walkable area surface to start play preview");
    return true;
}

void sync_viewer_render_options(ClientRenderer& renderer, const ClientApplicationState& state)
{
    renderer.set_area_viewer_options(ClientAreaViewerOptions{
        .lights_enabled = state.shell.viewer_area_lights_enabled,
        .debug_enabled = state.shell.viewer_area_debug_enabled,
        .triggers_enabled = state.shell.viewer_area_triggers_enabled,
        .encounters_enabled = state.shell.viewer_area_encounters_enabled,
        .tile_grid_enabled = !state.play_preview.session.active()
            && !state.play_preview.placement_pending()
            && !state.play_preview.selecting_actor,
        .forward_plus_enabled = state.shell.viewer_forward_plus_enabled,
        .forward_plus_auto_configure_area = state.shell.viewer_forward_plus_auto_configure_area,
        .forward_plus_tile_size = state.shell.viewer_forward_plus_tile_size,
        .forward_plus_depth_slices = state.shell.viewer_forward_plus_depth_slices,
        .forward_plus_max_lights_per_cluster = state.shell.viewer_forward_plus_max_lights_per_cluster,
        .forward_plus_debug_mode = state.shell.viewer_forward_plus_debug_mode,
        .fog_enabled = state.shell.viewer_area_fog_enabled,
        .shadows_enabled = state.shell.viewer_area_shadows_enabled,
        .day_night_autoplay = state.shell.viewer_area_day_night_autoplay,
        .day_night_elapsed_seconds = state.shell.viewer_area_day_night_elapsed_seconds,
        .day_night_time_generation = state.shell.viewer_area_day_night_time_generation,
        .reload_generation = state.shell.viewer_area_reload_generation,
    });
}

} // namespace nw::toolset::client_application_detail
