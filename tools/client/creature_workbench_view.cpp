#include "creature_workbench_view.hpp"
#include "command_view.hpp"
#include "shell_controller.hpp"
#include "smalls_rmlui.hpp"
#include "toolset_backend.hpp"
#include "workspace.hpp"
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/smalls/runtime.hpp>

#include <RmlUi/Core.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <utility>

namespace nw::toolset {
namespace {
constexpr int kCreatureFeatRowHeightPx = 30;
constexpr int kCreatureFeatOverscanRows = 8;
constexpr int kCreatureSpellRowHeightPx = 30;
constexpr int kCreatureSpellOverscanRows = 8;
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

std::optional<int32_t> control_integer(Rml::Element* element, const char* attribute)
{
    const auto text = element->GetAttribute<Rml::String>(attribute, "");
    int32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) { return std::nullopt; }
    return value;
}

class CreatureFeatListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    explicit CreatureFeatListAdapter(const nw::toolset::CreatureFeatViewSnapshot& snapshot)
        : snapshot_{snapshot}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(snapshot_.rows.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        return static_cast<int>(snapshot_.rows[static_cast<size_t>(index)].feat_id);
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "creature_feat_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        if (index < 0 || static_cast<size_t>(index) >= snapshot_.rows.size()) {
            return {};
        }

        const auto& row = snapshot_.rows[static_cast<size_t>(index)];
        std::string markup;
        markup.reserve(160);
        markup += "<div class=\"creature_feat_cells";
        if (row.assigned) {
            markup += " assigned";
        }
        markup += "\"><span class=\"creature_feat_name\">";
        markup += escape_html(snapshot_.text_view(row.name));
        markup += "</span></div>";
        return markup;
    }

private:
    const nw::toolset::CreatureFeatViewSnapshot& snapshot_;
};

class CreatureSpellListAdapter final : public nw::toolset::VirtualListAdapter {
public:
    CreatureSpellListAdapter(const nw::toolset::CreatureSpellViewSnapshot& snapshot,
        const std::vector<uint32_t>& matches)
        : snapshot_{snapshot}
        , matches_{matches}
    {
    }

    [[nodiscard]] int size() const override
    {
        return static_cast<int>(matches_.size());
    }

    [[nodiscard]] int row_key(int index) const override
    {
        const auto row_index = matches_[static_cast<size_t>(index)];
        return snapshot_.rows[row_index].spell_id;
    }

    [[nodiscard]] std::string_view row_extra_classes() const override
    {
        return "creature_spell_row";
    }

    [[nodiscard]] std::string render_row_inner(int index, bool /*selected*/) const override
    {
        if (index < 0 || static_cast<size_t>(index) >= matches_.size()) {
            return {};
        }
        const auto row_index = matches_[static_cast<size_t>(index)];
        if (row_index >= snapshot_.rows.size()) {
            return {};
        }

        const auto& row = snapshot_.rows[row_index];
        std::string markup;
        markup.reserve(320);
        markup += "<div class=\"creature_spell_cells";
        if (snapshot_.memorizes ? row.uses > 0 : row.known) {
            markup += " assigned";
        }
        markup += "\"><span class=\"creature_spell_name\">";
        markup += escape_html(snapshot_.text_view(row.name));
        markup += "</span><span class=\"creature_spell_level\">";
        markup += std::to_string(row.level);
        markup += "</span>";
        if (snapshot_.memorizes) {
            markup += "<span class=\"quantity_stepper\"><button type=\"button\" "
                      "class=\"creature_spell_decrement\" data-spell=\"";
            markup += std::to_string(row.spell_id);
            markup += "\"";
            if (row.uses == 0) {
                markup += " disabled";
            }
            markup += ">-</button><span>";
            markup += std::to_string(row.uses);
            markup += "</span><button type=\"button\" class=\"creature_spell_increment\" "
                      "data-spell=\"";
            markup += std::to_string(row.spell_id);
            markup += "\">+</button></span>";
        } else {
            markup += "<span class=\"creature_spell_known\">";
            if (row.known) {
                markup += "Known";
            }
            markup += "</span>";
        }
        markup += "</div>";
        return markup;
    }

private:
    const nw::toolset::CreatureSpellViewSnapshot& snapshot_;
    const std::vector<uint32_t>& matches_;
};

