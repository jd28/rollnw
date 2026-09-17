#include "inventory_workbench_view.hpp"
#include "command_view.hpp"
#include "shell_controller.hpp"
#include "smalls_rmlui.hpp"
#include "toolset_backend.hpp"
#include "workspace.hpp"

#include <RmlUi/Core.h>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/runtime.hpp>

#include <charconv>
#include <utility>

namespace nw::toolset {
namespace {
constexpr int kCreatureInventoryCellPx = 32;
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

const Inventory* live_grid_inventory(ObjectHandle object)
{
    switch (object.type) {
    case ObjectType::creature:
        if (const auto* owner = kernel::objects().get<Creature>(object)) { return &owner->inventory(); }
        break;
    case ObjectType::item:
        if (const auto* owner = kernel::objects().get<Item>(object)) { return &owner->inventory(); }
        break;
    case ObjectType::placeable:
        if (const auto* owner = kernel::objects().get<Placeable>(object)) { return &owner->inventory(); }
        break;
    default:
        break;
    }
    return nullptr;
}

ObjectHandle live_inventory_item(const Inventory& inventory, int32_t index)
{
    if (index >= 0 && static_cast<size_t>(index) < inventory.items.size()) {
        if (const auto* item = inventory_item_ptr(inventory.items[static_cast<size_t>(index)])) { return item->handle(); }
    }
    return ObjectHandle{};
}

constexpr std::array<std::string_view, 18> kCreatureEquipmentSlotLabels{
    "Head",
    "Chest",
    "Boots",
    "Arms",
    "Right Hand",
    "Left Hand",
    "Cloak",
    "Left Ring",
    "Right Ring",
    "Neck",
    "Belt",
    "Arrows",
    "Bullets",
    "Bolts",
    "Attack 2",
    "Attack 1",
    "Special Attack",
    "Skin",
};

constexpr std::array<std::string_view, 18> kCreatureEquipmentSlotAssets{
    "inv_slot_helm.png",
    "inv_slot_armor.png",
    "inv_slot_boots.png",
    "inv_slot_gloves.png",
    "inv_slot_right.png",
    "inv_slot_left.png",
    "inv_slot_cloak.png",
    "inv_slot_ring.png",
    "inv_slot_ring.png",
    "inv_slot_amulet.png",
    "inv_slot_belt.png",
    "inv_slot_arrow.png",
    "inv_slot_sling.png",
    "inv_slot_bolts.png",
    "inv_slot_cre_2.png",
    "inv_slot_cre_1.png",
    "inv_slot_cre_3.png",
    "inv_slot_cre_skin.png",
};

constexpr std::array<std::string_view, 18> kCreatureEquipmentSlotClasses{
    "head",
    "chest",
    "boots",
    "arms",
    "right_hand",
    "left_hand",
    "cloak",
    "left_ring",
    "right_ring",
    "neck",
    "belt",
    "arrows",
    "bullets",
    "bolts",
    "creature_left",
    "creature_right",
    "creature_bite",
    "creature_skin",
};

constexpr std::array<size_t, 4> kCreatureNaturalEquipmentOrder{15, 14, 16, 17};

bool active_creature_inventory_object_matches_tab(const InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    return target.matches_active_tab
        && object_has_grid_inventory(state.creature_inventory.object.type);
}

std::string render_creature_inventory_page(const InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    std::string markup;
    if (active_creature_inventory_object_matches_tab(state, target)
        && state.creature_inventory.status != nw::toolset::InventoryViewStatus::ready) {
        markup = "<div class=\"property_tree_empty error\">";
        markup += escape_html(state.creature_inventory.diagnostic.empty()
                ? std::string_view{"Inventory data is unavailable."}
                : std::string_view{state.creature_inventory.diagnostic});
        markup += "</div>";
    } else if (!active_creature_inventory_matches_tab(state, target)) {
        markup = "<div class=\"property_tree_empty\">Waiting for a live object inventory.</div>";
    } else {
        markup.reserve(9000);
        const int board_width = state.creature_inventory.column_count * kCreatureInventoryCellPx;
        const int board_height = state.creature_inventory.row_count * kCreatureInventoryCellPx;
        markup += "<div id=\"creature_inventory_board\" class=\"creature_inventory_board\" style=\"flex-basis:";
        markup += std::to_string(board_width);
        markup += "px;width:";
        markup += std::to_string(board_width);
        markup += "px;height:";
        markup += std::to_string(board_height);
        markup += "px\">";
        const int cell_count = state.creature_inventory.row_count
            * state.creature_inventory.column_count;
        for (int index = 0; index < cell_count; ++index) {
            markup += "<span class=\"creature_inventory_cell\"></span>";
        }
        markup += "<span id=\"creature_inventory_drop_target\" "
                  "class=\"creature_inventory_drop_target\"></span>";
        for (const auto& row : state.creature_inventory.inventory) {
            if (row.page != state.creature_inventory_page) {
                continue;
            }
            const int top = (row.row - row.height + 1) * kCreatureInventoryCellPx;
            const int left = row.column * kCreatureInventoryCellPx;
            const int width = row.width * kCreatureInventoryCellPx;
            const int height = row.height * kCreatureInventoryCellPx;
            auto label = state.creature_inventory.text_view(row.name);
            if (label.empty()) {
                label = state.creature_inventory.text_view(row.resref);
            }
            markup += "<div class=\"creature_inventory_item";
            if (state.creature_inventory_selection >= 0
                && row.source_index
                    == static_cast<uint32_t>(state.creature_inventory_selection)) {
                markup += " selected";
            }
            markup += "\" data-key=\"";
            markup += std::to_string(row.source_index);
            markup += "\" title=\"";
            markup += escape_html(label);
            markup += "\" style=\"left:";
            markup += std::to_string(left);
            markup += "px;top:";
            markup += std::to_string(top);
            markup += "px;width:";
            markup += std::to_string(width);
            markup += "px;height:";
            markup += std::to_string(height);
            markup += "px\">";
            const auto icon_source = state.creature_inventory.text_view(row.icon_source);
            if (!icon_source.empty()) {
                markup += "<img class=\"creature_inventory_item_icon\" src=\"";
                markup += escape_html(icon_source);
                markup += "\"/>";
            } else {
                markup += "<span class=\"creature_inventory_item_label\">";
                markup += escape_html(label);
                markup += "</span>";
            }
            if (row.infinite || row.stack_size > 1) {
                markup += "<span class=\"creature_inventory_stack\">";
                markup += row.infinite ? "*" : std::to_string(row.stack_size);
                markup += "</span>";
            }
            markup += "</div>";
        }
        markup += "</div><div class=\"creature_inventory_pages\">";
        for (int page = 0; page < state.creature_inventory.page_count; ++page) {
            markup += "<button class=\"creature_inventory_page";
            if (page == state.creature_inventory_page) {
                markup += " active";
            }
            markup += "\" data-page=\"";
            markup += std::to_string(page);
            markup += "\" title=\"Inventory page ";
            markup += std::to_string(page + 1);
            markup += "\">";
            markup += std::to_string(page + 1);
            markup += "</button>";
        }
        markup += "</div>";
    }
    return markup;
}

} // namespace

bool active_creature_inventory_matches_tab(const InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    return target.matches_active_tab
        && state.creature_inventory.status == nw::toolset::InventoryViewStatus::ready;
}

void rebuild_active_creature_inventory(InventoryWorkbenchViewState& state, nw::ObjectHandle object)
{
    nw::toolset::build_object_inventory_rows(
        nw::kernel::runtime(), object, state.item_icon_cache, state.creature_inventory);
    if (state.creature_inventory_page < 0
        || state.creature_inventory_page >= state.creature_inventory.page_count) {
        state.creature_inventory_page = 0;
    }
    if (state.creature_inventory_selection < 0
        || static_cast<size_t>(state.creature_inventory_selection)
            >= state.creature_inventory.inventory.size()) {
        state.creature_inventory_selection = -1;
    }
    state.creature_inventory_rendered = false;
}

void clear_active_creature_inventory(InventoryWorkbenchViewState& state)
{
    state.creature_inventory = {};
    state.creature_inventory_page = 0;
    state.creature_inventory_selection = -1;
    state.creature_inventory_rendered = false;
}

bool sync_creature_inventory_window(Rml::ElementDocument* doc, InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target, bool force)
{
    auto* surface = find_el(doc, "creature_inventory_page_surface");
    if (!surface || (!force && state.creature_inventory_rendered)) {
        return false;
    }

    surface->SetInnerRML(render_creature_inventory_page(state, target));
    if (auto* count = find_el(doc, "creature_inventory_count")) {
        count->SetInnerRML(active_creature_inventory_matches_tab(state, target)
                ? std::to_string(state.creature_inventory.inventory.size())
                : std::string{"0"});
    }
    state.creature_inventory_rendered = true;
    return true;
}

void append_creature_inventory_markup(std::string& content_markup, const InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target)
{
    const bool creature = state.creature_inventory.object.type == nw::ObjectType::creature;
    content_markup += "<div class=\"creature_inventory_editor";
    if (!creature) {
        content_markup += " item_inventory_editor";
    }
    content_markup += "\">";
    if (creature) {
        content_markup += "<div class=\"creature_inventory_section_header\">Equipment</div>";
    }
    if (creature && active_creature_inventory_matches_tab(state, target)) {
        const auto append_slot = [&](size_t index) {
            const auto& row = state.creature_inventory.equipment[index];
            content_markup += "<div class=\"creature_equipment_slot";
            if (row.assigned()) {
                content_markup += " assigned";
            } else {
                content_markup += " empty";
            }
            content_markup += " creature_equipment_slot_";
            content_markup += kCreatureEquipmentSlotClasses[index];
            content_markup += "\" data-slot=\"";
            content_markup += std::to_string(index);
            content_markup += "\" title=\"";
            content_markup += kCreatureEquipmentSlotLabels[index];
            if (row.assigned()) {
                const auto name = state.creature_inventory.text_view(row.name);
                const auto label = name.empty()
                    ? state.creature_inventory.text_view(row.resref)
                    : name;
                if (!label.empty()) {
                    content_markup += ": ";
                    content_markup += escape_html(label);
                }
            }
            content_markup += "\">";
            const auto icon_source = state.creature_inventory.text_view(row.icon_source);
            if (row.assigned() && !icon_source.empty()) {
                content_markup += "<span class=\"creature_equipment_item_frame\" style=\"width:";
                content_markup += std::to_string(row.icon_visible_width);
                content_markup += "px;height:";
                content_markup += std::to_string(row.icon_visible_height);
                content_markup += "px\"><img class=\"creature_equipment_item_icon\" style=\"left:-";
                content_markup += std::to_string(row.icon_visible_x);
                content_markup += "px;top:-";
                content_markup += std::to_string(row.icon_visible_y);
                content_markup += "px\" src=\"";
                content_markup += escape_html(icon_source);
                content_markup += "\"/></span>";
            } else if (!row.assigned()) {
                content_markup += "<img class=\"creature_equipment_slot_icon\" src=\"";
                content_markup += kCreatureEquipmentSlotAssets[index];
                content_markup += "\"/>";
            }
            content_markup += "</div>";
        };

        content_markup += "<div class=\"creature_equipment_standard\">"
                          "<div class=\"creature_equipment_column creature_equipment_right_column\">";
        append_slot(4);
        content_markup += "</div><div class=\"creature_equipment_column creature_equipment_armor_column\">";
        append_slot(1);
        content_markup += "<div class=\"creature_equipment_ammo_row\">";
        append_slot(11);
        append_slot(12);
        append_slot(13);
        content_markup += "</div></div><div class=\"creature_equipment_column creature_equipment_left_column\">";
        append_slot(5);
        append_slot(10);
        content_markup += "</div><div class=\"creature_equipment_column creature_equipment_wearables_column\">";
        append_slot(0);
        append_slot(3);
        content_markup += "<div class=\"creature_equipment_jewelry_row\"><div>";
        append_slot(8);
        append_slot(7);
        content_markup += "</div>";
        append_slot(9);
        content_markup += "</div></div><div class=\"creature_equipment_column creature_equipment_cloak_column\">";
        append_slot(6);
        append_slot(2);
        content_markup += "</div></div><div class=\"creature_equipment_creature_header\">Creature Slots</div>"
                          "<div class=\"creature_equipment_creature\">";
        for (const size_t index : kCreatureNaturalEquipmentOrder) {
            append_slot(index);
        }
        content_markup += "</div>";
    } else if (creature) {
        content_markup += "<div class=\"creature_equipment_grid_empty\"></div>";
    }
    content_markup += "<div class=\"creature_inventory_header\"><span>Inventory</span>"
                      "<span id=\"creature_inventory_count\" class=\"property_tree_count\">";
    content_markup += active_creature_inventory_matches_tab(state, target)
        ? std::to_string(state.creature_inventory.inventory.size())
        : std::string{"0"};
    content_markup += "</span></div><div id=\"creature_inventory_page_surface\" "
                      "class=\"creature_inventory_page_surface\">";
    content_markup += render_creature_inventory_page(state, target);
    content_markup += "</div></div>";
}

std::optional<InventoryWorkbenchClick> capture_inventory_workbench_click(
    Rml::Element* hit, const InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, uint64_t module_generation)
{
    auto* control = find_ancestor_with_class(hit, "creature_equipment_slot");
    auto kind = InventoryWorkbenchClickKind::equipment;
    if (!control) {
        control = find_ancestor_with_class(hit, "creature_inventory_item");
        kind = InventoryWorkbenchClickKind::item;
    }
    if (!control) {
        control = find_ancestor_with_class(hit, "creature_inventory_page");
        kind = InventoryWorkbenchClickKind::page;
    }
    if (!control) { return std::nullopt; }
    InventoryWorkbenchClick click;
    click.release_phase = ClientRmlForwardPhase::before_native;
    const auto index = control_integer(control, kind == InventoryWorkbenchClickKind::equipment ? "data-slot" : kind == InventoryWorkbenchClickKind::item ? "data-key"
                                                                                                                                                         : "data-page");
    if (!index || *index < 0 || !workspace.active_tab() || !active_creature_inventory_matches_tab(state, target)
        || state.creature_inventory.object != target.object || !object_has_grid_inventory(target.object.type)) { return click; }
    const auto& inventory = state.creature_inventory;
    if (kind == InventoryWorkbenchClickKind::equipment) {
        if (static_cast<size_t>(*index) >= inventory.equipment.size()) { return click; }
        if (target.object.type != ObjectType::creature) { return click; }
        click.equipped_item = inventory.equipment[static_cast<size_t>(*index)].item;
        if (click.equipped_item.type != ObjectType::invalid && click.equipped_item.type != ObjectType::item) { return click; }
        if (click.equipped_item.type != ObjectType::item && state.creature_inventory_selection >= 0) {
            const auto selected = static_cast<size_t>(state.creature_inventory_selection);
            if (selected >= inventory.inventory.size() || inventory.inventory[selected].source_index != selected
                || inventory.inventory[selected].item.type != ObjectType::item) { return click; }
            click.inventory_item = inventory.inventory[selected].item;
        }
    } else if (kind == InventoryWorkbenchClickKind::item) {
        if (static_cast<size_t>(*index) >= inventory.inventory.size()) { return click; }
        const auto& row = inventory.inventory[static_cast<size_t>(*index)];
        if (row.source_index != static_cast<uint32_t>(*index) || row.item.type != ObjectType::item) { return click; }
        click.inventory_item = row.item;
    } else {
        if (*index >= inventory.page_count) { return click; }
    }
    click.kind = kind;
    click.object = target.object;
    click.module_generation = module_generation;
    click.mutation_epoch = object_mutation_state().epoch;
    click.index = *index;
    click.selection = state.creature_inventory_selection;
    click.page = state.creature_inventory_page;
    click.tab_id = workspace.active_tab_id();
    return click;
}

bool apply_inventory_workbench_click(InventoryWorkbenchClick& click,
    InventoryWorkbenchViewState& state, ObjectWorkbenchTarget target,
    const WorkspaceState& workspace, ToolsetBackend& backend,
    ShellController& shell, const CommandContext& context)
{
    const auto kind = std::exchange(click.kind, InventoryWorkbenchClickKind::none);
    if (kind == InventoryWorkbenchClickKind::none || kind > InventoryWorkbenchClickKind::page || click.index < 0
        || click.release_phase != ClientRmlForwardPhase::before_native || !active_creature_inventory_matches_tab(state, target)
        || target.object != click.object || state.creature_inventory.object != click.object
        || !workspace.active_tab() || workspace.active_tab_id() != click.tab_id || context.active_tab_id != click.tab_id
        || context.workspace != &workspace || smalls_rmlui_host().active_object() != click.object
        || backend.module_generation() != click.module_generation || object_mutation_state().epoch != click.mutation_epoch
        || state.creature_inventory_selection != click.selection || state.creature_inventory_page != click.page) { return false; }
    const auto* inventory = live_grid_inventory(click.object);
    if (!inventory || inventory->pages() <= 0 || inventory->pages() > UINT8_MAX
        || inventory->rows() <= 0 || inventory->rows() > Inventory::max_rows
        || inventory->columns() <= 0 || inventory->columns() > Inventory::max_columns
        || inventory->pages() != state.creature_inventory.page_count
        || inventory->rows() != state.creature_inventory.row_count || inventory->columns() != state.creature_inventory.column_count) { return false; }
    if (kind == InventoryWorkbenchClickKind::page) {
        if (click.index >= inventory->pages()) { return false; }
        state.creature_inventory_page = click.index;
        state.creature_inventory_selection = -1;
        return true;
    }
    if (kind == InventoryWorkbenchClickKind::item) {
        if (click.inventory_item.type != ObjectType::item || live_inventory_item(*inventory, click.index) != click.inventory_item) { return false; }
        state.creature_inventory_selection = click.index;
        return true;
    }
    const auto* creature = kernel::objects().get<Creature>(click.object);
    if (!creature || static_cast<size_t>(click.index) >= creature->equipment.equips.size()) { return false; }
    const auto& equipped = creature->equipment.equips[static_cast<size_t>(click.index)];
    const auto* item = equip_item_ptr(equipped);
    const ObjectHandle equipped_item = item ? item->handle() : ObjectHandle{};
    if (equipped_item != click.equipped_item || (!item && !equipped.empty())) { return false; }
    CommandInvocation invocation;
    if (click.equipped_item.type == ObjectType::item) {
        invocation.command_id = "object.creature.unequip_slot";
        invocation.args.push_back(CommandArg::positional_string(std::to_string(click.index)));
    } else if (click.selection >= 0) {
        if (click.inventory_item.type != ObjectType::item || live_inventory_item(*inventory, click.selection) != click.inventory_item) { return false; }
        invocation.command_id = "object.creature.equip_inventory_item";
        invocation.args.reserve(2);
        invocation.args.push_back(CommandArg::positional_string(std::to_string(click.selection)));
        invocation.args.push_back(CommandArg::positional_string(std::to_string(click.index)));
    } else {
        return click.selection == -1;
    }
    const auto result = backend.execute_command(std::move(invocation), context);
    if (result.ok()) { state.creature_inventory_selection = -1; }
    append_command_results(shell, {&result, 1});
    return true;
}

} // namespace nw::toolset
