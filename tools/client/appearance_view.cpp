#include "appearance_view.hpp"
#include "command_view.hpp"
#include "creature_body_part_editor.hpp"
#include "object_edits.hpp"
#include "shell_controller.hpp"
#include "smalls_rmlui.hpp"
#include "toolset_backend.hpp"
#include "workspace.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/smalls/runtime.hpp>
#include <utility>

namespace nw::toolset {
namespace {
constexpr int kAppearanceRowHeightPx = 30;
constexpr int kAppearanceOverscanRows = 4;
constexpr int kSoundCatalogRowHeightPx = 34;
constexpr int kSoundCatalogOverscanRows = 4;
std::string escape_html(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

Rml::Element* find_el(Rml::ElementDocument* doc, const char* id)
{
    return doc ? doc->GetElementById(id) : nullptr;
}

Rml::Element* find_ancestor_with_class(Rml::Element* hit, const char* name)
{
    for (auto* element = hit; element; element = element->GetParentNode()) {
        if (element->IsClassSet(name)) { return element; }
    }
    return nullptr;
}

Rml::Element* find_ancestor_with_id(Rml::Element* hit, const char* id)
{
    for (auto* element = hit; element; element = element->GetParentNode()) {
        if (element->GetId() == id) { return element; }
    }
    return nullptr;
}

nw::toolset::AppearanceCatalog& appearance_catalog(
    AppearanceViewState& state, nw::toolset::AppearanceCatalogKind kind);
const nw::toolset::AppearanceCatalog& appearance_catalog(
    const AppearanceViewState& state, nw::toolset::AppearanceCatalogKind kind);
nw::toolset::AppearanceCatalogKind appearance_catalog_kind(
    const AppearanceViewState& state, AppearanceEditorField field);
std::optional<int32_t> appearance_editor_value(
    const AppearanceViewState& state, AppearanceEditorField field);
void reset_appearance_catalogs_for_module(AppearanceViewState& state, uint64_t module_generation);
void configure_appearance_list(AppearanceViewState& state);
void select_live_appearance(AppearanceViewState& state);
std::string_view creature_color_palette_asset(int32_t palette) noexcept;
void configure_sound_catalog_list(AppearanceViewState& state);
std::string appearance_editor_label(
    const AppearanceViewState& state, AppearanceEditorField field, int32_t current);
std::string appearance_editor_label(
    const AppearanceViewState& state, AppearanceEditorField field);
std::string_view appearance_editor_field_name(AppearanceEditorField field) noexcept;
std::string_view appearance_editor_field_label(AppearanceEditorField field) noexcept;
class AppearanceListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    AppearanceListAdapter(const nw::toolset::AppearanceCatalog& catalog,
        const std::vector<uint32_t>& matches)
        : catalog_{catalog}
        , matches_{matches}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(matches_.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        return row(index).id;
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "appearance_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        const auto& value = row(index);
        std::string markup;
        markup.reserve(value.name.size() + 96);
        markup += "<div class=\"appearance_name\">";
        markup += escape_html(value.name);
        markup += "</div><div class=\"appearance_id\">";
        markup += std::to_string(value.id);
        markup += "</div>";
        return markup;
    }

private:
    [[nodiscard]] const nw::toolset::AppearanceCatalogRow& row(int index) const
    {
        return catalog_.rows[matches_[static_cast<size_t>(index)]];
    }

    const nw::toolset::AppearanceCatalog& catalog_;
    const std::vector<uint32_t>& matches_;
};

class SoundCatalogListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    SoundCatalogListAdapter(const nw::toolset::SoundCatalog& catalog,
        const std::vector<uint32_t>& matches)
        : catalog_{catalog}
        , matches_{matches}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(matches_.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        return static_cast<int>(matches_[static_cast<size_t>(index)]);
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "sound_catalog_row";
    }

    [[nodiscard]] std::string render_row_inner(
        int index, bool /*selected*/) const override
    {
        const auto& value = row(index);
        std::string markup;
        markup.reserve(value.name.size() + value.resource.length() + 96);
        markup += "<div class=\"sound_catalog_name\">";
        markup += escape_html(value.name);
        markup += "</div><div class=\"sound_catalog_resref\">";
        markup += escape_html(value.resource.view());
        markup += "</div>";
        return markup;
    }

private:
    [[nodiscard]] const nw::toolset::SoundCatalogRow& row(int index) const
    {
        return catalog_.rows[matches_[static_cast<size_t>(index)]];
    }

