#include "creature_body_part_editor.hpp"

#include "ui_v1.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace nw::toolset {

namespace {

constexpr std::string_view body_parts_list = "creature.appearance.body_parts";
constexpr std::string_view body_part_options_list = "creature.appearance.body_part_options";

std::string option_detail(uint32_t flags)
{
    if ((flags & creature_body_part_option_flag_armor) != 0) {
        return "Armor";
    }
    if ((flags & creature_body_part_option_flag_empty) != 0) {
        return "None";
    }
    if ((flags & creature_body_part_option_flag_mirror) != 0) {
        return "Mirror";
    }
    return {};
}

UiListSelection list_selection(
    std::string_view list_id, int32_t value, int index)
{
    return UiListSelection{
        .list_id = std::string{list_id},
        .key = std::to_string(value),
        .index = index,
        .cell = -1,
    };
}

} // namespace

bool CreatureBodyPartEditor::ensure_lists(VirtualListHost& host)
{
    if (host_generation_ == host.generation()) {
        return true;
    }

    const UiListConfig parts_config{
        .row_height = 30,
        .overscan = 6,
        .columns = 1,
    };
    const UiListConfig options_config{
        .row_height = 30,
        .overscan = 4,
        .columns = 1,
    };
    if (!host.create(std::string{body_parts_list}, parts_config)
        || !host.create(std::string{body_part_options_list}, options_config)
        || !host.set_title(body_part_options_list, "Body Part Models")
        || !host.set_visible(body_parts_list, false)
        || !host.set_visible(body_part_options_list, false)
        || !host.register_refresh_callback("toolset.creature_editor.refresh")
        || !host.set_callback(body_parts_list, UiListEventType::activate,
            "toolset.creature_editor.on_body_part_activate")
        || !host.set_callback(body_part_options_list, UiListEventType::activate,
            "toolset.creature_editor.on_body_part_option_activate")) {
        return false;
    }
    host_generation_ = host.generation();
    return true;
}

bool CreatureBodyPartEditor::clear(VirtualListHost& host)
{
    if (!ensure_lists(host)) {
        return false;
    }
    object_ = ObjectHandle{};
    assembly_ = -1;
    values_.clear();
    part_ids_.clear();
    option_ids_.clear();
    selected_part_ = -1;
    options_open_ = false;
    return host.set_items(body_parts_list, {})
        && host.set_visible(body_parts_list, false)
        && host.set_items(body_part_options_list, {})
        && host.set_visible(body_part_options_list, false);
}

int32_t CreatureBodyPartEditor::selected_value() const noexcept
{
    if (selected_part_ < 0
        || static_cast<size_t>(selected_part_) >= values_.size()) {
        return -1;
    }
    return values_[static_cast<size_t>(selected_part_)];
}

bool CreatureBodyPartEditor::refresh_options(
    const CreatureBodyPartCatalog& catalog, VirtualListHost& host)
{
    option_ids_.clear();
    const auto options = catalog.options(assembly_, selected_part_);
    if (selected_part_ < 0 || options.empty()) {
        options_open_ = false;
        return host.set_items(body_part_options_list, {})
            && host.set_visible(body_part_options_list, false);
    }

    const int32_t current = selected_value();
    const bool valid_current = current >= 0 && current <= 255;
    const bool current_present = valid_current
        && std::ranges::find(options, current,
               &CreatureBodyPartOption::option_id)
            != options.end();
    std::vector<UiListItem> items;
    items.reserve(options.size() + (valid_current && !current_present ? 1u : 0u));
    option_ids_.reserve(items.capacity());

    bool current_added = !valid_current || current_present;
    for (const auto& option : options) {
        if (!current_added && current < option.option_id) {
            items.push_back(UiListItem{
                .key = std::to_string(current),
                .cells = {std::to_string(current), "Current", {}, {}},
                .cell_count = 2,
                .enabled_mask = 3,
            });
            option_ids_.push_back(current);
            current_added = true;
        }
        items.push_back(UiListItem{
            .key = std::to_string(option.option_id),
            .cells = {std::to_string(option.option_id),
                option_detail(option.flags), {}, {}},
            .cell_count = 2,
            .enabled_mask = 3,
        });
        option_ids_.push_back(option.option_id);
    }
    if (!current_added) {
        items.push_back(UiListItem{
            .key = std::to_string(current),
            .cells = {std::to_string(current), "Current", {}, {}},
            .cell_count = 2,
            .enabled_mask = 3,
        });
        option_ids_.push_back(current);
    }

    if (!host.set_items(body_part_options_list, std::move(items))
        || !host.set_visible(body_part_options_list, options_open_)) {
        return false;
    }
    const auto selected = std::ranges::find(option_ids_, current);
    if (selected == option_ids_.end()) {
        return true;
    }
    const int index = static_cast<int>(selected - option_ids_.begin());
    return host.set_selected(body_part_options_list,
        list_selection(body_part_options_list, current, index), false);
}

