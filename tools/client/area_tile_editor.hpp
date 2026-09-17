#pragma once

#include "../ui/virtual_list.hpp"
#include "area_tile_edits.hpp"
#include "area_tile_interaction.hpp"
#include "area_tile_palette.hpp"
#include "viewport_rect.hpp"

#include <RmlUi/Core/Types.h>
#include <nw/render/viewer/preview_scene.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

class ClientRenderer;

namespace Rml {
class Element;
class ElementDocument;
}

namespace nw::toolset {

class ShellController;
class ToolsetBackend;
struct CommandContext;

// One displayed editor owns palette textures, selection, stroke masks and
// preview rows. DOM/renderer borrows never enter this state. Stored indices are
// row-major cells/corners; a stroke fixes area identity and generations until
// commit/cancel. Invalid rows/grids are rejected by the existing operations.
struct AreaTileStrokeState {
    nw::ObjectHandle area{};
    nw::Resref tileset;
    AreaTileBrush brush;
    std::string label;
    uint64_t mutation_epoch = 0;
    uint64_t resource_generation = 0;
    int32_t width = 0;
    int32_t height = 0;
    AreaTileCellCoord last_target{};
    std::vector<uint8_t> visited;
    std::vector<uint8_t> previewed_tiles;
    std::vector<uint32_t> tile_indices;
    std::vector<uint32_t> corner_indices;
    uint8_t pointer_button = 0;
    bool active = false;
    bool has_last_target = false;
};

struct AreaTileEditorState {
    AreaTilePalette palette;
    VirtualListController list;
    VirtualListRange rendered_range{};
    AreaTileStrokeState stroke;
    AreaTileSelection selection;
    std::string feedback;
    std::string query;
    std::vector<nw::render::viewer::AreaTilePreviewRow> preview_rows;
    Rml::Vector2f pending_cursor_point{};
    uint32_t cursor_target_index = UINT32_MAX;
    // Cached presentation mode, not a second source of keyboard state.
    AreaTilePointerModifier cursor_modifier
        = AreaTilePointerModifier::none;
    uint64_t next_random_seed = 1;
    int rendered_row_count = 0;
    int32_t selected_row = -1;
    int32_t group_orientation = 0; // Owned invariant: quarter turns in [0, 3].
    bool list_configured = false;
    bool rendered = false;
    bool cursor_update_pending = false;
};

// Cold schema 1. One displayed palette click owns no DOM target. Existing
// palette rows remain indexed batches; SDK release precedes apply. Stale or
// malformed descriptors are consumed without changing editor state.
enum class AreaTilePaletteClickKind : uint8_t { none,
    back,
    folder,
    action };
enum class AreaTilePaletteClickEffect : uint8_t { none,
    folder_changed,
    selected,
    unavailable };
struct AreaTilePaletteClick {
    ObjectHandle area{};
    uint64_t resource_generation = 0;
    uint32_t folder = UINT32_MAX;
    uint32_t row = UINT32_MAX;
    uint32_t parent = UINT32_MAX;
    AreaTileBrush brush;
    AreaTilePaletteClickKind kind = AreaTilePaletteClickKind::none;
    std::string query;
};
std::optional<AreaTilePaletteClick> capture_area_tile_palette_click(Rml::Element* hit,
    const AreaTileEditorState& editor);
AreaTilePaletteClickEffect apply_area_tile_palette_click(AreaTilePaletteClick& click,
    AreaTileEditorState& editor, ObjectHandle active_area);

void reset_area_tile_palette_folder_view(AreaTileEditorState& editor);
[[nodiscard]] bool rebuild_area_tile_palette(AreaTileEditorState& editor, ObjectHandle area);
void sync_area_tile_selection_info(Rml::ElementDocument* document,
    const AreaTileEditorState& editor, ObjectHandle area);
bool sync_area_tile_palette_window(Rml::ElementDocument* document,
    AreaTileEditorState& editor, ObjectHandle area, bool tiles_visible,
    AreaTilePointerModifier modifier, bool force);
// One displayed query is a singleton; existing indexed palette rows are filtered
// in batches. Hidden surfaces retain state, missing fields mean an empty query.
void refresh_area_tile_palette_query(Rml::ElementDocument*, AreaTileEditorState&,
    ObjectHandle area, bool tiles_visible, AreaTilePointerModifier);
void append_area_tile_palette_markup(std::string& markup, const AreaTileEditorState& editor);
[[nodiscard]] bool reset_area_tile_editor(AreaTileEditorState& editor, ObjectHandle area);
// A valid group rotation invalidates the retained hover target so a stationary
// pointer is repicked. No brush/non-group/active stroke leaves rotation intact.
[[nodiscard]] bool rotate_area_tile_group_orientation(AreaTileEditorState& editor);
[[nodiscard]] std::optional<AreaTileBrush> selected_area_tile_brush(
    const AreaTileEditorState& editor, AreaTilePointerButton button) noexcept;
[[nodiscard]] bool area_tile_height_brush(AreaTileBrush brush) noexcept;
ObjectEditApplyResult build_area_tile_stroke_edits(ObjectHandle area,
    std::span<const uint32_t> tile_indices, std::span<const uint32_t> corner_indices,
    AreaTileBrush brush, uint64_t seed, AreaTileEditBatch& output);
[[nodiscard]] bool append_area_tile_height_preview_cells(
    AreaTileStrokeState& stroke, std::span<const uint32_t> corner_indices) noexcept;

// Renderer integration borrows current area/viewport/eligibility facts. It must
// not cache root input authority. The caller synchronizes a stale viewport
// before begin and keeps cross-editor cancellation ordered. Pointer misses
// clear hover previews; commit rejects changed area/generations/grid identity.
void clear_area_tile_selection(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area);
bool update_area_tile_selection_preview(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area);
bool select_area_tiles(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, Rml::Vector2f point, ClientViewportRect viewport, bool actions_allowed, ShellController& shell);
bool cycle_area_tile_at_point(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, Rml::Vector2f point, ClientViewportRect viewport, bool actions_allowed, ToolsetBackend& backend, const CommandContext& context, ShellController& shell);
void cancel_area_tile_stroke(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area);
bool cancel_area_tile_action(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area);
bool update_area_tile_cursor(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, Rml::Vector2f point, ClientViewportRect viewport, AreaTilePointerModifier modifier, ShellController& shell);
bool begin_area_tile_stroke(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, Rml::Vector2f point, ClientViewportRect viewport, uint8_t pointer_button, AreaTileBrush brush, AreaTilePointerModifier modifier, bool actions_allowed, ShellController& shell);
void commit_area_tile_stroke(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, bool actions_allowed, bool viewport_stale, ToolsetBackend& backend, const CommandContext& context, ShellController& shell);
bool cycle_selected_area_tile_variation(ClientRenderer& renderer, AreaTileEditorState& editor, ObjectHandle active_area, bool actions_allowed, ToolsetBackend& backend, const CommandContext& context, ShellController& shell);
bool prepare_area_tile_cursor_update(ClientRenderer& renderer, AreaTileEditorState& editor,
    ObjectHandle active_area, AreaTilePointerModifier modifier);
void clear_area_tile_cursor_target(ClientRenderer& renderer, AreaTileEditorState& editor,
    ObjectHandle active_area, AreaTilePointerModifier modifier);
bool rotate_area_tile_group(ClientRenderer& renderer, AreaTileEditorState& editor,
    ObjectHandle active_area, std::optional<ClientViewportRect> viewport,
    AreaTilePointerModifier modifier, ShellController& shell);
} // namespace nw::toolset