void invalidate_creature_feat_render(CreatureWorkbenchViewState& state)
{
    configure_creature_feat_list(state);
    state.creature_feat_list.set_total_rows(static_cast<int>(state.creature_feats.rows.size()));
    state.creature_feat_rendered = false;
}

std::string creature_spell_choice_label(
    const nw::toolset::CreatureSpellViewSnapshot& snapshot,
    const std::vector<nw::toolset::CreatureSpellChoice>& choices,
    int32_t selected,
    std::string_view fallback)
{
    const auto choice = std::ranges::find(
        choices, selected, &nw::toolset::CreatureSpellChoice::value);
    if (choice == choices.end()) {
        return std::string{fallback};
    }
    return std::string{snapshot.text_view(choice->name)};
}

void append_creature_spell_filter_field(std::string& markup,
    const CreatureWorkbenchViewState& state,
    CreatureSpellFilterField field,
    std::string_view name,
    std::string_view value)
{
    const bool active = state.creature_spell_filter_field == field
        && state.creature_spell_combobox.is_active();
    markup += "<label><span>";
    markup += name;
    markup += "</span><div";
    if (active) {
        markup += " id=\"active_creature_spell_filter_field\" tabindex=\"0\"";
    }
    markup += " class=\"combobox_field creature_spell_filter_field";
    if (active && state.creature_spell_combobox.popup_visible()) {
        markup += " open";
    }
    markup += "\" data-filter=\"";
    if (field == CreatureSpellFilterField::class_) {
        markup += "class";
    } else if (field == CreatureSpellFilterField::level) {
        markup += "level";
    } else {
        markup += "metamagic";
    }
    markup += "\"><span class=\"combobox_value creature_spell_filter_value\">";
    markup += escape_html(value);
    markup += "</span><span class=\"combobox_arrow\">"
              "<span class=\"combobox_arrow_indicator\"></span></span>"
              "</div></label>";
}

} // namespace

void configure_creature_feat_list(CreatureWorkbenchViewState& state)
{
    if (state.creature_feat_list_configured) {
        return;
    }
    state.creature_feat_list.set_row_height(kCreatureFeatRowHeightPx);
    state.creature_feat_list.set_overscan(kCreatureFeatOverscanRows);
    state.creature_feat_list_configured = true;
}

void configure_creature_spell_list(CreatureWorkbenchViewState& state)
{
    if (state.creature_spell_list_configured) {
        return;
    }
    state.creature_spell_list.set_row_height(kCreatureSpellRowHeightPx);
    state.creature_spell_list.set_overscan(kCreatureSpellOverscanRows);
    state.creature_spell_list_configured = true;
}

bool active_creature_feats_match_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    return target.matches_active_tab
        && state.creature_feats.status == nw::toolset::CreatureFeatViewStatus::ready;
}

bool active_creature_feat_object_matches_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    return target.matches_active_tab
        && state.creature_feats.object.type == nw::ObjectType::creature;
}

void rebuild_active_creature_feats(CreatureWorkbenchViewState& state, nw::ObjectHandle object)
{
    nw::toolset::build_creature_feat_rows(
        nw::kernel::runtime(), object, state.creature_feat_query, state.creature_feats);
    invalidate_creature_feat_render(state);
}

void clear_active_creature_feats(CreatureWorkbenchViewState& state)
{
    state.creature_feats = {};
    configure_creature_feat_list(state);
    state.creature_feat_list.set_total_rows(0);
    state.creature_feat_list.set_scroll_top(0);
    state.creature_feat_rendered = false;
}

