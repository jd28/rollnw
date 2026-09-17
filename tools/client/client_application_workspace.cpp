#include "client_application_workspace.hpp"
#include "appearance_catalog.hpp"
#include "client_application_editor.hpp"
#include "client_application_input.hpp"
#include "client_application_shell.hpp"
#include "client_application_workbench.hpp"
#include "rollnw_tool_version.hpp"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/toolset_visual.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <utility>

namespace nw::toolset::client_application_detail {

void synchronize_client_mutations(ClientRenderer& renderer, ClientApplicationState& state,
    Rml::Context* context, Rml::ElementDocument* doc)
{
    const auto mutation = nw::toolset::object_mutation_state();
    if (mutation.epoch != state.observed_object_mutation_epoch) {
        const auto mutation_focus_target = nw::toolset::managed_list_focus_target(
            context ? context->GetFocusElement() : nullptr);
        state.observed_object_mutation_epoch = mutation.epoch;
        refresh_workspace_tabs(doc, state);
        const bool area_structure_changed = mutation.area_structure_epoch != state.observed_area_structure_epoch;
        const auto* displayed_tab = state.workspace.active_tab();
        const bool changed_area_visible = displayed_tab && displayed_tab->kind == nw::toolset::WorkspaceTabKind::area
            && displayed_tab->document.object() == mutation.area;
        if (area_structure_changed && changed_area_visible) {
            cancel_area_object_placement(renderer, state);
            cancel_area_object_drag(renderer, state);
            cancel_area_tile_stroke(renderer, state);
            const nw::ObjectHandle selected
                = state.area_workspace_surface
                    == AreaWorkspaceSurface::objects
                ? mutation.object
                : nw::ObjectHandle{};
            const bool rebuilt = renderer.rebuild_live_viewer_area(
                mutation.area, selected);
            if (!rebuilt) {
                state.stale_area_viewport = mutation.area;
                append_output(state, "error", "Failed to rebuild the live area viewport after structural edit");
            } else {
                state.observed_area_structure_epoch
                    = mutation.area_structure_epoch;
                state.stale_area_viewport = nw::ObjectHandle{};
                if (state.area_workspace_surface
                    == AreaWorkspaceSurface::tiles) {
                    nw::toolset::refresh_area_tile_selection_after_rebuild(
                        renderer, state.area_tile_editor, mutation.area);
                }
            }
            state.smalls.publish_active_area(mutation.area);
            if (state.area_workspace_surface
                    == AreaWorkspaceSurface::objects
                && mutation.object.type != nw::ObjectType::invalid) {
                state.smalls.publish_active_object(mutation.object);
                state.workbench.active_object_tab_id = state.workspace.active_tab_id();
                state.managed_list_reorder = {};
                nw::toolset::activate_object_workbench(state.workbench, mutation.object, state.workspace.active_tab_id());
            } else {
                state.smalls.clear_active_object();
                state.workbench.active_object_tab_id.clear();
                clear_active_object_details(state);
            }
            refresh_workspace_content(doc, state);
            sync_object_details_window(doc, state, true);
            sync_creature_feat_window(doc, state, true);
            sync_creature_spell_window(doc, state, true);
            sync_creature_inventory_window(doc, state, true);
            sync_appearance_window(doc, state, true);
        } else {
            if (area_structure_changed
                && state.stale_area_viewport.type
                    == nw::ObjectType::invalid) {
                state.observed_area_structure_epoch
                    = mutation.area_structure_epoch;
            }
            const auto* active_tab = state.workspace.active_tab();
            const bool area_tab = active_tab && active_tab->kind == nw::toolset::WorkspaceTabKind::area;
            if (mutation.kind == nw::toolset::ObjectMutationKind::spatial) {
                renderer.sync_viewer_area_object_spatial(mutation.object);
            } else if (mutation.kind == nw::toolset::ObjectMutationKind::structure
                && mutation.object.type == nw::ObjectType::encounter
                && !area_tab) {
                if (!renderer.rebuild_live_viewer_object(mutation.object)) {
                    append_output(state, "error",
                        "Failed to rebuild the Encounter spawn preview after spawn-list edit");
                }
            } else if (mutation.kind == nw::toolset::ObjectMutationKind::visual) {
                if (state.workbench.appearance_view.appearance_body_preview_object == mutation.object
                    && !update_appearance_preview_rows(mutation.object, false)) {
                    append_output(state, "error", "Failed to refresh the creature Appearance preview");
                }
                bool refreshed = false;
                if (mutation.visual_kind
                    == nw::toolset::ObjectVisualMutationKind::debug_geometry) {
                    refreshed = area_tab && renderer.rebuild_live_viewer_area(renderer.area_viewer_object(), mutation.object);
                } else if (mutation.visual_kind == nw::toolset::ObjectVisualMutationKind::detail
                    || (mutation.visual_kind == nw::toolset::ObjectVisualMutationKind::base_appearance
                        && mutation.object.type == nw::ObjectType::creature)) {
                    refreshed = renderer.refresh_live_viewer_object_visual(mutation.object);
                } else if (mutation.visual_kind == nw::toolset::ObjectVisualMutationKind::base_appearance) {
                    refreshed = area_tab
                        ? renderer.rebuild_live_viewer_area(
                              renderer.area_viewer_object(), mutation.object)
                        : renderer.rebuild_live_viewer_object(mutation.object);
                }
                if (!refreshed) {
                    append_output(state, "error", "Failed to refresh the live object viewport after visual edit");
                }
            }
            if (area_tab
                && state.area_workspace_surface
                    == AreaWorkspaceSurface::objects
                && editable_area_object(mutation.object)) {
                renderer.set_viewer_area_object_selection(mutation.object);
            }
            if (mutation.object == state.workbench.object_details.object && active_object_details_matches_tab(state)) {
                bool workbench_rebuilt = false;
                (void)nw::toolset::refresh_object_workbench_snapshots(state.workbench, mutation.object);
                const bool smalls_appearance_mutation = mutation.object.type == nw::ObjectType::door
                    || mutation.object.type == nw::ObjectType::item;
                if (state.workbench.object_workbench_surface == ObjectWorkbenchSurface::appearance
                    && appearance_catalog_kind(mutation.object.type)
                    && mutation.object.type != nw::ObjectType::placeable) {
                    rebuild_active_appearances(state, mutation.object);
                    if (mutation.kind == nw::toolset::ObjectMutationKind::visual) {
                        refresh_workspace_content(doc, state);
                        workbench_rebuilt = true;
                    }
                }
                if (smalls_appearance_mutation
                    || state.workbench.object_workbench_surface == ObjectWorkbenchSurface::inventory) {
                    refresh_workspace_content(doc, state);
                    workbench_rebuilt = true;
                }
                if (!workbench_rebuilt) {
                    refresh_smalls_elements(doc, state);
                }
                sync_object_details_window(doc, state, true);
                sync_creature_feat_window(doc, state, true);
                sync_creature_spell_window(doc, state, true);
                sync_creature_inventory_window(doc, state, true);
                sync_appearance_window(doc, state, true);
                nw::toolset::sync_managed_lists(doc,
                    nw::toolset::ui_v1_host(), state.workbench.managed_lists, true);
            }
        }
        if (mutation_focus_target
            && nw::toolset::focus_managed_list_target(
                doc, *mutation_focus_target)) {
            state.viewer_viewport_focused = false;
        }
    }
}

void apply_workspace_tab_scroll(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    apply_tab_scroll(doc, kWorkspaceTabScrollStrip,
        state.workspace_view.workspace_tab_scroll_x);
}

void apply_object_workbench_tab_scroll(
    Rml::ElementDocument* doc, ClientApplicationState& state)
{
    apply_tab_scroll(doc, kObjectWorkbenchTabScrollStrip,
        state.workbench.object_workbench_tab_scroll_x);
}

void clear_workspace_tab_drag(ClientApplicationState& state)
{
    nw::toolset::clear_workspace_tab_drag(state.workspace_view);
}

void set_recent_hover(Rml::ElementDocument* doc, ClientApplicationState& state, int index)
{
    nw::toolset::set_recent_hover(doc, state.browser, index);
}

bool render_project_tree_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    return nw::toolset::render_project_tree_window(doc, state.browser, force);
}

