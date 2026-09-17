#pragma once

#include "appearance_catalog.hpp"
#include "client_input_routes.hpp"
#include "object_workbench.hpp"
#include "sound_catalog.hpp"
#include "virtual_list.hpp"

#include <RmlUi/Core/Types.h>

#include <array>
#include <limits>
#include <optional>

class ClientRenderer;
namespace Rml {
class Context;
class Element;
class ElementDocument;
}

struct SDL_KeyboardEvent;

namespace nw::toolset {
class ToolsetBackend;
class ShellController;
struct CommandContext;
class WorkspaceState;

enum class SoundResourceClickKind : uint8_t {
    none,
    open,
    close,
    select,
};

// Cold in-process schema 1. Owning tab/query/resource data survive SDK release;
// the dense row alone never identifies a sound. One selector edge is a true
// singleton. Malformed matched controls still require before-native release.
struct SoundResourceClick {
    ObjectHandle object{};
    Resref resource;
    uint64_t module_generation = 0;
    uint64_t resource_generation = 0;
    uint64_t mutation_epoch = 0;
    int32_t row = -1;
    SoundResourceClickKind kind = SoundResourceClickKind::none;
    ClientRmlForwardPhase release_phase = ClientRmlForwardPhase::before_native;
    std::string tab_id;
    std::string query;
};

enum class SoundResourceClickEffect : uint8_t {
    none,
    opened,
    closed,
};

enum class ColorEditorClickKind : uint8_t { none,
    close,
    channel,
    select,
    field,
    selector };

// Cold in-process schema 1. No DOM/geometry/provider-row borrow survives capture.
// One selector edge is singular because SDK callbacks can change its owner.
struct ColorEditorClick {
    ObjectHandle object{};
    ObjectHandle editor_object{};
    uint64_t module_generation = 0;
    uint64_t resource_generation = 0;
    uint64_t mutation_epoch = 0;
    uint32_t channel = 0;
    int32_t editor_channel = -1;
    int32_t source_value = -1;
    int32_t palette = -1;
    int32_t selected = -1;
    ColorEditorClickKind kind = ColorEditorClickKind::none;
    ClientRmlForwardPhase release_phase = ClientRmlForwardPhase::before_native;
    std::string tab_id;
};

inline constexpr int kPltPaletteColumns = 16;
inline constexpr int kPltPaletteRows = 11;
inline constexpr int kPltPaletteCellPx = 24;

enum class AppearanceEditorField : uint8_t {
    appearance,
    wings,
    tail,
};

enum class AppearanceCatalogClickKind : uint8_t { none,
    back,
    previous,
    next,
    open,
    select };
enum class AppearanceCatalogClickEffect : uint8_t { none,
    refresh,
    opened };

// Cold in-process schema 1: copied semantic ID and owning UTF-8 tab/query payload.
// Source values are [appearance kind, generic type] for Door, [field value, -1]
// otherwise. Missing values compare as unavailable; never an implicit zero.
struct AppearanceCatalogClick {
    ObjectHandle object{};
    uint64_t module_generation = 0;
    uint64_t resource_generation = 0;
    uint64_t mutation_epoch = 0;
    std::optional<std::array<int32_t, 2>> source_values;
    int32_t selected = -1;
    AppearanceEditorField field = AppearanceEditorField::appearance;
    AppearanceEditorField previous_field = AppearanceEditorField::appearance;
    AppearanceCatalogClickKind kind = AppearanceCatalogClickKind::none;
    bool selector_open = false;
    ClientRmlForwardPhase release_phase = ClientRmlForwardPhase::before_native;
    std::string tab_id;
    std::string query;
};

// One displayed selector owns copied catalog batches and dense match indices,
// current query/scroll/color channel and body-preview identity. Module/resource
// generations are current facts, not retained backend borrows. DOM borrows last
// one call. Providers drop invalid catalog rows; unavailable data renders its
// diagnostic. Color opening rejects unsupported channels/palettes/values;
// stale target checks reject selections. Backend retains edit/rule ownership.
struct AppearanceViewState {
    nw::toolset::AppearanceCatalog creature_appearance_catalog;
    nw::toolset::AppearanceCatalog placeable_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::placeable};
    nw::toolset::AppearanceCatalog door_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::door};
    nw::toolset::AppearanceCatalog wing_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::wing};
    nw::toolset::AppearanceCatalog tail_appearance_catalog{
        .kind = nw::toolset::AppearanceCatalogKind::tail};
    std::vector<uint32_t> appearance_matches;
    nw::toolset::VirtualListController appearance_list;
    nw::toolset::SoundCatalog sound_catalog;
    std::vector<uint32_t> sound_catalog_matches;
    nw::toolset::VirtualListController sound_catalog_list;
    std::string appearance_query;
    std::string sound_catalog_query;
    nw::ObjectHandle appearance_object{};
    nw::ObjectHandle appearance_body_preview_object{};
    nw::ObjectHandle color_editor_object{};
    int32_t color_editor_channel = -1;
    AppearanceEditorField appearance_editor_field = AppearanceEditorField::appearance;
    uint64_t appearance_catalog_generation = std::numeric_limits<uint64_t>::max();
    uint64_t sound_catalog_generation = std::numeric_limits<uint64_t>::max();
    float appearance_editor_scroll_top = 0.0f;
    bool appearance_list_configured = false;
    bool appearance_rendered = false;
    bool appearance_scroll_to_selection = false;
    bool appearance_selector_open = false;
    bool sound_catalog_list_configured = false;
    bool sound_catalog_rendered = false;
    bool sound_resource_selector_open = false;
    nw::toolset::VirtualListRange rendered_appearance_range{};
    int rendered_appearance_row_count = 0;
    nw::toolset::VirtualListRange rendered_sound_catalog_range{};
    int rendered_sound_catalog_row_count = 0;
};

