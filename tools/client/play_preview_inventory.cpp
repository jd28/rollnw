#include "play_preview_inventory.hpp"
#include "runtime_input.hpp"

#include <RmlUi/Core.h>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/equipment_changes.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>

#include <algorithm>
#include <charconv>
#include <fmt/format.h>

namespace nw::toolset {
namespace {
constexpr int inventory_cell_dp = 32;

// NWN equipment indices match the existing native slot protocol. These are
// presentation coordinates, not equipment eligibility or gameplay policy.
struct EquipmentSlotView {
    const char* name;
    const char* asset;
    int x, y, width, height;
};
constexpr std::array equipment_slots{
    EquipmentSlotView{"Head", "helm", 224, 0, 64, 64},
    EquipmentSlotView{"Chest", "armor", 80, 0, 64, 120},
    EquipmentSlotView{"Boots", "boots", 296, 128, 64, 64},
    EquipmentSlotView{"Arms", "gloves", 224, 72, 64, 32},
    EquipmentSlotView{"Right hand", "right", 0, 0, 64, 192},
    EquipmentSlotView{"Left hand", "left", 160, 0, 64, 128},
    EquipmentSlotView{"Cloak", "cloak", 296, 0, 64, 120},
    EquipmentSlotView{"Left ring", "ring", 224, 152, 32, 32},
    EquipmentSlotView{"Right ring", "ring", 224, 120, 32, 32},
    EquipmentSlotView{"Neck", "amulet", 256, 120, 32, 64},
    EquipmentSlotView{"Belt", "belt", 160, 144, 64, 32},
    EquipmentSlotView{"Arrows", "arrow", 64, 128, 32, 64},
    EquipmentSlotView{"Bullets", "sling", 96, 128, 32, 32},
    EquipmentSlotView{"Bolts", "bolts", 128, 128, 32, 64},
};

std::string escaped(std::string_view text)
{
    return Rml::StringUtilities::EncodeRml(std::string{text});
}

std::optional<size_t> element_index(Rml::Element& element, const char* attribute)
{
    const auto text = element.GetAttribute<Rml::String>(attribute, "");
    size_t index = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) { return std::nullopt; }
    return index;
}

Rml::Element* drop_target(Rml::Element* element)
{
    for (; element; element = element->GetParentNode()) {
        if (element->IsClassSet("preview_equipment_slot") || element->GetId() == "preview_inventory_board") { return element; }
    }
    return nullptr;
}

// One pointer gesture is a UI singleton. Borrow DOM pointers only during the
// event; resolve its indices against the frozen snapshot, never move DOM items.
std::optional<PreviewItemRequest> inventory_drop_request(const PlayPreviewState& preview,
    Rml::Element& source, Rml::Element& target, const Rml::Event& event)
{
    const auto& rows = preview.inventory_rows;
    if (source.GetOwnerDocument() != preview.inventory_document) { return std::nullopt; }
    if (source.IsClassSet("preview_inventory_item") && target.IsClassSet("preview_equipment_slot")) {
        const auto index = element_index(source, "data-item");
        const auto slot = element_index(target, "data-slot");
        if (!index || *index >= rows.inventory.size() || !slot || *slot >= equipment_slots.size()) { return std::nullopt; }
        return PreviewItemRequest{rows.inventory[*index].item, static_cast<int32_t>(*slot), PreviewItemAction::equip};
    }
    if (target.GetId() == "preview_inventory_board") {
        const auto cell = inventory_drag_cell(preview.inventory_drag, target, event, preview.inventory_page);
        if (source.IsClassSet("preview_equipment_slot")) {
            const auto slot = element_index(source, "data-slot");
            if (!slot || *slot >= equipment_slots.size() || !rows.equipment[*slot].assigned()) { return std::nullopt; }
            return PreviewItemRequest{rows.equipment[*slot].item, static_cast<int32_t>(*slot), PreviewItemAction::unequip, cell};
        }
        if (source.IsClassSet("preview_inventory_item")) {
            const auto index = element_index(source, "data-item");
            if (!index || *index >= rows.inventory.size()) { return std::nullopt; }
            return PreviewItemRequest{rows.inventory[*index].item, -1, PreviewItemAction::move_inventory, cell};
        }
    }
    return std::nullopt;
}

bool inventory_drop_allowed(const PlayPreviewState& preview, const PreviewItemRequest& request)
{
    const bool equip = request.action == PreviewItemAction::equip;
    if (equip) {
        const Vector<smalls::Value> args{
            nwn1::bridge::make_object_arg(preview.inventory_actor),
            nwn1::bridge::make_object_arg(request.item), smalls::Value::make_int(request.slot)};
        if (!nwn1::bridge::call_nwn1_module_bool("nwn1.item", "can_equip_item", args).value_or(false)) { return false; }
    }
    // Reuse the storage preflight for displaced gear and pack capacity. This is
    // advisory feedback only; the action revalidates at the simulation tick.
    const bool moving = request.action == PreviewItemAction::move_inventory;
    const std::array changes{EquipmentChange{request.slot, moving ? ObjectHandle{} : preview.inventory_rows.equipment[request.slot].item,
        equip ? request.item : ObjectHandle{}}};
    const std::array placements{InventoryPlacement{request.item, request.inventory_cell}};
    return prepare_equipment_changes(preview.inventory_actor,
        moving ? std::span<const EquipmentChange>{} : std::span<const EquipmentChange>{changes},
        equip ? std::span<const InventoryPlacement>{} : std::span<const InventoryPlacement>{placements})
        .has_value();
}

void append_equipment_slots(const PlayPreviewState& preview, std::string& markup)
{
    const auto& rows = preview.inventory_rows;
    for (size_t slot = 0; slot < equipment_slots.size(); ++slot) {
        const auto& view = equipment_slots[slot];
        const auto& row = rows.equipment[slot];
        const auto name = rows.text_view(row.name);
        markup += fmt::format("<button class=\"preview_equipment_slot {}\" data-slot=\"{}\" title=\"{}\" style=\"left:{}dp;top:{}dp;width:{}dp;height:{}dp\">",
            row.assigned() ? "assigned inventory_draggable" : "empty", slot,
            row.assigned() ? escaped(name) : view.name, view.x, view.y, view.width, view.height);
        const auto icon = rows.text_view(row.icon_source);
        if (row.assigned() && !icon.empty()) {
            markup += fmt::format("<span class=\"preview_equipment_image\" style=\"width:{}dp;height:{}dp\"><img style=\"left:-{}dp;top:-{}dp\" src=\"{}\"/></span>",
                row.icon_visible_width, row.icon_visible_height, row.icon_visible_x, row.icon_visible_y, escaped(icon));
        } else if (row.assigned()) {
            markup += "<span class=\"preview_icon_fallback\">" + escaped(name) + "</span>";
        } else {
            markup += fmt::format("<img class=\"preview_slot_placeholder\" src=\"inv_slot_{}.png\"/>", view.asset);
        }
        markup += "<span class=\"inventory_drop_outline\"></span></button>";
    }
}

void append_inventory_grid(const PlayPreviewState& preview, std::string& markup)
{
    const auto& rows = preview.inventory_rows;
    markup += fmt::format("<div id=\"preview_inventory_board\" style=\"width:{}dp;height:{}dp\">",
        rows.column_count * inventory_cell_dp, rows.row_count * inventory_cell_dp);
    for (int cell = 0; cell < rows.column_count * rows.row_count; ++cell) {
        markup += fmt::format("<span class=\"preview_inventory_cell\" style=\"left:{}dp;top:{}dp\"></span>",
            (cell % rows.column_count) * inventory_cell_dp, (cell / rows.column_count) * inventory_cell_dp);
    }
    for (size_t index = 0; index < rows.inventory.size(); ++index) {
        const auto& row = rows.inventory[index];
        if (row.page != preview.inventory_page) { continue; }
        const auto name = rows.text_view(row.name);
        markup += fmt::format("<button class=\"preview_inventory_item inventory_draggable{}\" data-item=\"{}\" title=\"{}\" style=\"left:{}dp;top:{}dp;width:{}dp;height:{}dp\">",
            row.item == preview.inventory_selection ? " selected" : "", index, escaped(name),
            row.column * inventory_cell_dp, (row.row - row.height + 1) * inventory_cell_dp,
            row.width * inventory_cell_dp, row.height * inventory_cell_dp);
        const auto icon = rows.text_view(row.icon_source);
        markup += icon.empty() ? "<span class=\"preview_icon_fallback\">" + escaped(name) + "</span>"
                               : "<img class=\"preview_inventory_icon\" src=\"" + escaped(icon) + "\"/>";
        if (row.stack_size > 1) {
            markup += fmt::format("<span class=\"preview_inventory_stack\">{}</span>", row.stack_size);
        }
        markup += "</button>";
    }
    markup += "<span class=\"inventory_drop_outline\"></span></div><div id=\"preview_inventory_pages\">";
    if (rows.page_count > 1) {
        const int previous = (preview.inventory_page + rows.page_count - 1) % rows.page_count;
        const int next = (preview.inventory_page + 1) % rows.page_count;
        markup += fmt::format("<button id=\"preview_inventory_previous\" class=\"preview_inventory_page\" data-page=\"{}\" title=\"Previous page\"><span class=\"inventory_chevron left\"></span></button><div class=\"preview_inventory_page_marks\">", previous);
        for (int page = 0; page < rows.page_count; ++page) {
            markup += fmt::format("<span class=\"preview_inventory_page_mark{}\"></span>", page == preview.inventory_page ? " active" : "");
        }
        markup += fmt::format("</div><button id=\"preview_inventory_next\" class=\"preview_inventory_page\" data-page=\"{}\" title=\"Next page\"><span class=\"inventory_chevron right\"></span></button>", next);
    }
    markup += "</div>";
}

} // namespace