bool sync_creature_feat_window(Rml::ElementDocument* doc, CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, bool force)
{
    auto* list = find_el(doc, "creature_feat_rows");
    if (!list) {
        return false;
    }

    configure_creature_feat_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0, static_cast<int>(std::lround(list->GetScrollTop())));
    state.creature_feat_list.set_viewport_height(viewport_height);
    state.creature_feat_list.set_scroll_top(scroll_top);
    const auto range = state.creature_feat_list.compute_range();
    const int row_count = static_cast<int>(state.creature_feats.rows.size());
    if (!force && state.creature_feat_rendered
        && row_count == state.rendered_creature_feat_row_count
        && range.start == state.rendered_creature_feat_range.start
        && range.end == state.rendered_creature_feat_range.end) {
        return false;
    }

    std::string markup;
    if (active_creature_feat_object_matches_tab(state, target)
        && state.creature_feats.status != nw::toolset::CreatureFeatViewStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.creature_feats.diagnostic.empty()
                ? std::string_view{"Creature feat data is unavailable."}
                : std::string_view{state.creature_feats.diagnostic});
        markup += "</div>";
    } else if (!active_creature_feats_match_tab(state, target)) {
        markup = "<div class=\"property_tree_empty\">Waiting for a live Creature.</div>";
    } else if (state.creature_feats.rows.empty()) {
        markup = "<div class=\"property_tree_empty\">No feats match this filter.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(
            state.creature_feat_list, CreatureFeatListAdapter{state.creature_feats});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    if (auto* count = find_el(doc, "creature_feat_count")) {
        count->SetInnerRML(active_creature_feats_match_tab(state, target)
                ? std::to_string(state.creature_feats.rows.size())
                : std::string{"0"});
    }
    state.rendered_creature_feat_range = range;
    state.rendered_creature_feat_row_count = row_count;
    state.creature_feat_rendered = true;
    return true;
}

bool active_creature_spells_match_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    return target.matches_active_tab
        && state.creature_spells.status == nw::toolset::CreatureSpellViewStatus::ready;
}

bool active_creature_spell_object_matches_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    return target.matches_active_tab
        && state.creature_spells.object.type == nw::ObjectType::creature;
}

bool active_creature_spell_filter_matches_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    return active_creature_spells_match_tab(state, target)
        && target.surface == ObjectWorkbenchSurface::spells
        && state.creature_spell_filter_field != CreatureSpellFilterField::none
        && state.creature_spell_combobox.is_active();
}

void clear_creature_spell_filter(CreatureWorkbenchViewState& state)
{
    state.creature_spell_combobox.close();
    state.creature_spell_filter_field = CreatureSpellFilterField::none;
    state.creature_spell_popup_placement.reset();
}

void filter_active_creature_spells(CreatureWorkbenchViewState& state)
{
    nw::toolset::filter_creature_spell_rows(state.creature_spells,
        state.creature_spell_query,
        state.creature_spell_level,
        state.creature_spell_matches);
    configure_creature_spell_list(state);
    state.creature_spell_list.set_total_rows(
        static_cast<int>(state.creature_spell_matches.size()));
    state.creature_spell_rendered = false;
}

void rebuild_active_creature_spells(CreatureWorkbenchViewState& state,
    nw::ObjectHandle object,
    int32_t selected_class,
    int32_t selected_metamagic)
{
    clear_creature_spell_filter(state);
    nw::toolset::build_creature_spell_rows(nw::kernel::runtime(),
        object,
        selected_class,
        selected_metamagic,
        state.creature_spells);
    filter_active_creature_spells(state);
}

void clear_active_creature_spells(CreatureWorkbenchViewState& state)
{
    clear_creature_spell_filter(state);
    state.creature_spells = {};
    state.creature_spell_matches.clear();
    configure_creature_spell_list(state);
    state.creature_spell_list.set_total_rows(0);
    state.creature_spell_list.set_scroll_top(0);
    state.creature_spell_rendered = false;
}

std::optional<CreatureSpellFilterField> creature_spell_filter_field_from_name(
    std::string_view value) noexcept
{
    if (value == "class") {
        return CreatureSpellFilterField::class_;
    }
    if (value == "level") {
        return CreatureSpellFilterField::level;
    }
    if (value == "metamagic") {
        return CreatureSpellFilterField::metamagic;
    }
    return std::nullopt;
}

