#include "area_tile_editor.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nw::toolset {
namespace {

constexpr int kAreaTilePaletteRowHeightPx = 58;
constexpr int kAreaTilePaletteOverscanRows = 3;

Rml::Element* find_el(Rml::ElementDocument* document, const char* id)
{
    return document ? document->GetElementById(id) : nullptr;
}

void configure_area_tile_palette_list(AreaTileEditorState& editor)
{
    if (editor.list_configured) {
        return;
    }
    editor.list.set_row_height(kAreaTilePaletteRowHeightPx);
    editor.list.set_overscan(kAreaTilePaletteOverscanRows);
    editor.list_configured = true;
}

class AreaTilePaletteListAdapter final
    : public nw::toolset::VirtualListAdapter {
public:
    explicit AreaTilePaletteListAdapter(
        const nw::toolset::AreaTilePalette& palette)
        : palette_{palette}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(palette_.matches.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        return static_cast<int>(
            palette_.matches[static_cast<size_t>(index)]);
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "area_tile_palette_row";
    }

    [[nodiscard]] std::string render_row_inner(
        int index, bool /*selected*/) const override
    {
        const auto& value = row(index);
        std::string markup;
        markup.reserve(
            value.label.size() + value.thumbnail_source.size() + 160);
        markup += "<div class=\"area_tile_palette_thumbnail";
        if (value.kind == nw::toolset::AreaTilePaletteRowKind::folder) {
            markup += " area_tile_palette_folder_indicator\">";
            markup += "<span class=\"area_tile_palette_folder_glyph\">›</span>";
        } else {
            markup += "\">";
            if (!value.thumbnail_source.empty()) {
                markup += "<img src=\"";
                markup += Rml::StringUtilities::EncodeRml(value.thumbnail_source);
                markup += "\"/>";
            } else {
                std::string_view glyph;
                switch (value.brush.kind) {
                case nw::toolset::AreaTileBrushKind::terrain:
                    glyph = "T";
                    break;
                case nw::toolset::AreaTileBrushKind::crosser:
                    glyph = "~";
                    break;
                case nw::toolset::AreaTileBrushKind::group:
                    glyph = "F";
                    break;
                case nw::toolset::AreaTileBrushKind::eraser:
                    glyph = "E";
                    break;
                case nw::toolset::AreaTileBrushKind::raise:
                    glyph = "+/-";
                    break;
                case nw::toolset::AreaTileBrushKind::lower:
                    glyph = "-";
                    break;
                }
                markup += "<span class=\"area_tile_palette_action_glyph\">";
                markup += glyph;
                markup += "</span>";
            }
        }
        markup += "</div><div class=\"area_tile_palette_text\">"
                  "<div class=\"area_tile_palette_action_name\">";
        markup += Rml::StringUtilities::EncodeRml(value.label);
        markup += "</div></div>";
        return markup;
    }

private:
    [[nodiscard]] const nw::toolset::AreaTilePaletteRow& row(
        int index) const
    {
        return palette_.rows[palette_.matches[static_cast<size_t>(index)]];
    }

    const nw::toolset::AreaTilePalette& palette_;
};

} // namespace

std::optional<AreaTilePaletteClick> capture_area_tile_palette_click(Rml::Element* hit,
    const AreaTileEditorState& editor)
{
    Rml::Element* control = nullptr;
    bool back = false;
    for (auto* element = hit; element; element = element->GetParentNode()) {
        if (element->GetId() == "area_tile_editor_back") {
            control = element;
            back = true;
            break;
        }
    }
    if (!control) {
        for (auto* element = hit; element; element = element->GetParentNode()) {
            if (element->IsClassSet("area_tile_palette_row")) {
                control = element;
                break;
            }
        }
    }
    if (!control) { return std::nullopt; }
    AreaTilePaletteClick click;
    const auto& palette = editor.palette;
    if (palette.status != AreaTilePaletteStatus::ready) { return click; }
    click.area = palette.area;
    click.resource_generation = palette.resource_generation;
    click.folder = palette.current_folder;
    click.query = editor.query;
    if (back) {
        click.kind = AreaTilePaletteClickKind::back;
        return click;
    }
    const auto key = control->GetAttribute<Rml::String>("data-key", "");
    int32_t row = -1;
    const auto parsed = std::from_chars(key.data(), key.data() + key.size(), row);
    if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || row < 0
        || static_cast<size_t>(row) >= palette.rows.size()) { return click; }
    const auto& source = palette.rows[static_cast<size_t>(row)];
    if (source.kind > AreaTilePaletteRowKind::action) { return click; }
    click.row = static_cast<uint32_t>(row);
    click.parent = source.parent;
    click.brush = source.brush;
    click.kind = source.kind == AreaTilePaletteRowKind::folder ? AreaTilePaletteClickKind::folder : AreaTilePaletteClickKind::action;
    return click;
}