    const nw::toolset::SoundCatalog& catalog_;
    const std::vector<uint32_t>& matches_;
};

nw::toolset::AppearanceCatalog& appearance_catalog(
    AppearanceViewState& state, nw::toolset::AppearanceCatalogKind kind)
{
    switch (kind) {
    case nw::toolset::AppearanceCatalogKind::creature:
        return state.creature_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::placeable:
        return state.placeable_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::door:
        return state.door_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::wing:
        return state.wing_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::tail:
        return state.tail_appearance_catalog;
    }
    std::abort();
}

const nw::toolset::AppearanceCatalog& appearance_catalog(
    const AppearanceViewState& state, nw::toolset::AppearanceCatalogKind kind)
{
    switch (kind) {
    case nw::toolset::AppearanceCatalogKind::creature:
        return state.creature_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::placeable:
        return state.placeable_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::door:
        return state.door_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::wing:
        return state.wing_appearance_catalog;
    case nw::toolset::AppearanceCatalogKind::tail:
        return state.tail_appearance_catalog;
    }
    std::abort();
}

nw::toolset::AppearanceCatalogKind appearance_catalog_kind(
    const AppearanceViewState& state, AppearanceEditorField field)
{
    if (field == AppearanceEditorField::wings) {
        return nw::toolset::AppearanceCatalogKind::wing;
    }
    if (field == AppearanceEditorField::tail) {
        return nw::toolset::AppearanceCatalogKind::tail;
    }
    if (state.appearance_object.type == nw::ObjectType::placeable) {
        return nw::toolset::AppearanceCatalogKind::placeable;
    }
    if (state.appearance_object.type == nw::ObjectType::door) {
        return nw::toolset::AppearanceCatalogKind::door;
    }
    return nw::toolset::AppearanceCatalogKind::creature;
}

std::optional<int32_t> appearance_editor_value(
    const AppearanceViewState& state, AppearanceEditorField field)
{
    if (field == AppearanceEditorField::appearance) {
        if (state.appearance_object.type == nw::ObjectType::door) {
            const auto selectors = nw::toolset::door_appearance(
                nw::kernel::runtime(), state.appearance_object);
            return selectors && selectors->appearance == 0
                ? std::optional<int32_t>{selectors->generic_type}
                : std::nullopt;
        }
        return nw::toolset::object_appearance(
            nw::kernel::runtime(), state.appearance_object);
    }
    if (state.appearance_object.type != nw::ObjectType::creature) {
        return std::nullopt;
    }
    const auto values = nw::toolset::editable_creature_accessories(
        nw::kernel::runtime(), state.appearance_object);
    const size_t index = field == AppearanceEditorField::wings ? 0 : 1;
    if (index >= values.size()) {
        return std::nullopt;
    }
    return values[index];
}

void reset_appearance_catalogs_for_module(AppearanceViewState& state, uint64_t module_generation)
{
    const uint64_t generation = module_generation;
    if (generation == state.appearance_catalog_generation) {
        return;
    }

    state.creature_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::creature,
    };
    state.placeable_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::placeable,
    };
    state.door_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::door,
    };
    state.wing_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::wing,
    };
    state.tail_appearance_catalog = {
        .kind = nw::toolset::AppearanceCatalogKind::tail,
    };
    state.appearance_catalog_generation = generation;
    state.appearance_query.clear();
    state.appearance_matches.clear();
    state.appearance_object = nw::ObjectHandle{};
    state.appearance_editor_scroll_top = 0.0f;
    state.appearance_selector_open = false;
    state.appearance_editor_field = AppearanceEditorField::appearance;
    clear_color_editor(state);
}

void configure_appearance_list(AppearanceViewState& state)
{
    if (state.appearance_list_configured) {
        return;
    }
    state.appearance_list.set_row_height(kAppearanceRowHeightPx);
    state.appearance_list.set_overscan(kAppearanceOverscanRows);
    state.appearance_list_configured = true;
}

void select_live_appearance(AppearanceViewState& state)
{
    int selected = -1;
    const auto current = appearance_editor_value(
        state, state.appearance_editor_field);
    if (current) {
        const auto& catalog = active_appearance_catalog(state);
        for (size_t index = 0; index < state.appearance_matches.size(); ++index) {
            const uint32_t row_index = state.appearance_matches[index];
            if (row_index < catalog.rows.size() && catalog.rows[row_index].id == *current) {
                selected = static_cast<int>(index);
                break;
            }
        }
    }
    state.appearance_list.set_selected(selected);
}

std::string_view creature_color_palette_asset(int32_t palette) noexcept
{
    if (palette == 0) {
        return "mvpal_skin.png";
    }
    if (palette == 1) {
        return "mvpal_hair.png";
    }
    return {};
}

void configure_sound_catalog_list(AppearanceViewState& state)
{
    if (state.sound_catalog_list_configured) {
        return;
    }
    state.sound_catalog_list.set_row_height(kSoundCatalogRowHeightPx);
    state.sound_catalog_list.set_overscan(kSoundCatalogOverscanRows);
    state.sound_catalog_list_configured = true;
}

std::string appearance_editor_label(
    const AppearanceViewState& state, AppearanceEditorField field, int32_t current)
{
    const auto* row = nw::toolset::find_appearance_catalog_row(
        appearance_catalog(state, appearance_catalog_kind(state, field)), current);
    if (row) {
        return row->name;
    }
    if (field == AppearanceEditorField::wings) {
        return "Wings " + std::to_string(current);
    }
    if (field == AppearanceEditorField::tail) {
        return "Tail " + std::to_string(current);
    }
    return "Appearance " + std::to_string(current);
}

std::string appearance_editor_label(
    const AppearanceViewState& state, AppearanceEditorField field)
{
    const auto current = appearance_editor_value(state, field);
    if (!current) {
        return "Unavailable";
    }
    return appearance_editor_label(state, field, *current);
}

std::string_view appearance_editor_field_name(AppearanceEditorField field) noexcept
{
    if (field == AppearanceEditorField::wings) {
        return "wings";
    }
    if (field == AppearanceEditorField::tail) {
        return "tail";
    }
    return "appearance";
}

std::string_view appearance_editor_field_label(AppearanceEditorField field) noexcept
{
    if (field == AppearanceEditorField::wings) {
        return "Wings";
    }
    if (field == AppearanceEditorField::tail) {
        return "Tail";
    }
    return "Appearance";
}

} // namespace