std::optional<SoundResourceClick> capture_sound_resource_click(
    Rml::Element* hit, const AppearanceViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, uint64_t module_generation, uint64_t resource_generation);
// Caller performs before-native release/common-owner validation and, for open,
// closes the shared SmallS selector first. Current owners are rechecked here.
// Consumes once; only committed selections close/request content refresh.
SoundResourceClickEffect apply_sound_resource_click(SoundResourceClick& click,
    AppearanceViewState& state, ObjectWorkbenchTarget target, const WorkspaceState& workspace,
    ToolsetBackend& backend, ShellController& shell, const CommandContext& context);

std::optional<ColorEditorClick> capture_color_editor_click(Rml::Element* hit, Rml::Vector2f point,
    const AppearanceViewState& state, ObjectWorkbenchTarget target, const WorkspaceState& workspace,
    uint64_t module_generation, uint64_t resource_generation);
// Channel applies before SDK release; others apply after. Field caller closes
// the shared SmallS selector after release, then this rechecks the live owner.
// Consumes once and returns whether the existing content refresh is required.
bool apply_color_editor_click(ColorEditorClick& click, AppearanceViewState& state,
    ObjectWorkbenchTarget target, const WorkspaceState& workspace, ToolsetBackend& backend,
    ShellController& shell, const CommandContext& context);

std::optional<AppearanceCatalogClick> capture_appearance_back_click(Rml::Element* hit,
    const AppearanceViewState& state, ObjectWorkbenchTarget target, const WorkspaceState& workspace,
    uint64_t module_generation, uint64_t resource_generation);
std::optional<AppearanceCatalogClick> capture_appearance_catalog_click(Rml::Element* hit,
    const AppearanceViewState& state, ObjectWorkbenchTarget target, const WorkspaceState& workspace,
    uint64_t module_generation, uint64_t resource_generation);
// Before-native SDK release and shared-selector close precede apply. Checks
// current ownership and values, consumes once, and returns presentation intent.
AppearanceCatalogClickEffect apply_appearance_catalog_click(AppearanceCatalogClick& click,
    AppearanceViewState& state, ObjectWorkbenchTarget target, const WorkspaceState& workspace,
    ToolsetBackend& backend, ShellController& shell, const CommandContext& context);