AreaTilePaletteClickEffect apply_area_tile_palette_click(AreaTilePaletteClick& click,
    AreaTileEditorState& editor, ObjectHandle active_area)
{
    const auto kind = std::exchange(click.kind, AreaTilePaletteClickKind::none);
    auto& palette = editor.palette;
    if (kind == AreaTilePaletteClickKind::none || kind > AreaTilePaletteClickKind::action
        || palette.status != AreaTilePaletteStatus::ready || palette.area != click.area || active_area != click.area
        || !kernel::objects().valid(click.area) || palette.resource_generation != click.resource_generation
        || kernel::resman().generation() != click.resource_generation || palette.current_folder != click.folder
        || editor.query != click.query) { return AreaTilePaletteClickEffect::none; }
    if (kind == AreaTilePaletteClickKind::back) {
        if (leave_area_tile_palette_folder(palette)) { return AreaTilePaletteClickEffect::folder_changed; }
        editor.feedback = "Tile palette navigation is unavailable";
        return AreaTilePaletteClickEffect::unavailable;
    }
    if (click.row >= palette.rows.size() || click.row > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) { return AreaTilePaletteClickEffect::none; }
    const auto& row = palette.rows[click.row];
    const auto expected = kind == AreaTilePaletteClickKind::folder ? AreaTilePaletteRowKind::folder : AreaTilePaletteRowKind::action;
    if (row.kind != expected || row.parent != click.parent || row.brush.kind != click.brush.kind
        || row.brush.value != click.brush.value || row.brush.orientation != click.brush.orientation) { return AreaTilePaletteClickEffect::none; }
    if (kind == AreaTilePaletteClickKind::folder) {
        if (enter_area_tile_palette_folder(palette, click.row)) { return AreaTilePaletteClickEffect::folder_changed; }
        editor.feedback = "Tile palette folder is unavailable";
        return AreaTilePaletteClickEffect::unavailable;
    }
    editor.selected_row = static_cast<int32_t>(click.row);
    editor.group_orientation = 0;
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_update_pending = false;
    editor.preview_rows.clear();
    editor.feedback.clear();
    const auto selected = std::ranges::find(palette.matches, click.row);
    if (selected != palette.matches.end()) { editor.list.set_selected(static_cast<int>(selected - palette.matches.begin())); }
    editor.rendered = false;
    return AreaTilePaletteClickEffect::selected;
}

void reset_area_tile_palette_folder_view(AreaTileEditorState& editor)
{
    editor.feedback.clear();
    editor.query.clear();
    editor.selected_row = -1;
    editor.list.set_selected(-1);
    editor.list.set_scroll_top(0);
    editor.list.set_total_rows(
        static_cast<int>(editor.palette.matches.size()));
    editor.rendered = false;
}

bool rebuild_area_tile_palette(AreaTileEditorState& editor, nw::ObjectHandle area)
{
    if (!nw::toolset::build_area_tile_palette(area, editor.palette)) {
        editor.list.set_total_rows(0);
        editor.rendered = false;
        return false;
    }
    if (!nw::toolset::filter_area_tile_palette(
            editor.palette, editor.query)) {
        editor.list.set_total_rows(0);
        editor.rendered = false;
        return false;
    }
    configure_area_tile_palette_list(editor);
    editor.list.set_total_rows(
        static_cast<int>(editor.palette.matches.size()));
    const auto selected = std::find(
        editor.palette.matches.begin(), editor.palette.matches.end(),
        static_cast<uint32_t>(editor.selected_row));
    if (selected == editor.palette.matches.end()) {
        editor.selected_row = -1;
        editor.list.set_selected(-1);
    } else {
        editor.list.set_selected(static_cast<int>(
            std::distance(editor.palette.matches.begin(), selected)));
    }
    editor.rendered = false;
    return true;
}

