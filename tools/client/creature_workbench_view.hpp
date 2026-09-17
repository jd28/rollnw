#pragma once
#include "client_input_routes.hpp"
#include "object_workbench.hpp"
#include "smalls_creature_feats.hpp"
#include "smalls_creature_properties.hpp"
#include "smalls_creature_spells.hpp"
#include "virtual_combobox.hpp"
#include "virtual_list.hpp"
#include <RmlUi/Core/Types.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace Rml {
class ElementDocument;
class Element;
}
namespace nw::toolset {
class WorkspaceState;
class ToolsetBackend;
class ShellController;
struct CommandContext;

enum class CreatureWorkbenchCommandKind : uint8_t {
    none,
    class_level,
    feat,
    known_spell,
    memorized_spell,
};

// Schema 1: one selected UI edge; the fixed header owns semantic IDs/current
// values, followed by the owned tab identity. No DOM or provider-row borrow.
struct CreatureWorkbenchCommandClick {
    ObjectHandle object{};
    uint64_t module_generation = 0;
    uint64_t mutation_epoch = 0;
    int32_t key = -1;
    int32_t current = 0;
    int32_t delta = 0;
    int32_t class_id = -1;
    int32_t metamagic = -1;
    CreatureWorkbenchCommandKind kind = CreatureWorkbenchCommandKind::none;
    ClientRmlForwardPhase release_phase = ClientRmlForwardPhase::none;
    std::string tab_id;
};
enum class CreatureSpellFilterField : uint8_t {
    none,
    class_,
    level,
    metamagic,
};

// One displayed creature workbench owns copied SmallS presentation batches,
// query/match indices and cached visible ranges. DOM borrows last one call.
// Target facts are recaptured per call; unavailable/stale tabs render waiting.
struct CreatureWorkbenchViewState {
    nw::toolset::CreatureClassPresentationSnapshot creature_class_presentation;
    nw::toolset::CreatureFeatViewSnapshot creature_feats;
    nw::toolset::VirtualListController creature_feat_list;
    nw::toolset::CreatureSpellViewSnapshot creature_spells;
    std::vector<uint32_t> creature_spell_matches;
    nw::toolset::VirtualListController creature_spell_list;
    nw::toolset::VirtualComboBox creature_spell_combobox;
    std::string creature_feat_query;
    std::string creature_spell_query;
    int32_t creature_spell_level = -1;
    std::optional<nw::toolset::VirtualComboBoxPopupPlacement> creature_spell_popup_placement;
    CreatureSpellFilterField creature_spell_filter_field = CreatureSpellFilterField::none;
    bool creature_feat_list_configured = false;
    bool creature_feat_rendered = false;
    bool creature_spell_list_configured = false;
    bool creature_spell_rendered = false;
    nw::toolset::VirtualListRange rendered_creature_feat_range{};
    int rendered_creature_feat_row_count = 0;
    nw::toolset::VirtualListRange rendered_creature_spell_range{};
    int rendered_creature_spell_row_count = 0;
};

std::optional<CreatureWorkbenchCommandClick> capture_creature_workbench_command_click(
    Rml::Element* hit, const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, uint64_t module_generation);
bool execute_creature_workbench_command_click(CreatureWorkbenchCommandClick& click,
    const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, ToolsetBackend& backend,
    ShellController& shell, const CommandContext& context);
// Level is -1 (all) or 0..9. Class/metamagic must exist in current choices;
// invalid choices reject without rebuilding. SmallS retains rule ownership.
void configure_creature_feat_list(CreatureWorkbenchViewState& state);
void configure_creature_spell_list(CreatureWorkbenchViewState& state);
bool active_creature_feats_match_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target);
bool active_creature_feat_object_matches_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target);
void rebuild_active_creature_feats(CreatureWorkbenchViewState& state, nw::ObjectHandle object);
void clear_active_creature_feats(CreatureWorkbenchViewState& state);
bool sync_creature_feat_window(Rml::ElementDocument* doc, CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, bool force);
bool active_creature_spells_match_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target);
bool active_creature_spell_object_matches_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target);
bool active_creature_spell_filter_matches_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target);
void clear_creature_spell_filter(CreatureWorkbenchViewState& state);
void filter_active_creature_spells(CreatureWorkbenchViewState& state);
void rebuild_active_creature_spells(CreatureWorkbenchViewState& state,
    nw::ObjectHandle object,
    int32_t selected_class = -1,
    int32_t selected_metamagic = -1);
void clear_active_creature_spells(CreatureWorkbenchViewState& state);
std::optional<CreatureSpellFilterField> creature_spell_filter_field_from_name(
    std::string_view value) noexcept;
bool open_creature_spell_filter(CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, CreatureSpellFilterField field);
bool commit_creature_spell_filter(CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, int32_t value);
bool sync_creature_spell_window(Rml::ElementDocument* doc, CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, bool force);
bool sync_creature_spell_filter_window(
    Rml::ElementDocument* doc, CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, bool force);
void append_creature_spell_markup(std::string& markup, const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target);
void append_creature_classes_markup(std::string& markup, const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target);
void append_creature_workbench_overlay_markup(
    std::string& markup, const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target);
bool active_creature_class_presentation_matches_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target);
void rebuild_creature_class_presentation(CreatureWorkbenchViewState& state, nw::ObjectHandle object);
} // namespace nw::toolset
