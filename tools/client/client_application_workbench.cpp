#include "client_application_workbench.hpp"
#include "appearance_view.hpp"
#include "client_application_commands.hpp"
#include "creature_workbench_view.hpp"
#include "inventory_workbench_view.hpp"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <utility>

namespace nw::toolset::client_application_detail {
void hide_object_variable_warning_tooltip(
    Rml::ElementDocument* doc, ClientApplicationState& state)
{
    nw::toolset::hide_object_variable_warning_tooltip(doc, state.workbench);
}

void sync_object_variable_warning_tooltip(Rml::ElementDocument* doc,
    ClientApplicationState& state,
    Rml::Element* hit,
    Rml::Vector2f point,
    int viewport_width,
    int viewport_height)
{
    nw::toolset::sync_object_variable_warning_tooltip(doc, state.workbench, state.shell.command_palette_visible, hit, point, viewport_width, viewport_height);
}

void dispatch_managed_list_events(ClientApplicationState& state)
{
    nw::toolset::dispatch_smalls_list_events(state.smalls, state.shell);
}

bool synchronize_smalls_runtime(ClientApplicationState& state)
{
    return nw::toolset::synchronize_smalls_view(state.smalls, state.rml_smalls_binding.get(), state.rml_smalls_data_model.get());
}

void refresh_smalls_elements(Rml::ElementDocument* document, ClientApplicationState& state)
{
    nw::toolset::refresh_smalls_view(document, state.smalls, state.rml_smalls_binding.get(), state.rml_smalls_data_model.get());
}

nw::toolset::SmallsListActivation activate_managed_list(Rml::ElementDocument* document, ClientApplicationState& state, Rml::Element* hit)
{
    return nw::toolset::activate_smalls_list(document, hit, state.smalls, state.rml_smalls_binding.get(), state.rml_smalls_data_model.get(), state.shell, state.workbench.managed_lists);
}

bool cycle_managed_list(Rml::ElementDocument* document, ClientApplicationState& state, Rml::Element* element, int delta)
{
    return nw::toolset::cycle_smalls_list(document, element, delta, state.smalls, state.rml_smalls_binding.get(), state.rml_smalls_data_model.get(), state.shell, state.workbench.managed_lists);
}

void ensure_active_dialog_document(ClientApplicationState& state)
{
    nw::toolset::ensure_active_dialog_document(state.dialog_view, state.backend.current_project_dir(), state.workspace.active_tab());
}

bool active_object_details_matches_tab(const ClientApplicationState& state)
{
    return nw::toolset::active_object_details_matches_tab(state.workbench, state.workspace);
}

bool active_object_matches_tab(const ClientApplicationState& state)
{
    return nw::toolset::active_object_matches_tab(state.workbench, state.workspace);
}

void configure_details_list(ClientApplicationState& state)
{
    nw::toolset::configure_details_list(state.workbench);
}

void close_object_details_combobox(
    Rml::ElementDocument* doc, ClientApplicationState& state)
{
    nw::toolset::close_object_details_combobox(doc, state.workbench);
}

void rebuild_active_object_details(ClientApplicationState& state, nw::ObjectHandle object)
{
    nw::toolset::rebuild_object_workbench_snapshots(state.workbench, object);
}

void clear_active_object_details(ClientApplicationState& state)
{
    nw::toolset::clear_object_workbench_snapshots(state.workbench);
    state.managed_list_reorder = {};
    nw::toolset::clear_object_workbench_children(state.workbench);
}

bool sync_active_module_object(ClientApplicationState& state)
{
    const auto* active_tab = state.workspace.active_tab();
    if (!active_tab || active_tab->kind != nw::toolset::WorkspaceTabKind::home) {
        return false;
    }

    const nw::ObjectHandle object = state.backend.module_object();
    if (object.type != nw::ObjectType::module) {
        if (state.workbench.active_object_tab_id == active_tab->id) {
            state.smalls.clear_active_object();
            state.workbench.active_object_tab_id.clear();
            clear_active_object_details(state);
        }
        return false;
    }

    const bool changed = state.workbench.object_details.object != object
        || state.workbench.active_object_tab_id != active_tab->id
        || state.workbench.object_details.status != nw::toolset::ObjectDetailsStatus::ready;
    state.smalls.publish_active_object(object);
    state.smalls.clear_active_area();
    state.workbench.active_object_tab_id = active_tab->id;
    if (!changed) {
        return true;
    }

    clear_active_object_details(state);
    configure_details_list(state);
    state.workbench.details_list.set_scroll_top(0);
    rebuild_active_object_details(state, object);
    state.observed_object_mutation_epoch = nw::toolset::object_mutation_state().epoch;
    return true;
}

bool sync_object_details_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    return nw::toolset::sync_object_details_window(doc, state.workbench, state.workspace, force);
}

bool sync_creature_feat_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    return nw::toolset::sync_creature_feat_window(doc, state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

void clear_creature_spell_filter(ClientApplicationState& state)
{
    nw::toolset::clear_creature_spell_filter(state.workbench.creature_view);
}

bool sync_creature_spell_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    return nw::toolset::sync_creature_spell_window(doc, state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

bool sync_creature_spell_filter_window(
    Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    return nw::toolset::sync_creature_spell_filter_window(doc, state.workbench.creature_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

bool active_creature_inventory_matches_tab(const ClientApplicationState& state)
{
    return nw::toolset::active_creature_inventory_matches_tab(state.workbench.inventory_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
}

bool sync_creature_inventory_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    return nw::toolset::sync_creature_inventory_window(doc, state.workbench.inventory_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

void close_appearance_selector(ClientApplicationState& state)
{
    nw::toolset::close_appearance_selector(state.workbench.appearance_view);
}

void rebuild_active_appearances(ClientApplicationState& state, nw::ObjectHandle object)
{
    nw::toolset::rebuild_active_appearances(state.workbench.appearance_view, state.backend.module_generation(), object);
}

bool active_appearances_match_tab(const ClientApplicationState& state)
{
    return nw::toolset::active_appearances_match_tab(state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
}

bool sync_appearance_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    return nw::toolset::sync_appearance_window(doc, state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), force);
}

void close_sound_resource_selector(ClientApplicationState& state)
{
    nw::toolset::close_sound_resource_selector(state.workbench.appearance_view);
}

bool sync_sound_catalog_window(
    Rml::ElementDocument* doc, ClientApplicationState& state, bool force)
{
    return nw::toolset::sync_sound_catalog_window(doc, state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace), state.backend_ready ? nw::kernel::resman().generation() : 0, force);
}

bool sync_appearance_body_preview(ClientRenderer& renderer, ClientApplicationState& state)
{
    return nw::toolset::sync_appearance_body_preview(renderer, state.workbench.appearance_view, nw::toolset::object_workbench_target(state.workbench, state.workspace));
}

ObjectWorkbenchChangeListener::ObjectWorkbenchChangeListener(ClientApplicationState& state)
    : state_(state)
{
}
void ObjectWorkbenchChangeListener::ProcessEvent(Rml::Event& event)
{
    nw::toolset::process_object_workbench_change(event, state_.workbench, state_.workspace,
        state_.backend, state_.shell, command_context(state_, nw::toolset::CommandSource::widget));
}
bool ObjectWorkbenchChangeListener::commit_sound_volume()
{
    return nw::toolset::commit_object_workbench_sound_volume(state_.workbench, state_.workspace,
        state_.backend, state_.shell, command_context(state_, nw::toolset::CommandSource::widget));
}
} // namespace nw::toolset::client_application_detail