void sync_area_tile_selection_info(
    Rml::ElementDocument* doc, const AreaTileEditorState& editor, nw::ObjectHandle area_handle)
{
    auto* element = find_el(doc, "area_tile_selection_info");
    if (!element) {
        return;
    }
    const auto& selection = editor.selection;
    const auto* area = nw::kernel::objects().get<nw::Area>(area_handle);
    if (!selection.active() || selection.area != area_handle || !area
        || !area->tileset || area->width <= 0 || area->height <= 0
        || selection.source_tile_index >= area->tiles.size()) {
        element->SetInnerRML("");
        element->SetClass("visible", false);
        return;
    }

    const auto& tile = area->tiles[selection.source_tile_index];
    const uint32_t x = selection.source_tile_index
        % static_cast<uint32_t>(area->width);
    const uint32_t y = selection.source_tile_index
        / static_cast<uint32_t>(area->width);
    std::string_view label = "Tileset Group";
    if (selection.is_group()) {
        const auto row = std::ranges::find_if(editor.palette.rows,
            [&selection](const auto& value) {
                return value.kind
                    == nw::toolset::AreaTilePaletteRowKind::action
                    && value.brush.kind
                    == nw::toolset::AreaTileBrushKind::group
                    && value.brush.value
                    == static_cast<int32_t>(selection.group_index);
            });
        if (row != editor.palette.rows.end()) {
            label = row->label;
        }
    } else if (tile.id >= 0
        && static_cast<size_t>(tile.id) < area->tileset->tiles.size()) {
        label = area->tileset->tiles[static_cast<size_t>(tile.id)].model;
    } else {
        label = "Area Tile";
    }

    std::string markup;
    markup.reserve(label.size() + 180);
    markup += "<div class=\"area_tile_selection_title\">Selected ";
    markup += selection.is_group() ? "Group" : "Tile";
    markup += "</div><div class=\"area_tile_selection_name\">";
    markup += Rml::StringUtilities::EncodeRml(Rml::String{label});
    markup += "</div><div class=\"area_tile_selection_meta\">Cell ";
    markup += std::to_string(x);
    markup += ", ";
    markup += std::to_string(y);
    markup += " · Tile ";
    markup += std::to_string(tile.id);
    markup += " · Height ";
    markup += std::to_string(tile.height);
    markup += " · Rotation ";
    markup += std::to_string(tile.orientation * 90);
    markup += "°";
    if (selection.is_group()) {
        markup += " · ";
        markup += std::to_string(selection.tile_indices.size());
        markup += " cells";
    }
    markup += "</div>";
    element->SetInnerRML(markup);
    element->SetClass("visible", true);
}

void refresh_area_tile_palette_query(Rml::ElementDocument* doc, AreaTileEditorState& editor,
    ObjectHandle area, bool tiles_visible, AreaTilePointerModifier modifier)
{
    if (!tiles_visible) { return; }
    std::string query;
    if (auto* field = find_el(doc, "area_tile_palette_search")) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(field)) {
            query = control->GetValue();
        } else {
            query = field->GetAttribute<Rml::String>("value", "");
        }
    }
    const bool changed = query != editor.query;
    if (changed) {
        editor.query = std::move(query);
        (void)filter_area_tile_palette(editor.palette, editor.query);
        editor.list.set_total_rows(static_cast<int>(editor.palette.matches.size()));
        const auto selected = std::find_if(editor.palette.matches.begin(), editor.palette.matches.end(),
            [&editor](uint32_t row) { return row < editor.palette.rows.size() && static_cast<int32_t>(row) == editor.selected_row; });
        editor.list.set_selected(selected == editor.palette.matches.end() ? -1 : static_cast<int>(std::distance(editor.palette.matches.begin(), selected)));
        editor.list.set_scroll_top(0);
        editor.rendered = false;
    }
    (void)sync_area_tile_palette_window(doc, editor, area, tiles_visible, modifier, changed);
}