bool open_creature_spell_filter(CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, CreatureSpellFilterField field)
{
    if (!active_creature_spells_match_tab(state, target)
        || field == CreatureSpellFilterField::none) {
        clear_creature_spell_filter(state);
        return false;
    }

    std::vector<nw::toolset::VirtualComboBoxItem> options;
    int32_t selected = -1;
    if (field == CreatureSpellFilterField::class_) {
        options.reserve(state.creature_spells.classes.size());
        for (const auto& choice : state.creature_spells.classes) {
            options.push_back({
                .key = choice.value,
                .label = std::string{state.creature_spells.text_view(choice.name)},
            });
        }
        selected = state.creature_spells.selected_class;
    } else if (field == CreatureSpellFilterField::level) {
        options.reserve(11);
        options.push_back({.key = -1, .label = "All"});
        for (int32_t level = 0; level <= 9; ++level) {
            options.push_back({.key = level, .label = std::to_string(level)});
        }
        selected = state.creature_spell_level;
    } else {
        options.reserve(state.creature_spells.metamagic.size());
        for (const auto& choice : state.creature_spells.metamagic) {
            options.push_back({
                .key = choice.value,
                .label = std::string{state.creature_spells.text_view(choice.name)},
            });
        }
        selected = state.creature_spells.selected_metamagic;
    }

    if (!state.creature_spell_combobox.open(std::move(options), selected)) {
        clear_creature_spell_filter(state);
        return false;
    }
    state.creature_spell_filter_field = field;
    state.creature_spell_popup_placement.reset();
    return true;
}

bool commit_creature_spell_filter(CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, int32_t value)
{
    if (!active_creature_spell_filter_matches_tab(state, target)) {
        return false;
    }

    int32_t selected_class = state.creature_spells.selected_class;
    int32_t selected_metamagic = state.creature_spells.selected_metamagic;
    if (state.creature_spell_filter_field == CreatureSpellFilterField::class_) {
        const auto choice = std::ranges::find(
            state.creature_spells.classes, value, &nw::toolset::CreatureSpellChoice::value);
        if (choice == state.creature_spells.classes.end()) {
            return false;
        }
        selected_class = value;
    } else if (state.creature_spell_filter_field == CreatureSpellFilterField::level) {
        if (value < -1 || value > 9) {
            return false;
        }
        state.creature_spell_level = value;
    } else {
        const auto choice = std::ranges::find(
            state.creature_spells.metamagic, value, &nw::toolset::CreatureSpellChoice::value);
        if (choice == state.creature_spells.metamagic.end()) {
            return false;
        }
        selected_metamagic = value;
    }

    if (state.creature_spell_filter_field == CreatureSpellFilterField::level) {
        filter_active_creature_spells(state);
    } else {
        rebuild_active_creature_spells(state,
            state.creature_spells.object,
            selected_class,
            selected_metamagic);
    }
    state.creature_spell_list.set_scroll_top(0);
    clear_creature_spell_filter(state);
    return true;
}

bool sync_creature_spell_window(Rml::ElementDocument* doc, CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, bool force)
{
    auto* list = find_el(doc, "creature_spell_rows");
    if (!list) {
        return false;
    }

    configure_creature_spell_list(state);
    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int scroll_top = std::max(0, static_cast<int>(std::lround(list->GetScrollTop())));
    state.creature_spell_list.set_viewport_height(viewport_height);
    state.creature_spell_list.set_scroll_top(scroll_top);
    const auto range = state.creature_spell_list.compute_range();
    const int row_count = static_cast<int>(state.creature_spell_matches.size());
    if (!force && state.creature_spell_rendered
        && row_count == state.rendered_creature_spell_row_count
        && range.start == state.rendered_creature_spell_range.start
        && range.end == state.rendered_creature_spell_range.end) {
        return false;
    }

    std::string markup;
    if (active_creature_spell_object_matches_tab(state, target)
        && state.creature_spells.status != nw::toolset::CreatureSpellViewStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.creature_spells.diagnostic.empty()
                ? std::string_view{"Creature spell data is unavailable."}
                : std::string_view{state.creature_spells.diagnostic});
        markup += "</div>";
    } else if (!active_creature_spells_match_tab(state, target)) {
        markup = "<div class=\"property_tree_empty\">Waiting for a live Creature.</div>";
    } else if (state.creature_spells.classes.empty()) {
        markup = "<div class=\"property_tree_empty\">";
        markup += escape_html(state.creature_spells.diagnostic.empty()
                ? std::string_view{"Creature has no spellcasting classes."}
                : std::string_view{state.creature_spells.diagnostic});
        markup += "</div>";
    } else if (state.creature_spells.rows.empty()) {
        markup = "<div class=\"property_tree_empty\">Selected class has no configured spells.</div>";
    } else if (state.creature_spell_matches.empty()) {
        markup = "<div class=\"property_tree_empty\">No spells match this filter.</div>";
    } else {
        markup = nw::toolset::render_virtual_list(state.creature_spell_list,
            CreatureSpellListAdapter{state.creature_spells, state.creature_spell_matches});
    }

    list->SetInnerRML(markup);
    list->SetScrollTop(static_cast<float>(scroll_top));
    if (auto* count = find_el(doc, "creature_spell_count")) {
        count->SetInnerRML(active_creature_spells_match_tab(state, target)
                ? std::to_string(state.creature_spell_matches.size())
                : std::string{"0"});
    }
    if (auto* value_header = find_el(doc, "creature_spell_value_header")) {
        value_header->SetInnerRML(state.creature_spells.memorizes ? "Uses" : "Known");
    }
    state.rendered_creature_spell_range = range;
    state.rendered_creature_spell_row_count = row_count;
    state.creature_spell_rendered = true;
    return true;
}