std::optional<nw::toolset::AppearanceCatalogKind> appearance_catalog_kind(nw::ObjectType type)
{
    if (type == nw::ObjectType::creature) {
        return nw::toolset::AppearanceCatalogKind::creature;
    }
    if (type == nw::ObjectType::placeable) {
        return nw::toolset::AppearanceCatalogKind::placeable;
    }
    if (type == nw::ObjectType::door) {
        return nw::toolset::AppearanceCatalogKind::door;
    }
    return std::nullopt;
}

const nw::toolset::AppearanceCatalog& active_appearance_catalog(const AppearanceViewState& state)
{
    return appearance_catalog(
        state, appearance_catalog_kind(state, state.appearance_editor_field));
}

void clear_color_editor(AppearanceViewState& state)
{
    state.color_editor_object = nw::ObjectHandle{};
    state.color_editor_channel = -1;
}

void close_appearance_selector(AppearanceViewState& state)
{
    state.appearance_selector_open = false;
    state.appearance_editor_field = AppearanceEditorField::appearance;
    state.appearance_query.clear();
}

void rebuild_active_appearances(AppearanceViewState& state, uint64_t module_generation, nw::ObjectHandle object)
{
    configure_appearance_list(state);
    reset_appearance_catalogs_for_module(state, module_generation);
    const auto kind = appearance_catalog_kind(object.type);
    if (!kind) {
        clear_active_appearances(state);
        return;
    }
    state.appearance_object = object;

    auto& catalog = appearance_catalog(state, *kind);
    if (catalog.status == nw::toolset::AppearanceCatalogStatus::empty) {
        (void)nw::toolset::build_appearance_catalog(*kind, catalog);
    }
    if (object.type == nw::ObjectType::creature) {
        for (const auto accessory_kind : {
                 nw::toolset::AppearanceCatalogKind::wing,
                 nw::toolset::AppearanceCatalogKind::tail,
             }) {
            auto& accessory_catalog = appearance_catalog(state, accessory_kind);
            if (accessory_catalog.status == nw::toolset::AppearanceCatalogStatus::empty) {
                (void)nw::toolset::build_appearance_catalog(
                    accessory_kind, accessory_catalog);
            }
        }
    } else {
        state.appearance_editor_field = AppearanceEditorField::appearance;
    }

    const auto& active_catalog = active_appearance_catalog(state);
    nw::toolset::filter_appearance_catalog(
        active_catalog, state.appearance_query, state.appearance_matches);
    state.appearance_list.set_total_rows(static_cast<int>(state.appearance_matches.size()));
    select_live_appearance(state);
    state.appearance_scroll_to_selection = true;
    state.appearance_rendered = false;
}

void clear_active_appearances(AppearanceViewState& state)
{
    clear_color_editor(state);
    state.appearance_query.clear();
    state.appearance_matches.clear();
    state.appearance_object = nw::ObjectHandle{};
    state.appearance_editor_scroll_top = 0.0f;
    configure_appearance_list(state);
    state.appearance_list.set_total_rows(0);
    state.appearance_list.set_scroll_top(0);
    state.appearance_rendered = false;
    state.appearance_scroll_to_selection = false;
    state.appearance_selector_open = false;
    state.appearance_editor_field = AppearanceEditorField::appearance;
}

bool active_appearances_match_tab(const AppearanceViewState& state, ObjectWorkbenchTarget target)
{
    return target.matches_active_tab
        && state.appearance_object == target.object
        && appearance_catalog_kind(state.appearance_object.type).has_value();
}

bool active_color_editor_matches_tab(const AppearanceViewState& state, ObjectWorkbenchTarget target)
{
    return active_appearances_match_tab(state, target)
        && target.surface == ObjectWorkbenchSurface::appearance
        && state.color_editor_object == target.object
        && state.color_editor_channel >= 0;
}

bool open_color_editor(AppearanceViewState& state, nw::ObjectHandle object, uint32_t color)
{
    const auto rows = nw::toolset::creature_color_editor_rows(
        nw::kernel::runtime(), object);
    const auto row = std::ranges::find(rows, color,
        &nw::toolset::CreatureColorEditorRow::color);
    if (row == rows.end() || row->value < 0
        || row->value >= kPltPaletteColumns * kPltPaletteRows
        || creature_color_palette_asset(row->palette).empty()) {
        clear_color_editor(state);
        return false;
    }

    state.color_editor_object = object;
    state.color_editor_channel = static_cast<int32_t>(color);
    return true;
}