void refresh_recent_list(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    if (!doc) { return; }
    nw::toolset::refresh_browser_view(doc, state.browser, state.backend, state.shell,
        state.backend_ready, state.play_preview.selecting_actor);
    apply_shell_layout(doc, state);
}

void append_workspace_home_markup(std::string& content_markup, ClientApplicationState& state)
{
    const bool module_open = nw::toolset::append_workspace_home_start_markup(content_markup, state.browser,
        state.backend, state.loading, ROLLNW_TOOL_NAME " " ROLLNW_TOOL_VERSION);
    if (module_open) { nw::toolset::append_workspace_object_workbench_markup(content_markup, state.workspace,
        state.backend, state.workbench, state.area_workspace_surface, state.smalls.active_area()); }
    content_markup += "</div>";
}

bool workspace_home_active(const ClientApplicationState& state)
{
    const auto* tab = state.workspace.active_tab();
    return !tab || tab->kind == nw::toolset::WorkspaceTabKind::home;
}

void refresh_home_area_catalog(ClientApplicationState& state, bool force)
{
    nw::toolset::refresh_home_area_catalog(state.browser, state.backend, force);
}

bool sync_home_area_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    return nw::toolset::sync_home_area_window(doc, state.browser, workspace_home_active(state), force);
}

void refresh_workspace_content_impl(Rml::ElementDocument* doc, ClientApplicationState& state, bool preserve_controls)
{
    if (!doc) {
        return;
    }
    if (preserve_controls && state.workbench.creature_view.creature_spell_combobox.popup_visible()) {
        state.workbench.creature_view.creature_spell_combobox.invalidate_popup_render();
    }
    if (preserve_controls && active_appearances_match_tab(state)) {
        if (auto* editor = find_el(doc, "appearance_editor")) {
            state.workbench.appearance_view.appearance_editor_scroll_top = std::max(0.0f, editor->GetScrollTop());
        }
    }
    remember_tab_scroll(doc, kObjectWorkbenchTabScrollStrip,
        state.workbench.object_workbench_tab_scroll_x);

    ensure_active_dialog_document(state);
    sync_active_module_object(state);
    refresh_home_area_catalog(state, false);
    const auto* active_tab = state.workspace.active_tab();
    const bool focus_area = nw::toolset::area_viewport_changed(doc, active_tab);
    std::string content_markup;
    if (!active_tab || active_tab->kind == nw::toolset::WorkspaceTabKind::home) {
        append_workspace_home_markup(content_markup, state);
    } else {
        append_workspace_subtabs_markup(content_markup, *active_tab);
        nw::toolset::append_workspace_document_markup(content_markup, *active_tab, state.workspace,
            state.backend, state.workbench, state.area_workspace_surface, state.area_tile_editor,
            state.dialog_view, state.smalls.active_area());
    }

    if (auto* content = doc->GetElementById("workspace_content")) {
        content->SetInnerRML(content_markup);
    }
    state.workbench.object_workbench_tab_scroll_pending = true;
    apply_shell_layout(doc, state);
    sync_home_area_window(doc, state, true);
    nw::toolset::hydrate_object_workbench(doc, state.workbench, state.workspace);
    refresh_smalls_elements(doc, state);
    if (preserve_controls && active_appearances_match_tab(state)) {
        if (auto* editor = find_el(doc, "appearance_editor")) {
            editor->SetScrollTop(state.workbench.appearance_view.appearance_editor_scroll_top);
        }
    }
    sync_object_details_window(doc, state, true);
    nw::toolset::sync_managed_lists(
        doc, nw::toolset::ui_v1_host(), state.workbench.managed_lists, true);
    sync_creature_inventory_window(doc, state, true);
    nw::toolset::sync_dialog_view(doc, state.dialog_view, true);
    if (focus_area) focus_workspace_viewport(doc, state);
}

