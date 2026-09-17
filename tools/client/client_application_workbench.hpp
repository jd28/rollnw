#pragma once
#include "client_application_state.hpp"
#include "renderer.hpp"
#include <RmlUi_Platform_SDL.h>

#include "smalls_view.hpp"
#include <RmlUi/Core/EventListener.h>

namespace nw::toolset::client_application_detail {

// Private composition functions. Feature APIs receive only their own states.
class ObjectWorkbenchChangeListener final : public Rml::EventListener {
public:
    explicit ObjectWorkbenchChangeListener(ClientApplicationState& state);
    void ProcessEvent(Rml::Event&) override;
    bool commit_sound_volume();

private:
    ClientApplicationState& state_;
};

void hide_object_variable_warning_tooltip(
    Rml::ElementDocument* doc, ClientApplicationState& state);

void sync_object_variable_warning_tooltip(Rml::ElementDocument* doc,
    ClientApplicationState& state,
    Rml::Element* hit,
    Rml::Vector2f point,
    int viewport_width,
    int viewport_height);

void dispatch_managed_list_events(ClientApplicationState& state);

bool synchronize_smalls_runtime(ClientApplicationState& state);

void refresh_smalls_elements(Rml::ElementDocument* document, ClientApplicationState& state);

nw::toolset::SmallsListActivation activate_managed_list(Rml::ElementDocument* document, ClientApplicationState& state, Rml::Element* hit);

bool cycle_managed_list(Rml::ElementDocument* document, ClientApplicationState& state, Rml::Element* element, int delta);

void ensure_active_dialog_document(ClientApplicationState& state);

bool active_object_details_matches_tab(const ClientApplicationState& state);

bool active_object_matches_tab(const ClientApplicationState& state);

void configure_details_list(ClientApplicationState& state);

void close_object_details_combobox(
    Rml::ElementDocument* doc, ClientApplicationState& state);

void rebuild_active_object_details(ClientApplicationState& state, nw::ObjectHandle object);

void clear_active_object_details(ClientApplicationState& state);

bool sync_active_module_object(ClientApplicationState& state);

bool sync_object_details_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

bool sync_creature_feat_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

void clear_creature_spell_filter(ClientApplicationState& state);

bool sync_creature_spell_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

bool sync_creature_spell_filter_window(
    Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

bool active_creature_inventory_matches_tab(const ClientApplicationState& state);

bool sync_creature_inventory_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

void close_appearance_selector(ClientApplicationState& state);

void rebuild_active_appearances(ClientApplicationState& state, nw::ObjectHandle object);

bool active_appearances_match_tab(const ClientApplicationState& state);

bool sync_appearance_window(Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

void close_sound_resource_selector(ClientApplicationState& state);

bool sync_sound_catalog_window(
    Rml::ElementDocument* doc, ClientApplicationState& state, bool force);

bool sync_appearance_body_preview(ClientRenderer& renderer, ClientApplicationState& state);

} // namespace nw::toolset::client_application_detail