bool sync_appearance_window(Rml::ElementDocument* doc, AppearanceViewState& state, ObjectWorkbenchTarget target, bool force)
{
    auto* list = find_el(doc, "appearance_rows");
    if (!list) {
        return false;
    }

    configure_appearance_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int observed_scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    int scroll_top = observed_scroll_top;
    state.appearance_list.set_viewport_height(viewport_height);
    state.appearance_list.set_scroll_top(scroll_top);
    bool request_scroll = false;
    if (state.appearance_scroll_to_selection) {
        scroll_top = state.appearance_list.scroll_top_for_index(state.appearance_list.selected());
        state.appearance_list.set_scroll_top(scroll_top);
        request_scroll = scroll_top != observed_scroll_top;
        state.appearance_scroll_to_selection = request_scroll;
    }
    const auto range = state.appearance_list.compute_range();
    const int row_count = static_cast<int>(state.appearance_matches.size());
    const bool stable_markup = !force && state.appearance_rendered
        && row_count == state.rendered_appearance_row_count
        && range.start == state.rendered_appearance_range.start
        && range.end == state.rendered_appearance_range.end;
    if (stable_markup) {
        if (request_scroll) {
            list->SetScrollTop(static_cast<float>(scroll_top));
        }
    } else {
        std::string markup;
        const auto& catalog = active_appearance_catalog(state);
        if (!active_appearances_match_tab(state, target)) {
            markup = "<div class=\"property_tree_empty\">Waiting for a live Creature or Placeable.</div>";
        } else if (catalog.status != nw::toolset::AppearanceCatalogStatus::ready) {
            markup = "<div class=\"property_tree_empty error\">";
            markup += escape_html(catalog.diagnostic.empty()
                    ? std::string_view{"Appearance data is unavailable."}
                    : std::string_view{catalog.diagnostic});
            markup += "</div>";
        } else if (state.appearance_matches.empty()) {
            markup = "<div class=\"property_tree_empty\">No appearances match this filter.</div>";
        } else {
            markup = nw::toolset::render_virtual_list(
                state.appearance_list, AppearanceListAdapter{catalog, state.appearance_matches});
        }

        list->SetInnerRML(markup);
        list->SetScrollTop(static_cast<float>(scroll_top));
        state.rendered_appearance_range = range;
        state.rendered_appearance_row_count = row_count;
        state.appearance_rendered = true;
    }

    return !stable_markup || request_scroll;
}

void close_sound_resource_selector(AppearanceViewState& state)
{
    state.sound_resource_selector_open = false;
    state.sound_catalog_query.clear();
}

void clear_active_sound_catalog(AppearanceViewState& state)
{
    close_sound_resource_selector(state);
    state.sound_catalog_matches.clear();
    configure_sound_catalog_list(state);
    state.sound_catalog_list.set_total_rows(0);
    state.sound_catalog_list.set_scroll_top(0);
    state.sound_catalog_rendered = false;
}

bool active_sound_resource_selector_matches_tab(const AppearanceViewState& state, ObjectWorkbenchTarget target)
{
    return state.sound_resource_selector_open
        && target.surface == ObjectWorkbenchSurface::sounds
        && target.object.type == nw::ObjectType::sound
        && (target.matches_active_tab && target.object.type != nw::ObjectType::invalid);
}

void rebuild_sound_catalog(AppearanceViewState& state, uint64_t resource_generation, bool reset_selection)
{
    configure_sound_catalog_list(state);
    const uint64_t generation = resource_generation;
    if (generation != state.sound_catalog_generation) {
        state.sound_catalog = {};
        state.sound_catalog_generation = generation;
    }
    if (state.sound_catalog.status == nw::toolset::SoundCatalogStatus::empty) {
        (void)nw::toolset::build_sound_catalog(state.sound_catalog);
    }

    nw::toolset::filter_sound_catalog(state.sound_catalog,
        state.sound_catalog_query, state.sound_catalog_matches);
    state.sound_catalog_list.set_total_rows(
        static_cast<int>(state.sound_catalog_matches.size()));
    if (reset_selection
        || state.sound_catalog_list.selected() < 0
        || static_cast<size_t>(state.sound_catalog_list.selected())
            >= state.sound_catalog_matches.size()) {
        state.sound_catalog_list.set_selected(
            state.sound_catalog_matches.empty() ? -1 : 0);
    }
    state.sound_catalog_rendered = false;
}

bool sync_sound_catalog_window(
    Rml::ElementDocument* doc, AppearanceViewState& state, ObjectWorkbenchTarget target, uint64_t resource_generation, bool force)
{
    auto* list = find_el(doc, "sound_catalog_rows");
    if (!list || !active_sound_resource_selector_matches_tab(state, target)) {
        return false;
    }

    const uint64_t generation = resource_generation;
    if (generation != state.sound_catalog_generation) {
        rebuild_sound_catalog(state, resource_generation, true);
        force = true;
    }

    configure_sound_catalog_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(
            list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    state.sound_catalog_list.set_viewport_height(viewport_height);
    state.sound_catalog_list.set_scroll_top(scroll_top);
    const auto range = state.sound_catalog_list.compute_range();
    const int row_count = static_cast<int>(state.sound_catalog_matches.size());
    const bool stable_markup = !force && state.sound_catalog_rendered
        && row_count == state.rendered_sound_catalog_row_count
        && range.start == state.rendered_sound_catalog_range.start
        && range.end == state.rendered_sound_catalog_range.end;
    if (stable_markup) {
        return false;
    }

    std::string markup;
    if (state.sound_catalog.status
        != nw::toolset::SoundCatalogStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.sound_catalog.diagnostic.empty()
                ? std::string_view{"Sound data is unavailable."}
                : std::string_view{state.sound_catalog.diagnostic});
        markup += "</div>";
    } else if (state.sound_catalog_matches.empty()) {
        markup = "<div class=\"property_tree_empty\">"
                 "No sound resources match this filter.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(state.sound_catalog_list,
            SoundCatalogListAdapter{
                state.sound_catalog, state.sound_catalog_matches});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    state.rendered_sound_catalog_range = range;
    state.rendered_sound_catalog_row_count = row_count;
    state.sound_catalog_rendered = true;
    return true;
}