// Current displayed selector is singular. Root retains palette exclusion and
// Escape precedence. No keyboard event or SDK pointer is retained across calls.
enum class AppearanceSelectorKeyEffect : uint8_t { none,
    handled,
    content_changed };
AppearanceSelectorKeyEffect handle_appearance_selector_key(const SDL_KeyboardEvent&,
    Rml::Context*, Rml::ElementDocument*, AppearanceViewState&, ObjectWorkbenchTarget,
    uint64_t resource_generation, ToolsetBackend&, ShellController&, const CommandContext&);
bool close_appearance_selector_for_escape(AppearanceViewState&, ObjectWorkbenchTarget,
    uint64_t module_generation);

std::optional<nw::toolset::AppearanceCatalogKind> appearance_catalog_kind(nw::ObjectType type);
const nw::toolset::AppearanceCatalog& active_appearance_catalog(const AppearanceViewState& state);
void clear_color_editor(AppearanceViewState& state);
void close_appearance_selector(AppearanceViewState& state);
void rebuild_active_appearances(AppearanceViewState& state, uint64_t module_generation, nw::ObjectHandle object);
void clear_active_appearances(AppearanceViewState& state);
bool active_appearances_match_tab(const AppearanceViewState& state, ObjectWorkbenchTarget target);
bool active_color_editor_matches_tab(const AppearanceViewState& state, ObjectWorkbenchTarget target);
bool open_color_editor(AppearanceViewState& state, nw::ObjectHandle object, uint32_t color);
bool sync_appearance_window(Rml::ElementDocument* doc, AppearanceViewState& state, ObjectWorkbenchTarget target, bool force);
void close_sound_resource_selector(AppearanceViewState& state);
void clear_active_sound_catalog(AppearanceViewState& state);
bool active_sound_resource_selector_matches_tab(const AppearanceViewState& state, ObjectWorkbenchTarget target);
void rebuild_sound_catalog(AppearanceViewState& state, uint64_t resource_generation, bool reset_selection);
bool sync_sound_catalog_window(
    Rml::ElementDocument* doc, AppearanceViewState& state, ObjectWorkbenchTarget target, uint64_t resource_generation, bool force);
bool commit_sound_catalog_selection(AppearanceViewState& state, ObjectWorkbenchTarget target, ToolsetBackend& backend, ShellController& shell, const CommandContext& context, uint32_t row_index);
std::optional<AppearanceEditorField> appearance_editor_field_from_name(
    std::string_view field) noexcept;
void append_appearance_catalog_field_markup(std::string& content_markup,
    const AppearanceViewState& state,
    AppearanceEditorField field,
    std::optional<int32_t> current = std::nullopt);
void append_appearance_selector_markup(std::string& content_markup, const AppearanceViewState& state);
void append_sound_resource_selector_markup(
    std::string& content_markup, const AppearanceViewState& state);
void append_placeable_appearance_markup(
    std::string& content_markup, const AppearanceViewState& state);
void append_door_appearance_markup(
    std::string& content_markup, const AppearanceViewState& state);
void append_creature_accessories_markup(
    std::string& content_markup, const AppearanceViewState& state, ObjectWorkbenchTarget target);
void append_creature_colors_markup(std::string& content_markup, ObjectWorkbenchTarget target);
void append_creature_color_selector_markup(std::string& content_markup, const AppearanceViewState& state, ObjectWorkbenchTarget target);
bool commit_active_appearance_selection(AppearanceViewState& state, ToolsetBackend& backend, ShellController& shell, const CommandContext& context, int32_t value);
bool cycle_active_appearance(AppearanceViewState& state, ObjectWorkbenchTarget target, ToolsetBackend& backend, ShellController& shell, const CommandContext& context, int direction);
bool commit_active_color_selection(AppearanceViewState& state, ObjectWorkbenchTarget target, ToolsetBackend& backend, ShellController& shell, const CommandContext& context, int32_t value);
bool update_appearance_preview_rows(nw::ObjectHandle object, bool equipment_visible);
bool sync_appearance_body_preview(ClientRenderer& renderer, AppearanceViewState& state, ObjectWorkbenchTarget target);

} // namespace nw::toolset