bool sync_area_tile_palette_window(
    Rml::ElementDocument* doc, AreaTileEditorState& editor, ObjectHandle area,
    bool tiles_visible, AreaTilePointerModifier modifier, bool force)
{
    auto* list = find_el(doc, "area_tile_palette_rows");
    if (!tiles_visible || !list || area.type != ObjectType::area) {
        return false;
    }
    if (auto* feedback = find_el(doc, "area_tile_palette_feedback")) {
        const bool visible = !editor.feedback.empty();
        feedback->SetInnerRML(
            visible ? Rml::StringUtilities::EncodeRml(editor.feedback) : std::string{});
        feedback->SetClass("visible", visible);
    }
    if (auto* hint = find_el(doc, "area_tile_modifier_hint")) {
        hint->SetClass("visible",
            modifier
                == nw::toolset::AreaTilePointerModifier::select);
    }
    sync_area_tile_selection_info(doc, editor, area);

    if (editor.palette.area != area
        || editor.palette.resource_generation
            != nw::kernel::resman().generation()) {
        (void)rebuild_area_tile_palette(editor, area);
        force = true;
    }
    configure_area_tile_palette_list(editor);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(
            list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    editor.list.set_viewport_height(viewport_height);
    editor.list.set_scroll_top(scroll_top);
    const auto range = editor.list.compute_range();
    const int row_count = static_cast<int>(editor.palette.matches.size());
    const bool stable = !force && editor.rendered
        && list->GetNumChildren() > 0
        && editor.rendered_row_count == row_count
        && editor.rendered_range.start == range.start
        && editor.rendered_range.end == range.end;
    if (stable) {
        return false;
    }
    (void)nw::toolset::load_area_tile_palette_thumbnails(
        editor.palette, range.start, range.end);

    std::string markup;
    if (editor.palette.status
        != nw::toolset::AreaTilePaletteStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += Rml::StringUtilities::EncodeRml(Rml::String{editor.palette.diagnostic.empty()
                ? std::string_view{"Tile palette is unavailable."}
                : std::string_view{editor.palette.diagnostic}});
        markup += "</div>";
    } else if (editor.palette.matches.empty()) {
        markup = "<div class=\"property_tree_empty\">"
                 "No actions match this filter.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(
            editor.list, AreaTilePaletteListAdapter{editor.palette});
    }
    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    editor.rendered_range = range;
    editor.rendered_row_count = row_count;
    editor.rendered = true;
    return true;
}

void append_area_tile_palette_markup(
    std::string& content_markup, const AreaTileEditorState& editor)
{
    std::string_view folder_label = "Tiles";
    if (editor.palette.current_folder < editor.palette.rows.size()) {
        folder_label
            = editor.palette.rows[editor.palette.current_folder].label;
    }
    content_markup += "<div id=\"area_tile_palette\" "
                      "class=\"object_workbench area_tile_palette\">"
                      "<div class=\"object_workbench_header area_tile_palette_header\">";
    if (editor.palette.current_folder != editor.palette.root_folder) {
        content_markup += "<button id=\"area_tile_editor_back\" type=\"button\" "
                          "class=\"panel_back_button\" title=\"Back\">"
                          "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                          "<span class=\"panel_back_shaft\"></span></span></button>";
    }
    content_markup += "<div class=\"area_tile_palette_heading\">"
                      "<div class=\"object_workbench_title\">";
    content_markup += Rml::StringUtilities::EncodeRml(Rml::String{folder_label});
    content_markup += "</div></div></div>"
                      "<div class=\"area_tile_palette_controls\">"
                      "<input id=\"area_tile_palette_search\" type=\"text\" value=\"";
    content_markup += Rml::StringUtilities::EncodeRml(editor.query);
    content_markup += "\" placeholder=\"Find terrain or feature\"/>"
                      "<div id=\"area_tile_palette_feedback\" "
                      "class=\"area_tile_palette_feedback\"></div>"
                      "<div id=\"area_tile_modifier_hint\" "
                      "class=\"area_tile_modifier_hint\">"
                      "Left-click selects a tile or group &middot; "
                      "Right-click cycles its variation</div>"
                      "</div><div id=\"area_tile_selection_info\" "
                      "class=\"area_tile_selection_info\"></div>"
                      "<div id=\"area_tile_palette_rows\" "
                      "class=\"area_tile_palette_rows\"></div></div>";
}