bool commit_sound_catalog_selection(AppearanceViewState& state, ObjectWorkbenchTarget target, ToolsetBackend& backend, ShellController& shell, const CommandContext& context, uint32_t row_index)
{
    if (!active_sound_resource_selector_matches_tab(state, target)
        || row_index >= state.sound_catalog.rows.size()) {
        return false;
    }

    const std::array additions{
        state.sound_catalog.rows[row_index].resource,
    };
    auto edit = nw::toolset::make_sound_resource_additions(
        nw::kernel::runtime(), target.object, additions);
    if (!edit) {
        shell.append_output("warn",
            "The Sound resource list is invalid or already contains 1,024 entries");
        return false;
    }

    auto result = backend.replace_sound_resources(std::move(*edit),
        "Add sound resource",
        context);
    const bool committed = result.ok();
    append_command_results(shell, {&result, 1});
    return committed;
}

std::optional<SoundResourceClick> capture_sound_resource_click(
    Rml::Element* hit, const AppearanceViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, uint64_t module_generation, uint64_t resource_generation)
{
    auto* control = find_ancestor_with_id(hit, "sound_resource_add");
    auto kind = SoundResourceClickKind::open;
    if (!control) {
        control = find_ancestor_with_id(hit, "sound_resource_selector_back");
        kind = SoundResourceClickKind::close;
    }
    if (!control) {
        control = find_ancestor_with_class(hit, "sound_catalog_row");
        kind = SoundResourceClickKind::select;
    }
    if (!control) { return std::nullopt; }
    SoundResourceClick click;
    if (!target.matches_active_tab || target.object.type != ObjectType::sound
        || target.surface != ObjectWorkbenchSurface::sounds || !workspace.active_tab()) { return click; }
    if (kind != SoundResourceClickKind::open && !active_sound_resource_selector_matches_tab(state, target)) { return click; }
    if (kind == SoundResourceClickKind::select) {
        const auto text = control->GetAttribute<Rml::String>("data-key", "");
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), click.row);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || click.row < 0
            || static_cast<size_t>(click.row) >= state.sound_catalog.rows.size()
            || state.sound_catalog.status != SoundCatalogStatus::ready
            || state.sound_catalog_generation != resource_generation
            || std::find(state.sound_catalog_matches.begin(), state.sound_catalog_matches.end(),
                   static_cast<uint32_t>(click.row))
                == state.sound_catalog_matches.end()) { return click; }
        click.resource = state.sound_catalog.rows[static_cast<size_t>(click.row)].resource;
        click.query = state.sound_catalog_query;
    }
    click.kind = kind;
    click.object = target.object;
    click.tab_id = workspace.active_tab_id();
    click.module_generation = module_generation;
    click.resource_generation = resource_generation;
    click.mutation_epoch = object_mutation_state().epoch;
    return click;
}

SoundResourceClickEffect apply_sound_resource_click(SoundResourceClick& click,
    AppearanceViewState& state, ObjectWorkbenchTarget target, const WorkspaceState& workspace,
    ToolsetBackend& backend, ShellController& shell, const CommandContext& context)
{
    const auto kind = std::exchange(click.kind, SoundResourceClickKind::none);
    if (kind == SoundResourceClickKind::none || kind > SoundResourceClickKind::select
        || click.release_phase != ClientRmlForwardPhase::before_native
        || !target.matches_active_tab || target.object != click.object || target.object.type != ObjectType::sound
        || target.surface != ObjectWorkbenchSurface::sounds || !workspace.active_tab()
        || workspace.active_tab_id() != click.tab_id || context.active_tab_id != click.tab_id
        || context.workspace != &workspace || smalls_rmlui_host().active_object() != click.object
        || backend.module_generation() != click.module_generation
        || kernel::resman().generation() != click.resource_generation
        || object_mutation_state().epoch != click.mutation_epoch) { return SoundResourceClickEffect::none; }
    if (kind == SoundResourceClickKind::open) {
        close_appearance_selector(state);
        state.sound_resource_selector_open = true;
        state.sound_catalog_query.clear();
        rebuild_sound_catalog(state, click.resource_generation, true);
        state.sound_catalog_list.set_scroll_top(0);
        return SoundResourceClickEffect::opened;
    }
    if (!active_sound_resource_selector_matches_tab(state, target)) { return SoundResourceClickEffect::none; }
    if (kind == SoundResourceClickKind::select) {
        if (click.row < 0 || static_cast<size_t>(click.row) >= state.sound_catalog.rows.size()
            || state.sound_catalog.status != SoundCatalogStatus::ready
            || state.sound_catalog_generation != click.resource_generation || state.sound_catalog_query != click.query
            || state.sound_catalog.rows[static_cast<size_t>(click.row)].resource != click.resource
            || std::find(state.sound_catalog_matches.begin(), state.sound_catalog_matches.end(),
                   static_cast<uint32_t>(click.row))
                == state.sound_catalog_matches.end()
            || !commit_sound_catalog_selection(state, target, backend, shell, context,
                static_cast<uint32_t>(click.row))) { return SoundResourceClickEffect::none; }
    }
    close_sound_resource_selector(state);
    return SoundResourceClickEffect::closed;
}