bool sync_creature_spell_filter_window(
    Rml::ElementDocument* doc, CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target, bool force)
{
    auto* list = find_el(doc, "creature_spell_filter_options");
    if (!list || !active_creature_spell_filter_matches_tab(state, target)
        || !state.creature_spell_combobox.popup_visible()) {
        return false;
    }

    const int viewport_height = std::max(1,
        static_cast<int>(std::lround(std::max(list->GetClientHeight(), list->GetOffsetHeight()))));
    const int observed_scroll_top = std::max(0,
        static_cast<int>(std::lround(list->GetScrollTop())));
    auto update = state.creature_spell_combobox.update(
        viewport_height, observed_scroll_top, force);
    if (update.replace_markup) {
        list->SetInnerRML(update.markup);
    }
    if (update.set_scroll) {
        list->SetScrollTop(static_cast<float>(update.scroll_top));
    }

    bool positioned = false;
    auto* field = find_el(doc, "active_creature_spell_filter_field");
    auto* workbench = find_el(doc, "object_workbench");
    if (field && workbench) {
        const nw::toolset::VirtualComboBoxRect anchor{
            .x = static_cast<int>(std::lround(field->GetAbsoluteLeft() - workbench->GetAbsoluteLeft())),
            .y = static_cast<int>(std::lround(field->GetAbsoluteTop() - workbench->GetAbsoluteTop())),
            .width = static_cast<int>(std::lround(field->GetOffsetWidth())),
            .height = static_cast<int>(std::lround(field->GetOffsetHeight())),
        };
        const nw::toolset::VirtualComboBoxRect bounds{
            .width = static_cast<int>(std::lround(workbench->GetClientWidth())),
            .height = static_cast<int>(std::lround(workbench->GetClientHeight())),
        };
        const auto placement = state.creature_spell_combobox.place_popup(anchor, bounds);
        if (placement.width > 0 && placement.height > 0
            && (!state.creature_spell_popup_placement
                || *state.creature_spell_popup_placement != placement)) {
            list->SetProperty("left", std::to_string(placement.left) + "px");
            list->SetProperty("top", std::to_string(placement.top) + "px");
            list->SetProperty("width", std::to_string(placement.width) + "px");
            list->SetProperty("height", std::to_string(placement.height) + "px");
            state.creature_spell_popup_placement = placement;
            positioned = true;
        }
    }
    return update.replace_markup || update.set_scroll || positioned;
}