void refresh_workspace_content(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    refresh_workspace_content_impl(doc, state, true);
}

void refresh_workspace_tabs(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    nw::toolset::refresh_workspace_tabs(doc, state.workspace_view, state.workspace);
}

void refresh_workspace_view(Rml::ElementDocument* doc, ClientApplicationState& state)
{
    if (!doc) { return; }
    refresh_workspace_tabs(doc, state);
    refresh_workspace_content_impl(doc, state, false);
}

std::optional<WorkspaceViewerViewportRequest> active_workspace_viewer_viewport_request(
    Rml::ElementDocument* doc, ClientApplicationState& state, int frame_width, int frame_height)
{
    return nw::toolset::active_workspace_viewer_viewport_request(doc, state.workspace, state.backend, frame_width, frame_height);
}

void clear_inactive_object(ClientApplicationState& state)
{
    if (state.workbench.active_object_tab_id.empty()) {
        return;
    }

    const auto* active_tab = state.workspace.active_tab();
    if (active_tab_has_object_workbench(active_tab)
        && active_tab->id == state.workbench.active_object_tab_id) {
        return;
    }

    state.smalls.clear_active_object();
    state.smalls.clear_active_area();
    state.workbench.active_object_tab_id.clear();
}

} // namespace nw::toolset::client_application_detail