std::optional<AppearanceEditorField> appearance_editor_field_from_name(
    std::string_view field) noexcept
{
    if (field == "appearance") {
        return AppearanceEditorField::appearance;
    }
    if (field == "wings") {
        return AppearanceEditorField::wings;
    }
    if (field == "tail") {
        return AppearanceEditorField::tail;
    }
    return std::nullopt;
}

void append_appearance_catalog_field_markup(std::string& content_markup,
    const AppearanceViewState& state,
    AppearanceEditorField field,
    std::optional<int32_t> current)
{
    content_markup += "<div class=\"appearance_catalog_editor\"><div class=\"appearance_field_label\">";
    content_markup += appearance_editor_field_label(field);
    content_markup += "</div><div class=\"combobox_field appearance_field appearance_catalog_field\" data-field=\"";
    content_markup += appearance_editor_field_name(field);
    content_markup += "\"><span class=\"combobox_value\">";
    content_markup += escape_html(current
            ? appearance_editor_label(state, field, *current)
            : appearance_editor_label(state, field));
    content_markup += "</span><span class=\"combobox_arrow\">"
                      "<span class=\"combobox_arrow_indicator\"></span></span></div>";
    content_markup += "</div>";
}

void append_appearance_selector_markup(std::string& content_markup, const AppearanceViewState& state)
{
    const auto label = appearance_editor_field_label(state.appearance_editor_field);
    content_markup += "<div class=\"appearance_selector\">"
                      "<div class=\"appearance_selector_header\">"
                      "<button id=\"appearance_selector_back\" "
                      "class=\"appearance_selector_back panel_back_button\" title=\"Back\">"
                      "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                      "<span class=\"panel_back_shaft\"></span></span></button>"
                      "<div class=\"appearance_selector_title\">";
    content_markup += label;
    content_markup += "</div></div><div class=\"appearance_selector_filter\">"
                      "<input id=\"appearance_search\" type=\"text\" placeholder=\"Filter ";
    content_markup += label;
    content_markup += "...\" value=\"";
    content_markup += escape_html(state.appearance_query);
    content_markup += "\" /></div><div id=\"appearance_rows\" class=\"appearance_selector_rows\">"
                      "<div class=\"property_tree_empty\">Loading options...</div></div></div>";
}

void append_sound_resource_selector_markup(
    std::string& content_markup, const AppearanceViewState& state)
{
    content_markup += "<div class=\"sound_resource_selector appearance_selector\">"
                      "<div class=\"appearance_selector_header\">"
                      "<button id=\"sound_resource_selector_back\" "
                      "class=\"appearance_selector_back panel_back_button\" title=\"Back\">"
                      "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                      "<span class=\"panel_back_shaft\"></span></span></button>"
                      "<div class=\"appearance_selector_title\">Add Sound</div></div>"
                      "<div class=\"appearance_selector_filter\">"
                      "<input id=\"sound_catalog_search\" type=\"text\" "
                      "placeholder=\"Filter sounds...\" value=\"";
    content_markup += escape_html(state.sound_catalog_query);
    content_markup += "\" /></div><div id=\"sound_catalog_rows\" "
                      "class=\"appearance_selector_rows sound_catalog_rows\">"
                      "<div class=\"property_tree_empty\">Loading sounds...</div>"
                      "</div></div>";
}

void append_placeable_appearance_markup(
    std::string& content_markup, const AppearanceViewState& state)
{
    const auto current = appearance_editor_value(
        state, AppearanceEditorField::appearance);
    const auto* row = current
        ? nw::toolset::find_appearance_catalog_row(
              state.placeable_appearance_catalog, *current)
        : nullptr;

    content_markup += "<div class=\"placeable_appearance_editor active\">";
    if (!current) {
        content_markup += "<div class=\"property_tree_empty error\">"
                          "Placeable appearance is unavailable.</div>";
    } else {
        content_markup += "<div class=\"placeable_appearance_summary\">"
                          "<div class=\"placeable_appearance_row\">"
                          "<span class=\"placeable_appearance_label\">Name</span>"
                          "<span class=\"placeable_appearance_value\">";
        content_markup += escape_html(row
                ? std::string_view{row->name}
                : std::string_view{"Unavailable"});
        content_markup += "</span></div><div class=\"placeable_appearance_row\">"
                          "<span class=\"placeable_appearance_label\">Model</span>"
                          "<span class=\"placeable_appearance_value\">";
        if (row) {
            content_markup += escape_html(row->model);
        }
        content_markup += "</span></div></div>";
        if (!row) {
            content_markup += "<div class=\"property_tree_empty error\">"
                              "Appearance ";
            content_markup += std::to_string(*current);
            content_markup += " is unavailable.</div>";
        }
    }
    content_markup += "<div class=\"placeable_appearance_actions\">"
                      "<button id=\"placeable_appearance_previous\" "
                      "class=\"placeable_appearance_cycle_button\" type=\"button\" "
                      "title=\"Previous appearance\">&#x2039;</button>"
                      "<button id=\"placeable_appearance_open\" "
                      "class=\"placeable_appearance_open appearance_catalog_field\" "
                      "data-field=\"appearance\" type=\"button\">Choose Appearance</button>"
                      "<button id=\"placeable_appearance_next\" "
                      "class=\"placeable_appearance_cycle_button\" type=\"button\" "
                      "title=\"Next appearance\">&#x203a;</button></div></div>";
}