void append_creature_spell_markup(std::string& markup, const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    markup += "<div class=\"creature_spell_editor\"><div class=\"creature_spell_toolbar\">";
    markup += "<input id=\"creature_spell_search\" type=\"text\" placeholder=\"Filter spells...\" value=\"";
    markup += escape_html(state.creature_spell_query);
    markup += "\" /></div><div class=\"creature_spell_header\">";
    markup += "<span class=\"creature_spell_header_name\">Spell</span>";
    markup += "<span class=\"creature_spell_header_level\">Level</span>";
    markup += "<span id=\"creature_spell_value_header\" class=\"creature_spell_header_value\">";
    markup += state.creature_spells.memorizes ? "Uses" : "Known";
    markup += "</span><span id=\"creature_spell_count\" class=\"property_tree_count\">";
    markup += active_creature_spells_match_tab(state, target)
        ? std::to_string(state.creature_spell_matches.size())
        : std::string{"0"};
    markup += "</span></div><div id=\"creature_spell_rows\" class=\"creature_spell_rows\">";
    markup += "<div class=\"property_tree_empty\">Waiting for a live Creature.</div></div>";
    markup += "<div class=\"creature_spell_filters\">";
    append_creature_spell_filter_field(markup,
        state,
        CreatureSpellFilterField::class_,
        "Class",
        creature_spell_choice_label(state.creature_spells,
            state.creature_spells.classes,
            state.creature_spells.selected_class,
            "None"));
    append_creature_spell_filter_field(markup,
        state,
        CreatureSpellFilterField::level,
        "Spell Level",
        state.creature_spell_level < 0 ? std::string{"All"}
                                       : std::to_string(state.creature_spell_level));
    append_creature_spell_filter_field(markup,
        state,
        CreatureSpellFilterField::metamagic,
        "Metamagic",
        creature_spell_choice_label(state.creature_spells,
            state.creature_spells.metamagic,
            state.creature_spells.selected_metamagic,
            "None"));
    markup += "</div></div>";
}

void append_creature_classes_markup(std::string& markup, const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    markup += "<div class=\"creature_classes_editor\">";
    if (!active_creature_class_presentation_matches_tab(state, target)) {
        markup += "<div class=\"property_tree_empty";
        if (state.creature_class_presentation.status
            == nw::toolset::ObjectDetailsStatus::invalid_data) {
            markup += " error";
        }
        markup += "\">";
        markup += escape_html(state.creature_class_presentation.diagnostic.empty()
                ? std::string_view{"Waiting for a live Creature."}
                : std::string_view{state.creature_class_presentation.diagnostic});
        markup += "</div></div>";
        return;
    }

    markup += "<div class=\"creature_classes_header\"><span>Class</span><span>Level</span></div>";
    if (state.creature_class_presentation.rows.empty()) {
        markup += "<div class=\"property_tree_empty\">No classes assigned.</div></div>";
        return;
    }
    size_t row_index = 0;
    for (const auto& row : state.creature_class_presentation.rows) {
        markup += "<div class=\"creature_class_row";
        if ((row_index & 1u) != 0) {
            markup += " alternate";
        }
        markup += "\"><span class=\"creature_class_name\">";
        markup += escape_html(state.creature_class_presentation.text_view(row.label));
        markup += "</span><span class=\"quantity_stepper creature_class_level_controls\">"
                  "<button type=\"button\" class=\"creature_class_level_adjust\" data-slot=\"";
        markup += std::to_string(row.slot);
        markup += "\" data-delta=\"-1\"";
        if (row.level <= row.minimum_level) {
            markup += " disabled";
        }
        markup += ">-</button><span>";
        markup += std::to_string(row.level);
        markup += "</span><button type=\"button\" class=\"creature_class_level_adjust\" data-slot=\"";
        markup += std::to_string(row.slot);
        markup += "\" data-delta=\"1\"";
        if (row.level >= row.maximum_level) {
            markup += " disabled";
        }
        markup += ">+</button></span></div>";
        ++row_index;
    }
    markup += "</div>";
}

void append_creature_workbench_overlay_markup(
    std::string& markup, const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    if (active_creature_spell_filter_matches_tab(state, target)
        && state.creature_spell_combobox.popup_visible()) {
        markup += "<div id=\"creature_spell_filter_options\" "
                  "class=\"combobox_options combobox_popup creature_spell_filter_options\"";
        if (state.creature_spell_popup_placement) {
            const auto& placement = *state.creature_spell_popup_placement;
            markup += " style=\"left:";
            markup += std::to_string(placement.left);
            markup += "px;top:";
            markup += std::to_string(placement.top);
            markup += "px;width:";
            markup += std::to_string(placement.width);
            markup += "px;height:";
            markup += std::to_string(placement.height);
            markup += "px\"";
        }
        markup += "><div class=\"property_tree_empty\">"
                  "Loading choices...</div></div>";
    }
}

bool active_creature_class_presentation_matches_tab(const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    return target.matches_active_tab
        && state.creature_class_presentation.object == target.object
        && state.creature_class_presentation.status == nw::toolset::ObjectDetailsStatus::ready;
}