bool open_play_preview_inventory(PlayPreviewState& preview, RuntimeInputState& input)
{
    if (!preview.session.active() || !preview.inventory_document) { return false; }
    if (!preview.inventory_visible) {
        preview.inventory_visible = true;
        preview.inventory_dirty = true;
        reset_play_preview_input(preview, input);
        input.pending.flags |= preview_input_cancel;
    }
    sync_play_preview_inventory(preview);
    return preview.inventory_visible;
}

void toggle_play_preview_inventory(PlayPreviewState& preview, RuntimeInputState& input)
{
    if (!preview.inventory_visible) {
        (void)open_play_preview_inventory(preview, input);
        return;
    }
    preview.inventory_visible = false;
    reset_play_preview_input(preview, input);
    sync_play_preview_inventory(preview);
}

void sync_play_preview_inventory(PlayPreviewState& preview)
{
    auto* doc = preview.inventory_document;
    if (!doc) { return; }
    auto* actor = preview.inventory_visible && preview.session.active()
        ? kernel::objects().get<Creature>(preview.session.actor())
        : nullptr;
    if (!actor) {
        preview.inventory_visible = false;
        doc->GetElementById("preview_inventory_tooltip")->SetClass("visible", false);
        if (preview.inventory_dragging || doc->GetElementById("preview_inventory_move")->IsPseudoClassSet("active")) {
            clear_inventory_drag_feedback(doc);
            preview.inventory_dragging = false;
            // Match pointer cancellation on window focus loss. Visibility is
            // already false, so releasing the native ghost cannot queue a drop.
            doc->GetContext()->ProcessMouseLeave();
            doc->GetContext()->ProcessMouseButtonUp(0, 0);
        }
        if (doc->IsVisible()) { doc->Hide(); }
        return;
    }
    const auto world = toolset_preview_item_world(preview.session);
    const bool actor_changed = preview.inventory_actor != actor->handle();
    const bool rows_changed = actor_changed || preview.inventory_revision != world.revision;
    // Replacing a source node during a native drag destroys RmlUi's borrowed
    // drag pointer. Keep the snapshot until release; stale drops are rejected.
    if (preview.inventory_dragging) {
        if (rows_changed) { clear_inventory_drag_feedback(doc); }
        return;
    }
    if (!preview.inventory_dirty && !rows_changed) { return; }
    if (actor_changed) {
        preview.inventory_selection = ObjectHandle{};
        preview.inventory_page = 0;
    }
    if (rows_changed && preview.inventory_icons) {
        build_object_inventory_rows(kernel::runtime(), actor->handle(), *preview.inventory_icons, preview.inventory_rows);
    }
    preview.inventory_dirty = false;
    preview.inventory_revision = world.revision;
    preview.inventory_actor = actor->handle();
    doc->GetElementById("preview_inventory_tooltip")->SetClass("visible", false);
    const auto& rows = preview.inventory_rows;
    if (preview.inventory_page < 0 || preview.inventory_page >= rows.page_count) { preview.inventory_page = 0; }
    std::string equipment, grid;
    if (rows.status == InventoryViewStatus::ready) {
        if (std::ranges::find(rows.inventory, preview.inventory_selection, &InventoryRow::item) == rows.inventory.end()) {
            preview.inventory_selection = ObjectHandle{};
        }
        append_equipment_slots(preview, equipment);
        append_inventory_grid(preview, grid);
    }
    doc->GetElementById("preview_equipment_slots")->SetInnerRML(equipment);
    doc->GetElementById("preview_inventory_grid")->SetInnerRML(grid);
    const auto message = rows.status == InventoryViewStatus::ready ? preview.item_message
        : rows.diagnostic.empty()                                  ? std::string{"Inventory display is unavailable."}
                                                                   : rows.diagnostic;
    auto* message_element = doc->GetElementById("preview_inventory_message");
    message_element->SetInnerRML(escaped(message));
    message_element->SetClass("visible", !message.empty());
    if (!doc->IsVisible()) { doc->Show(Rml::ModalFlag::Modal); }
}