void append_door_appearance_markup(
    std::string& content_markup, const AppearanceViewState& state)
{
    const auto selectors = nw::toolset::door_appearance(
        nw::kernel::runtime(), state.appearance_object);
    const nw::DoorTypeInfo* door_type = nullptr;
    const nw::GenericDoorInfo* generic_door = nullptr;
    if (selectors) {
        if (selectors->appearance == 0) {
            generic_door = nw::kernel::rules().genericdoors.get(
                nw::GenericDoor::make(selectors->generic_type));
        } else {
            door_type = nw::kernel::rules().doortypes.get(
                nw::DoorType::make(selectors->appearance));
        }
    }
    const bool generic = selectors && selectors->appearance == 0;
    const auto model = generic && generic_door
        ? generic_door->model
        : door_type ? door_type->model
                    : nw::Resref{};
    const bool available = (generic
                                   ? generic_door && generic_door->valid()
                                   : door_type && door_type->valid())
        && nw::kernel::resman().contains(
            {model, nw::ResourceType::mdl});
    const auto name = generic && generic_door
        ? generic_door->editor_name()
        : door_type ? door_type->editor_name()
                    : nw::String{};

    content_markup += "<div class=\"door_appearance_editor active\">";
    if (!selectors) {
        content_markup += "<div class=\"property_tree_empty error\">"
                          "Door appearance is unavailable.</div>";
    } else {
        content_markup += "<div class=\"door_appearance_summary\">"
                          "<div class=\"door_appearance_row\"><span "
                          "class=\"door_appearance_label\">Source</span><span "
                          "class=\"door_appearance_value\">";
        content_markup += generic ? "Generic Door" : "Tileset Door";
        content_markup += "</span></div><div class=\"door_appearance_row\"><span "
                          "class=\"door_appearance_label\">Name</span><span "
                          "class=\"door_appearance_value\">";
        content_markup += escape_html(name);
        content_markup += "</span></div><div class=\"door_appearance_row\"><span "
                          "class=\"door_appearance_label\">Model</span><span "
                          "class=\"door_appearance_value\">";
        content_markup += escape_html(model.view());
        content_markup += "</span></div></div>";
        if (!available) {
            content_markup += "<div class=\"property_tree_empty error\">"
                              "Selected Door appearance is unavailable.</div>";
        }
    }
    content_markup += "<div class=\"door_appearance_actions\">"
                      "<button id=\"door_appearance_previous\" "
                      "class=\"door_appearance_cycle_button\" type=\"button\" "
                      "title=\"Previous appearance\">&#x2039;</button>"
                      "<button id=\"door_appearance_open\" "
                      "class=\"door_appearance_open appearance_catalog_field\" "
                      "data-field=\"appearance\" type=\"button\">Choose Appearance</button>"
                      "<button id=\"door_appearance_next\" "
                      "class=\"door_appearance_cycle_button\" type=\"button\" "
                      "title=\"Next appearance\">&#x203a;</button></div></div>";
}

void append_creature_accessories_markup(
    std::string& content_markup, const AppearanceViewState& state, ObjectWorkbenchTarget target)
{
    const auto values = nw::toolset::editable_creature_accessories(
        nw::kernel::runtime(), target.object);
    if (values.size() != 2) {
        return;
    }

    content_markup += "<div class=\"creature_accessory_editor\"><div class=\"creature_accessory_title\">Accessories</div>";
    append_appearance_catalog_field_markup(
        content_markup, state, AppearanceEditorField::wings, values[0]);
    append_appearance_catalog_field_markup(
        content_markup, state, AppearanceEditorField::tail, values[1]);
    content_markup += "</div>";
}

void append_creature_colors_markup(std::string& content_markup, ObjectWorkbenchTarget target)
{
    const auto rows = nw::toolset::creature_color_editor_rows(
        nw::kernel::runtime(), target.object);
    if (rows.empty()) {
        return;
    }
    for (const auto& row : rows) {
        if (creature_color_palette_asset(row.palette).empty()
            || row.value < 0
            || row.value >= kPltPaletteColumns * kPltPaletteRows) {
            return;
        }
    }

    content_markup += "<div class=\"creature_color_editor\"><div class=\"creature_color_title\">Colors</div>";
    content_markup += "<div class=\"creature_color_fields\">";
    for (const auto& row : rows) {
        const auto asset = creature_color_palette_asset(row.palette);
        const int source_x = (row.value % kPltPaletteColumns) * 32;
        const int source_y = (row.value / kPltPaletteColumns) * 32;
        content_markup += "<div class=\"creature_color_field\" data-color=\"";
        content_markup += std::to_string(row.color);
        content_markup += "\"><img class=\"creature_color_swatch\" src=\"";
        content_markup += asset;
        content_markup += "\" rect=\"";
        content_markup += std::to_string(source_x);
        content_markup += " ";
        content_markup += std::to_string(source_y);
        content_markup += " 32 32\"/><span>";
        content_markup += escape_html(row.label);
        content_markup += "</span></div>";
    }
    content_markup += "</div></div>";
}