void rebuild_creature_class_presentation(CreatureWorkbenchViewState& state, nw::ObjectHandle object)
{
    auto& runtime = nw::kernel::runtime();
    if (object.type == nw::ObjectType::creature) {
        nw::toolset::build_creature_class_presentation(
            runtime, object, state.creature_class_presentation);
        if (state.creature_class_presentation.status
            == nw::toolset::ObjectDetailsStatus::invalid_data) {
            LOG_F(WARNING, "rollnw-client: %s",
                state.creature_class_presentation.diagnostic.c_str());
        }
    } else {
        state.creature_class_presentation = {};
    }
}

std::optional<CreatureWorkbenchCommandClick> capture_creature_workbench_command_click(
    Rml::Element* hit, const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, uint64_t module_generation)
{
    auto* control = find_ancestor_with_class(hit, "creature_class_level_adjust");
    auto kind = CreatureWorkbenchCommandKind::class_level;
    int32_t delta = 0;
    if (!control) {
        control = find_ancestor_with_class(hit, "creature_spell_decrement");
        kind = CreatureWorkbenchCommandKind::memorized_spell;
        delta = -1;
    }
    if (!control) {
        control = find_ancestor_with_class(hit, "creature_spell_increment");
        delta = 1;
    }
    if (!control) {
        control = find_ancestor_with_class(hit, "creature_spell_row");
        kind = CreatureWorkbenchCommandKind::known_spell;
    }
    if (!control) {
        control = find_ancestor_with_class(hit, "creature_feat_row");
        kind = CreatureWorkbenchCommandKind::feat;
    }
    if (!control) { return std::nullopt; }
    CreatureWorkbenchCommandClick click;
    click.release_phase = ClientRmlForwardPhase::before_native;
    const auto key = control_integer(control, kind == CreatureWorkbenchCommandKind::class_level ? "data-slot" : kind == CreatureWorkbenchCommandKind::memorized_spell ? "data-spell"
                                                                                                                                                                      : "data-key");
    if (!key || *key < 0 || !target.matches_active_tab || target.object.type != ObjectType::creature
        || !workspace.active_tab()) { return click; }
    click.key = *key;
    click.delta = delta;
    if (kind == CreatureWorkbenchCommandKind::class_level) {
        const auto adjustment = control_integer(control, "data-delta");
        if (!adjustment || (*adjustment != -1 && *adjustment != 1)
            || !active_creature_class_presentation_matches_tab(state, target)) { return click; }
        const auto row = std::ranges::find(state.creature_class_presentation.rows, *key, &CreatureClassPresentationRow::slot);
        if (row == state.creature_class_presentation.rows.end()
            || (*adjustment < 0 ? row->level <= row->minimum_level : row->level >= row->maximum_level)) { return click; }
        click.current = row->level;
        click.delta = *adjustment;
    } else if (kind == CreatureWorkbenchCommandKind::feat) {
        if (!active_creature_feats_match_tab(state, target) || state.creature_feats.object != target.object) { return click; }
        const auto row = std::ranges::find(state.creature_feats.rows, static_cast<uint32_t>(*key), &CreatureFeatRow::feat_id);
        if (row == state.creature_feats.rows.end()) { return click; }
        click.current = row->assigned ? 1 : 0;
    } else {
        if (!active_creature_spells_match_tab(state, target) || state.creature_spells.object != target.object
            || state.creature_spells.memorizes != (kind == CreatureWorkbenchCommandKind::memorized_spell)) { return click; }
        const auto row = std::ranges::find(state.creature_spells.rows, *key, &CreatureSpellRow::spell_id);
        if (row == state.creature_spells.rows.end()
            || (kind == CreatureWorkbenchCommandKind::memorized_spell && delta < 0 && row->uses <= 0)) { return click; }
        click.current = kind == CreatureWorkbenchCommandKind::known_spell ? (row->known ? 1 : 0) : row->uses;
        click.class_id = state.creature_spells.selected_class;
        click.metamagic = state.creature_spells.selected_metamagic;
    }
    click.object = target.object;
    click.module_generation = module_generation;
    click.mutation_epoch = object_mutation_state().epoch;
    click.tab_id = workspace.active_tab_id();
    click.kind = kind;
    return click;
}