void PlayPreviewInventoryListener::ProcessEvent(Rml::Event& event)
{
    if (!preview_.inventory_visible || !preview_.session.active()) { return; }
    auto* target = event.GetTargetElement();
    if (!target || target->GetOwnerDocument() != preview_.inventory_document) { return; }
    // The SDK handle owns window movement, including its default drag action.
    if (target->GetId() == "preview_inventory_move") { return; }
    const auto& type = event.GetType();
    if (type == "mouseover" || type == "mousemove" || type == "mouseout") {
        auto* tooltip = preview_.inventory_document->GetElementById("preview_inventory_tooltip");
        auto* control = target;
        while (control && !control->IsClassSet("preview_inventory_item") && !control->IsClassSet("preview_equipment_slot")) {
            control = control->GetParentNode();
        }
        const auto name = control ? control->GetAttribute<Rml::String>("title", "") : Rml::String{};
        const bool visible = type != "mouseout" && !preview_.inventory_dragging && !name.empty();
        tooltip->SetClass("visible", visible);
        if (visible) {
            const auto encoded = escaped(name);
            if (tooltip->GetInnerRML() != encoded) { tooltip->SetInnerRML(encoded); }
            const Rml::Vector2f dimensions{tooltip->GetContext()->GetDimensions()};
            const float x = event.GetParameter<float>("mouse_x", 0);
            const float y = event.GetParameter<float>("mouse_y", 0);
            // Anchor on the roomier side before layout; no stale tooltip size
            // is needed on the first hover or after a name changes.
            const bool right = x > dimensions.x / 2;
            const bool above = y > dimensions.y / 2;
            tooltip->SetProperty(right ? "left" : "right", "auto");
            tooltip->SetProperty(right ? "right" : "left", fmt::format("{}px", (right ? dimensions.x - x : x) + 12));
            tooltip->SetProperty(above ? "top" : "bottom", "auto");
            tooltip->SetProperty(above ? "bottom" : "top", fmt::format("{}px", (above ? dimensions.y - y : y) + 16));
        }
        return;
    }
    if (type == "dragend") {
        clear_inventory_drag_feedback(preview_.inventory_document);
        preview_.inventory_dragging = false;
        return;
    }
    if (type == "dragstart" || type == "dragover" || type == "dragmove" || type == "dragout" || type == "dragdrop") {
        event.StopPropagation();
        if (type == "dragout") {
            if (auto* destination = drop_target(target)) {
                destination->SetClass("inventory_drop_target", false);
                destination->SetClass("inventory_drop_invalid", false);
            }
            return;
        }
        if (preview_.pending_item || preview_.inventory_actor != preview_.session.actor()
            || preview_.inventory_revision != toolset_preview_item_world(preview_.session).revision) { return; }
        if (type == "dragstart") {
            preview_.inventory_document->GetElementById("preview_inventory_tooltip")->SetClass("visible", false);
            if (!target->IsClassSet("preview_inventory_item")
                && !(target->IsClassSet("preview_equipment_slot") && target->IsClassSet("assigned"))) { return; }
            ObjectHandle item;
            const bool carried = target->IsClassSet("preview_inventory_item");
            const auto index = element_index(*target, carried ? "data-item" : "data-slot");
            if (!index) { return; }
            if (carried) {
                if (*index >= preview_.inventory_rows.inventory.size()) { return; }
                item = preview_.inventory_rows.inventory[*index].item;
            } else {
                if (*index >= equipment_slots.size()) { return; }
                item = preview_.inventory_rows.equipment[*index].item;
            }
            if (!begin_inventory_drag(preview_.inventory_drag, item, *target, event, carried)) { return; }
            preview_.inventory_dragging = true;
            target->SetClass("inventory_drag_source", true);
            return;
        }
        auto* source = static_cast<Rml::Element*>(event.GetParameter<void*>("drag_element", nullptr));
        auto* destination = drop_target(target);
        if (!preview_.inventory_dragging || !source || !destination) { return; }
        const auto request = inventory_drop_request(preview_, *source, *destination, event);
        const bool allowed = request && inventory_drop_allowed(preview_, *request);
        if (request && destination->GetId() == "preview_inventory_board") {
            show_inventory_drag_cells(*destination, preview_.inventory_drag, request->inventory_cell, allowed);
        } else {
            destination->SetClass("inventory_drop_target", allowed && type != "dragdrop");
            destination->SetClass("inventory_drop_invalid", !allowed && type != "dragdrop");
        }
        if (type == "dragdrop" && request) {
            if (allowed) {
                preview_.pending_item = *request;
            } else {
                preview_.item_message = "Cannot move this item there. Check equipment requirements and pack space.";
                preview_.inventory_dirty = true;
            }
        }
        return;
    }
    if (preview_.inventory_dragging || (type != "click" && type != "keydown")) { return; }
    if (event.GetType() == "keydown") {
        if (event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN) != Rml::Input::KI_DELETE) { return; }
        event.StopPropagation();
        if (preview_.pending_item || preview_.inventory_actor != preview_.session.actor()
            || preview_.inventory_revision != toolset_preview_item_world(preview_.session).revision) { return; }
        const auto& rows = preview_.inventory_rows.inventory;
        const auto selected = std::ranges::find(rows, preview_.inventory_selection, &InventoryRow::item);
        if (selected != rows.end()) {
            preview_.pending_item = PreviewItemRequest{selected->item, -1, PreviewItemAction::drop};
        }
        return;
    }
    for (auto* element = target; element; element = element->GetParentNode()) {
        if (element->GetId() == "preview_inventory_close") {
            preview_.inventory_visible = false;
            sync_play_preview_inventory(preview_);
            event.StopPropagation();
            return;
        }
        if (preview_.inventory_actor != preview_.session.actor()
            || preview_.inventory_revision != toolset_preview_item_world(preview_.session).revision) { return; }
        if (element->IsClassSet("preview_inventory_page")) {
            event.StopPropagation();
            const auto page = element_index(*element, "data-page");
            if (!page || *page >= preview_.inventory_rows.page_count) { return; }
            preview_.inventory_page = static_cast<int32_t>(*page);
            preview_.inventory_dirty = true;
            return;
        }
        if (element->IsClassSet("preview_inventory_item")) {
            event.StopPropagation();
            const auto index = element_index(*element, "data-item");
            if (!index || *index >= preview_.inventory_rows.inventory.size()) { return; }
            const auto item = preview_.inventory_rows.inventory[*index].item;
            preview_.inventory_selection = preview_.inventory_selection == item ? ObjectHandle{} : item;
            preview_.inventory_dirty = true;
            return;
        }
        if (element->IsClassSet("preview_equipment_slot")) {
            event.StopPropagation();
            const auto slot = element_index(*element, "data-slot");
            if (!slot || *slot >= equipment_slots.size() || preview_.pending_item) { return; }
            const auto& rows = preview_.inventory_rows;
            const auto carried = std::ranges::find(rows.inventory, preview_.inventory_selection, &InventoryRow::item);
            if (carried != rows.inventory.end()) {
                preview_.pending_item = {carried->item, static_cast<int32_t>(*slot), PreviewItemAction::equip};
            } else if (rows.equipment[*slot].assigned()) {
                preview_.pending_item = PreviewItemRequest{rows.equipment[*slot].item, static_cast<int32_t>(*slot), PreviewItemAction::unequip};
            }
            return;
        }
    }
}

} // namespace nw::toolset