void append_creature_color_selector_markup(std::string& content_markup, const AppearanceViewState& state, ObjectWorkbenchTarget target)
{
    if (!active_color_editor_matches_tab(state, target)) {
        return;
    }

    const auto rows = nw::toolset::creature_color_editor_rows(
        nw::kernel::runtime(), state.color_editor_object);
    const auto selected = std::ranges::find(rows, state.color_editor_channel,
        &nw::toolset::CreatureColorEditorRow::color);
    if (selected == rows.end() || selected->value < 0
        || selected->value >= kPltPaletteColumns * kPltPaletteRows) {
        return;
    }
    const auto asset = creature_color_palette_asset(selected->palette);
    if (asset.empty()) {
        return;
    }

    content_markup += "<div id=\"creature_color_selector\" "
                      "class=\"appearance_selector creature_color_selector\">"
                      "<div class=\"appearance_selector_header\">"
                      "<button id=\"creature_color_selector_close\" "
                      "class=\"appearance_selector_back panel_back_button\" title=\"Back\">"
                      "<span class=\"panel_back_icon\"><span class=\"panel_back_head\"></span>"
                      "<span class=\"panel_back_shaft\"></span></span></button>"
                      "<div class=\"appearance_selector_title\">Colors</div></div>"
                      "<div class=\"creature_color_channels\">";
    for (const auto& row : rows) {
        content_markup += "<div class=\"creature_color_channel";
        if (row.color == selected->color) {
            content_markup += " active";
        }
        content_markup += "\" data-color=\"";
        content_markup += std::to_string(row.color);
        content_markup += "\">";
        content_markup += escape_html(row.label);
        content_markup += "</div>";
    }
    content_markup += "</div><div class=\"creature_color_palette_body\">"
                      "<div id=\"creature_color_palette\" class=\"creature_color_palette\" data-color=\"";
    content_markup += std::to_string(selected->color);
    content_markup += "\"><img src=\"";
    content_markup += asset;
    content_markup += "\"/><div class=\"creature_color_selection\" style=\"left:";
    content_markup += std::to_string(
        (selected->value % kPltPaletteColumns) * kPltPaletteCellPx);
    content_markup += "px;top:";
    content_markup += std::to_string(
        (selected->value / kPltPaletteColumns) * kPltPaletteCellPx);
    content_markup += "px\"></div></div></div></div>";
}

bool commit_active_appearance_selection(AppearanceViewState& state, ToolsetBackend& backend, ShellController& shell, const CommandContext& context, int32_t value)
{
    const std::string selected = std::to_string(value);
    nw::toolset::CommandResult result;
    if (state.appearance_editor_field == AppearanceEditorField::appearance) {
        if (state.appearance_object.type == nw::ObjectType::door) {
            result = backend.execute_command(
                "object.door.set_appearance",
                {std::string_view{"0"}, std::string_view{selected}},
                context);
        } else {
            result = backend.execute_command(
                "object.set_appearance",
                {std::string_view{selected}},
                context);
        }
    } else {
        const std::string_view accessory = appearance_editor_field_name(
            state.appearance_editor_field);
        result = backend.execute_command(
            "object.creature.set_accessory",
            {accessory, std::string_view{selected}},
            context);
    }
    append_command_results(shell, {&result, 1});
    return result.ok();
}

bool cycle_active_appearance(AppearanceViewState& state, ObjectWorkbenchTarget target, ToolsetBackend& backend, ShellController& shell, const CommandContext& context, int direction)
{
    if (direction == 0 || !active_appearances_match_tab(state, target)
        || (state.appearance_object.type != nw::ObjectType::placeable
            && state.appearance_object.type != nw::ObjectType::door)) {
        return false;
    }

    const auto& catalog = active_appearance_catalog(state);
    if (catalog.status != nw::toolset::AppearanceCatalogStatus::ready
        || catalog.rows.empty()) {
        return false;
    }
    const auto current = appearance_editor_value(
        state, AppearanceEditorField::appearance);
    const auto& rows = catalog.rows;
    auto selected = current
        ? std::ranges::find(rows, *current,
              &nw::toolset::AppearanceCatalogRow::id)
        : rows.end();
    size_t next = direction < 0 ? rows.size() - 1 : 0;
    if (selected != rows.end()) {
        const size_t index = static_cast<size_t>(selected - rows.begin());
        next = direction < 0
            ? (index == 0 ? rows.size() - 1 : index - 1)
            : (index + 1 == rows.size() ? 0 : index + 1);
    }
    return commit_active_appearance_selection(state, backend, shell, context, rows[next].id);
}

bool commit_active_color_selection(AppearanceViewState& state, ObjectWorkbenchTarget target, ToolsetBackend& backend, ShellController& shell, const CommandContext& context, int32_t value)
{
    if (!active_color_editor_matches_tab(state, target)) {
        return false;
    }

    const std::string color = std::to_string(state.color_editor_channel);
    const std::string selected = std::to_string(value);
    const auto result = backend.execute_command(
        "object.creature.set_color",
        {std::string_view{color}, std::string_view{selected}},
        context);
    append_command_results(shell, {&result, 1});
    return result.ok();
}

bool update_appearance_preview_rows(nw::ObjectHandle object, bool equipment_visible)
{
    if (object.type != nw::ObjectType::creature || !nw::kernel::objects().valid(object)) {
        return false;
    }

    auto& runtime = nw::kernel::runtime();
    auto object_value = nw::smalls::Value::make_object(object);
    object_value.type_id = runtime.object_subtype_for_tag(object.type);
    const auto result = runtime.execute_script("nwn1.creature", "update_appearance_preview",
        {object_value, nw::smalls::Value::make_bool(equipment_visible)});
    return result.ok() && result.value.type_id == runtime.bool_type() && result.value.data.bval;
}

} // namespace nw::toolset
