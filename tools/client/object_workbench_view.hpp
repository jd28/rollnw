#pragma once

#include "appearance_view.hpp"
#include "client_input_routes.hpp"
#include "creature_workbench_view.hpp"
#include "inventory_workbench_view.hpp"
#include "object_edits.hpp"
#include "object_workbench.hpp"
#include "rml_managed_list.hpp"
#include "smalls_creature_properties.hpp"
#include "virtual_combobox.hpp"
#include "virtual_list.hpp"

#include <RmlUi/Core/Types.h>
#include <optional>
#include <string>

namespace Rml {
class Element;
class ElementDocument;
class Event;
}
namespace nw::toolset {
class WorkspaceState;
class ToolsetBackend;
class ShellController;
struct WorkspaceTab;
struct CommandContext;
struct ObjectWorkbenchViewState;

enum class ObjectWorkbenchCommandKind : uint8_t {
    none,
    variable_add,
    variable_remove,
    variable_type,
    integer_step,
    boolean,
    door_state,
};

// Cold in-process click protocol schema 1: fixed owner/property header and
// owning UTF-8 tab/CommandArgs payload. No DOM or presentation-text borrow lasts
// across SDK dispatch. Only a value's propset/field/element/editor identifies it;
// the displayed dense row token alone does not. Source rows are SmallS-owned.
struct ObjectWorkbenchCommandRow {
    smalls::TypeID propset_type{};
    uint32_t field_index = UINT32_MAX;
    int32_t element_index = -1;
    uint32_t row = UINT32_MAX;
    int32_t current = 0;
    ObjectDetailsEditorKind editor = ObjectDetailsEditorKind::read_only;
};

struct ObjectWorkbenchCommandClick {
    ObjectHandle object{};
    uint64_t module_generation = 0;
    ObjectWorkbenchCommandKind kind = ObjectWorkbenchCommandKind::none;
    ClientRmlForwardPhase release_phase = ClientRmlForwardPhase::none;
    std::optional<ObjectWorkbenchCommandRow> property;
    std::string tab_id;
    CommandArgs args;
};

// One displayed UI click is a true singleton. A matched invalid control returns
// a kind=none descriptor so the root preserves its original release/consume path.
std::optional<ObjectWorkbenchCommandClick> capture_object_workbench_command_click(
    Rml::Element* hit, const ObjectWorkbenchViewState&, const WorkspaceState&,
    uint64_t module_generation);
// Consumes kind before calling the backend; repeats/stale owners/metadata reject
// without edits or logs. Existing backend commands retain their policy and undo.
bool execute_object_workbench_command_click(ObjectWorkbenchCommandClick&,
    const ObjectWorkbenchViewState&, const WorkspaceState&, ToolsetBackend&,
    ShellController&, const CommandContext&);

// A single displayed workbench owns copied row batches and its current tab ID.
// No DOM pointer survives presentation calls. Engine handles keep their existing
// validated identity contract; visible rows use dense indices/ranges.
struct PendingSoundVolume {
    nw::ObjectHandle object;
    std::string tab_id;
    uint64_t module_generation = 0;
    uint32_t row = 0;
    int32_t current = 0;
    int32_t desired = 0;
};

struct ObjectWorkbenchViewState {
    CreatureWorkbenchViewState creature_view;
    InventoryWorkbenchViewState inventory_view;
    AppearanceViewState appearance_view;
    ManagedListRenderState managed_lists;
    nw::toolset::ObjectDetailsSnapshot object_details;
    nw::toolset::VirtualListController details_list;
    nw::toolset::VirtualComboBox object_details_combobox{
        nw::toolset::VirtualComboBoxConfig{
            .row_height = 30,
            .visible_rows = 3,
            .overscan = 0,
        }};
    std::optional<uint32_t> object_details_combobox_row;
    std::optional<nw::toolset::VirtualComboBoxPopupPlacement>
        object_details_combobox_placement;
    nw::toolset::ObjectVariableSnapshot object_variables;
    nw::toolset::VirtualListController object_variable_list;
    std::string active_object_tab_id;
    ObjectWorkbenchSurface object_workbench_surface = ObjectWorkbenchSurface::details;
    std::string active_object_variable_warning;
    float object_workbench_tab_scroll_x = 0.0f;
    bool object_workbench_tab_scroll_pending = false;
    bool details_list_configured = false;
    bool details_rendered = false;
    bool object_variable_list_configured = false;
    bool object_variables_rendered = false;
    nw::toolset::VirtualListRange rendered_details_range{};
    int rendered_details_row_count = 0;
    nw::toolset::VirtualListRange rendered_object_variable_range{};
    int rendered_object_variable_row_count = 0;
    std::optional<PendingSoundVolume> pending_sound_volume;
    bool suppress_blur_commit = false;
};

// Surface edges are genuinely singular. The scalar request is consumed before
// synchronous selector-close callbacks; no DOM borrow survives capture.
struct ObjectWorkbenchSurfaceClick {
    std::optional<ObjectWorkbenchSurface> surface;
    bool pending = true;
};
std::optional<ObjectWorkbenchSurfaceClick> capture_object_workbench_surface_click(Rml::Element* hit);
// Caller verifies the common UI owner after SDK release. Presentation only;
// renderer/body and cross-view refresh remain composition responsibilities.
bool apply_object_workbench_surface_click(ObjectWorkbenchSurfaceClick& click,
    ObjectWorkbenchViewState& state, Rml::ElementDocument* document, ToolsetBackend& backend);
// DOM borrows last one call; returns immediately after the first close dispatch,
// which may replace those nodes. RmlUi's control protocol requires live pointers.
bool close_active_smalls_selector(Rml::ElementDocument* document);

// Active tab must be home/area/preview with matching ID and ready snapshots.
bool active_tab_has_object_workbench(const WorkspaceTab* tab);
ObjectWorkbenchTarget object_workbench_target(const ObjectWorkbenchViewState&, const WorkspaceState&);
bool active_object_details_matches_tab(const ObjectWorkbenchViewState&, const WorkspaceState&);
bool active_object_matches_tab(const ObjectWorkbenchViewState&, const WorkspaceState&);
bool active_object_variables_match_tab(const ObjectWorkbenchViewState&, const WorkspaceState&);
size_t active_details_row_count(const ObjectWorkbenchViewState&, const WorkspaceState&);
void configure_details_list(ObjectWorkbenchViewState&);
void configure_object_variable_list(ObjectWorkbenchViewState&);
void invalidate_details_render(ObjectWorkbenchViewState&);
void invalidate_object_variable_render(ObjectWorkbenchViewState&);
void rebuild_object_workbench_snapshots(ObjectWorkbenchViewState&, nw::ObjectHandle object);
void clear_object_workbench_snapshots(ObjectWorkbenchViewState&);
void clear_object_details_combobox_state(ObjectWorkbenchViewState&);
void close_object_details_combobox(Rml::ElementDocument*, ObjectWorkbenchViewState&);
bool open_object_details_sound_position_combobox(Rml::ElementDocument*, ObjectWorkbenchViewState&, const WorkspaceState&, uint32_t row);
bool sync_object_details_combobox(Rml::ElementDocument*, ObjectWorkbenchViewState&, const WorkspaceState&, bool force = false);
bool sync_object_variable_window(Rml::ElementDocument*, ObjectWorkbenchViewState&, const WorkspaceState&, bool force);
bool sync_object_details_window(Rml::ElementDocument*, ObjectWorkbenchViewState&, const WorkspaceState&, bool force);
void hide_object_variable_warning_tooltip(Rml::ElementDocument*, ObjectWorkbenchViewState&);
void sync_object_variable_warning_tooltip(Rml::ElementDocument*, ObjectWorkbenchViewState&, bool palette_visible,
    Rml::Element* hit, Rml::Vector2f point, int width, int height);
// Current command context is captured for this synchronous event, not retained.
// The root's thin listener adapter supplies it again on reentrant blur. Sound
// gestures retain object/tab/module identity and discard stale/invalid targets.
void process_object_workbench_change(Rml::Event&, ObjectWorkbenchViewState&, const WorkspaceState&,
    ToolsetBackend&, ShellController&, const CommandContext&);
bool commit_object_workbench_sound_volume(ObjectWorkbenchViewState&, const WorkspaceState&,
    ToolsetBackend&, ShellController&, const CommandContext&);
bool commit_object_details_sound_position(Rml::ElementDocument*, ObjectWorkbenchViewState&, const WorkspaceState&,
    ToolsetBackend&, ShellController&, const CommandContext&, int32_t desired);

// One displayed object's activation resets original scroll/page/surface state;
// switching creatures retains existing feat/spell queries. Mutation refresh keeps
// source selection/page and class/metamagic. Stale mutation objects reject.
void activate_object_workbench(ObjectWorkbenchViewState&, ObjectHandle object, std::string_view tab_id);
bool refresh_object_workbench_snapshots(ObjectWorkbenchViewState&, ObjectHandle object);
void clear_object_workbench_children(ObjectWorkbenchViewState&);
void hydrate_object_workbench(Rml::ElementDocument*, const ObjectWorkbenchViewState&, const WorkspaceState&);
void append_object_workbench_markup(std::string&, const ObjectWorkbenchViewState&, const WorkspaceState&, const ToolsetBackend&);
} // namespace nw::toolset