bool CreatureBodyPartEditor::refresh(const CreatureBodyPartEditorInput& input,
    const CreatureBodyPartCatalog& catalog,
    VirtualListHost& host)
{
    if (!ensure_lists(host)) {
        return false;
    }
    const auto parts = catalog.parts(input.assembly);
    if (input.object.type != ObjectType::creature
        || input.assembly < 0 || parts.empty()) {
        return clear(host);
    }

    if (input.object != object_ || input.assembly != assembly_) {
        selected_part_ = -1;
        options_open_ = false;
    }
    object_ = input.object;
    assembly_ = input.assembly;
    values_.assign(input.values.begin(), input.values.end());

    std::vector<UiListItem> items;
    part_ids_.clear();
    items.reserve(parts.size());
    part_ids_.reserve(parts.size());
    for (const auto& part : parts) {
        if ((part.flags & creature_body_part_info_flag_editor_visible) == 0) {
            continue;
        }
        if (part.part_id < 0
            || static_cast<size_t>(part.part_id) >= values_.size()) {
            return clear(host);
        }
        const int32_t value = values_[static_cast<size_t>(part.part_id)];
        items.push_back(UiListItem{
            .key = std::to_string(part.part_id),
            .cells = {part.label, std::to_string(value), {}, {}},
            .cell_count = 2,
            .enabled_mask = 3,
        });
        part_ids_.push_back(part.part_id);
    }
    if (items.empty()) {
        return clear(host);
    }
    if (!host.set_items(body_parts_list, std::move(items))
        || !host.set_visible(body_parts_list, true)) {
        return false;
    }

    const auto selected = std::ranges::find(part_ids_, selected_part_);
    if (selected == part_ids_.end()) {
        selected_part_ = -1;
        options_open_ = false;
        option_ids_.clear();
        return host.set_items(body_part_options_list, {})
            && host.set_visible(body_part_options_list, false);
    }
    const int index = static_cast<int>(selected - part_ids_.begin());
    return host.set_selected(body_parts_list,
               list_selection(body_parts_list, selected_part_, index), false)
        && refresh_options(catalog, host);
}

bool CreatureBodyPartEditor::close_options(VirtualListHost& host)
{
    if (!ensure_lists(host)) {
        return false;
    }
    selected_part_ = -1;
    options_open_ = false;
    option_ids_.clear();
    return host.set_items(body_part_options_list, {})
        && host.set_visible(body_part_options_list, false)
        && host.set_selected(body_parts_list,
            UiListSelection{
                .list_id = std::string{body_parts_list},
                .index = -1,
                .cell = -1,
            },
            false);
}

bool CreatureBodyPartEditor::activate_part(
    const UiListSelection& selection,
    const CreatureBodyPartCatalog& catalog,
    VirtualListHost& host)
{
    if (!ensure_lists(host) || selection.list_id != body_parts_list
        || selection.index < 0
        || static_cast<size_t>(selection.index) >= part_ids_.size()) {
        return false;
    }
    const int32_t part = part_ids_[static_cast<size_t>(selection.index)];
    if (selection.key != std::to_string(part)) {
        return false;
    }
    if (selected_part_ == part && options_open_) {
        options_open_ = false;
        return host.set_visible(body_part_options_list, false);
    }
    selected_part_ = part;
    options_open_ = true;
    return refresh_options(catalog, host);
}

std::optional<CreatureBodyPartEdit>
CreatureBodyPartEditor::activate_option(
    const UiListSelection& selection) const
{
    if (selection.list_id != body_part_options_list || selected_part_ < 0
        || selection.index < 0
        || static_cast<size_t>(selection.index) >= option_ids_.size()) {
        return std::nullopt;
    }
    const int32_t value = option_ids_[static_cast<size_t>(selection.index)];
    if (selection.key != std::to_string(value)) {
        return std::nullopt;
    }
    return CreatureBodyPartEdit{selected_part_, value};
}

bool CreatureBodyPartEditor::hide_options(VirtualListHost& host)
{
    if (!ensure_lists(host)) {
        return false;
    }
    options_open_ = false;
    return host.set_visible(body_part_options_list, false);
}

} // namespace nw::toolset