std::optional<nw::toolset::AreaTileBrush> selected_area_tile_brush(
    const AreaTileEditorState& editor, AreaTilePointerButton pointer_button) noexcept
{
    if (editor.selected_row < 0
        || static_cast<size_t>(editor.selected_row)
            >= editor.palette.rows.size()
        || editor.palette.rows[static_cast<size_t>(editor.selected_row)].kind
            != nw::toolset::AreaTilePaletteRowKind::action) {
        return std::nullopt;
    }
    auto brush
        = editor.palette.rows[static_cast<size_t>(editor.selected_row)].brush;
    if (brush.kind == nw::toolset::AreaTileBrushKind::group) {
        brush.orientation = editor.group_orientation;
    }
    if (pointer_button == AreaTilePointerButton::primary) {
        return brush;
    }
    if (pointer_button == AreaTilePointerButton::secondary
        && brush.kind == nw::toolset::AreaTileBrushKind::raise) {
        brush.kind = nw::toolset::AreaTileBrushKind::lower;
        return brush;
    }
    return std::nullopt;
}

bool reset_area_tile_editor(AreaTileEditorState& editor, ObjectHandle area)
{
    editor.feedback.clear();
    editor.query.clear();
    editor.preview_rows.clear();
    editor.selection = {};
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_modifier = AreaTilePointerModifier::none;
    editor.cursor_update_pending = false;
    editor.selected_row = -1;
    editor.list.set_scroll_top(0);
    return rebuild_area_tile_palette(editor, area);
}

bool rotate_area_tile_group_orientation(AreaTileEditorState& editor)
{
    if (editor.stroke.active) { return false; }
    const auto brush = selected_area_tile_brush(editor, AreaTilePointerButton::primary);
    if (!brush) {
        editor.feedback = "Choose a terrain action";
        return false;
    }
    if (brush->kind != AreaTileBrushKind::group) {
        editor.feedback = "Only tileset features have a manual rotation";
        return false;
    }
    editor.group_orientation = (editor.group_orientation + 1) % 4;
    editor.feedback = "Feature rotation: "
        + std::to_string(editor.group_orientation * 90) + " degrees";
    editor.cursor_target_index = UINT32_MAX;
    editor.cursor_update_pending = false;
    return true;
}

bool area_tile_height_brush(
    nw::toolset::AreaTileBrush brush) noexcept
{
    return brush.kind == nw::toolset::AreaTileBrushKind::raise
        || brush.kind == nw::toolset::AreaTileBrushKind::lower;
}

nw::toolset::ObjectEditApplyResult build_area_tile_stroke_edits(
    nw::ObjectHandle area,
    std::span<const uint32_t> tile_indices,
    std::span<const uint32_t> corner_indices,
    nw::toolset::AreaTileBrush brush,
    uint64_t seed,
    nw::toolset::AreaTileEditBatch& output)
{
    if (area_tile_height_brush(brush)) {
        const int32_t delta
            = brush.kind == nw::toolset::AreaTileBrushKind::raise ? 1 : -1;
        return nw::toolset::build_area_tile_height_brush_edits(
            area, corner_indices, delta, seed, output);
    }
    return nw::toolset::build_area_tile_brush_edits(
        area, tile_indices, brush, seed, output);
}

bool append_area_tile_height_preview_cells(
    AreaTileStrokeState& stroke,
    std::span<const uint32_t> corner_indices) noexcept
{
    for (const uint32_t corner_index : corner_indices) {
        const auto cells = nw::toolset::resolve_area_tile_corner_cell(
            stroke.width, stroke.height, corner_index);
        for (uint8_t index = 0; index < cells.count; ++index) {
            const uint32_t tile_index = cells.tile_indices[index];
            if (tile_index >= stroke.previewed_tiles.size()
                || stroke.previewed_tiles[tile_index] != 0) {
                continue;
            }
            try {
                stroke.tile_indices.push_back(tile_index);
            } catch (const std::bad_alloc&) {
                return false;
            } catch (const std::length_error&) {
                return false;
            }
            stroke.previewed_tiles[tile_index] = 1;
        }
    }
    return true;
}

} // namespace nw::toolset