bool execute_creature_workbench_command_click(CreatureWorkbenchCommandClick& click,
    const CreatureWorkbenchViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, ToolsetBackend& backend,
    ShellController& shell, const CommandContext& context)
{
    const auto kind = std::exchange(click.kind, CreatureWorkbenchCommandKind::none);
    if (kind == CreatureWorkbenchCommandKind::none || kind > CreatureWorkbenchCommandKind::memorized_spell
        || click.release_phase != ClientRmlForwardPhase::before_native || click.key < 0
        || !target.matches_active_tab || target.object != click.object || click.object.type != ObjectType::creature
        || backend.module_generation() != click.module_generation || object_mutation_state().epoch != click.mutation_epoch
        || !workspace.active_tab() || workspace.active_tab_id() != click.tab_id || context.active_tab_id != click.tab_id
        || context.workspace != &workspace || smalls_rmlui_host().active_object() != click.object) { return false; }
    CommandArgs args;
    const auto append = [&](std::initializer_list<int32_t> values) {
        args.reserve(values.size());
        for (const auto value : values) {
            args.push_back(CommandArg::positional_string(std::to_string(value)));
        }
    };
    const char* command = nullptr;
    if (kind == CreatureWorkbenchCommandKind::class_level) {
        if ((click.delta != -1 && click.delta != 1) || click.key >= 8
            || !active_creature_class_presentation_matches_tab(state, target)) { return false; }
        CreatureClassPresentationSnapshot current;
        build_creature_class_presentation(nw::kernel::runtime(), click.object, current);
        const auto row = std::ranges::find(current.rows, click.key, &CreatureClassPresentationRow::slot);
        if (current.status != ObjectDetailsStatus::ready || row == current.rows.end() || row->level != click.current
            || (click.delta < 0 ? row->level <= row->minimum_level : row->level >= row->maximum_level)) { return false; }
        command = "object.creature.adjust_class_level";
        append({click.key, click.delta});
    } else if (kind == CreatureWorkbenchCommandKind::feat) {
        if ((click.current != 0 && click.current != 1) || !active_creature_feats_match_tab(state, target)
            || state.creature_feats.object != click.object) { return false; }
        CreatureFeatViewSnapshot current;
        build_creature_feat_rows(nw::kernel::runtime(), click.object, {}, current);
        const auto row = std::ranges::find(current.rows, static_cast<uint32_t>(click.key), &CreatureFeatRow::feat_id);
        if (current.status != CreatureFeatViewStatus::ready || row == current.rows.end()
            || row->assigned != (click.current != 0)) { return false; }
        command = "object.creature.set_feat";
        append({click.key, 1 - click.current});
    } else {
        if (!active_creature_spells_match_tab(state, target) || state.creature_spells.object != click.object
            || state.creature_spells.selected_class != click.class_id || state.creature_spells.selected_metamagic != click.metamagic
            || state.creature_spells.memorizes != (kind == CreatureWorkbenchCommandKind::memorized_spell)) { return false; }
        CreatureSpellViewSnapshot current;
        build_creature_spell_rows(nw::kernel::runtime(), click.object, click.class_id, click.metamagic, current);
        const auto row = std::ranges::find(current.rows, click.key, &CreatureSpellRow::spell_id);
        if (current.status != CreatureSpellViewStatus::ready || row == current.rows.end()
            || current.selected_class != click.class_id || current.selected_metamagic != click.metamagic
            || current.memorizes != (kind == CreatureWorkbenchCommandKind::memorized_spell)) { return false; }
        if (kind == CreatureWorkbenchCommandKind::known_spell) {
            if ((click.current != 0 && click.current != 1) || row->known != (click.current != 0)) { return false; }
            command = "object.creature.set_known_spell";
            append({click.class_id, click.key, 1 - click.current});
        } else {
            if ((click.delta != -1 && click.delta != 1) || row->uses != click.current || row->uses < 0
                || (click.delta < 0 && row->uses == 0)) { return false; }
            command = "object.creature.adjust_memorized_spell";
            append({click.class_id, click.key, click.metamagic, click.delta});
        }
    }
    const auto result = backend.execute_command({command, std::move(args)}, context);
    append_command_results(shell, {&result, 1});
    return result.ok();
}

} // namespace nw::toolset
