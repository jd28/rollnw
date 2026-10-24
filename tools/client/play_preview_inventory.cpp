#include "play_preview_inventory.hpp"
#include "runtime_input.hpp"

#include <RmlUi/Core.h>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>

#include <charconv>

namespace nw::toolset {
namespace {

std::string item_name(const Item& item)
{
    auto name = kernel::strings().get(item.name);
    return Rml::StringUtilities::EncodeRml(name.empty() ? "Unnamed item" : name);
}

void append_item_action(PlayPreviewState& preview, std::string& markup,
    PreviewItemRequest request, const char* label)
{
    const size_t index = preview.inventory_actions.size();
    preview.inventory_actions.push_back(request);
    markup += "<button class=\"preview_item_action\" data-index=\"" + std::to_string(index)
        + "\">" + label + "</button>";
}

} // namespace

void toggle_play_preview_inventory(PlayPreviewState& preview, RuntimeInputState& input)
{
    if (!preview.session.active()) { return; }
    preview.inventory_visible = !preview.inventory_visible;
    preview.inventory_dirty = true;
    reset_play_preview_input(preview, input);
    if (preview.inventory_visible) { input.pending.flags |= preview_input_cancel; }
    sync_play_preview_inventory(preview);
}

void sync_play_preview_inventory(PlayPreviewState& preview)
{
    auto* doc = preview.inventory_document;
    if (!doc) { return; }
    if (!preview.inventory_visible || !preview.session.active()) {
        if (doc->IsVisible()) { doc->Hide(); }
        return;
    }
    auto* actor = kernel::objects().get<Creature>(preview.session.actor());
    if (!actor) {
        preview.inventory_visible = false;
        doc->Hide();
        return;
    }
    const auto world = toolset_preview_item_world(preview.session);
    if (!preview.inventory_dirty && preview.inventory_revision == world.revision
        && preview.inventory_actor == actor->handle()) { return; }
    preview.inventory_dirty = false;
    preview.inventory_revision = world.revision;
    preview.inventory_actor = actor->handle();
    preview.inventory_actions.clear();

    std::string markup = "<h3>Nearby ground items</h3>";
    size_t nearby_count = 0;
    const auto* actor_spatial = kernel::objects().components().find_spatial(actor->handle());
    for (const auto handle : world.ground) {
        const auto* item = kernel::objects().get<Item>(handle);
        const auto* spatial = kernel::objects().components().find_spatial(handle);
        if (!item || !spatial || !actor_spatial
            || glm::distance(spatial->position, actor_spatial->position) > 2.0f) { continue; }
        ++nearby_count;
        markup += "<div class=\"preview_item_row\"><span>" + item_name(*item) + "</span>";
        append_item_action(preview, markup, {handle, -1, PreviewItemAction::pickup}, "Pick up");
        markup += "</div>";
    }
    if (!nearby_count) { markup += "<p>No items within reach. Close inventory and move closer.</p>"; }
    markup += "<h3>Inventory</h3>";
    if (actor->inventory().items.empty()) { markup += "<p>Empty</p>"; }
    for (const auto& row : actor->inventory().items) {
        const auto* item = inventory_item_ptr(row);
        if (!item) { continue; }
        markup += "<div class=\"preview_item_row\"><span>" + item_name(*item) + "</span>";
        append_item_action(preview, markup, {item->handle(), -1, PreviewItemAction::equip}, "Equip");
        append_item_action(preview, markup, {item->handle(), -1, PreviewItemAction::drop}, "Drop");
        markup += "</div>";
    }
    markup += "<h3>Equipped</h3>";
    size_t equipped_count = 0;
    for (int32_t slot = 0; slot < 18; ++slot) {
        const auto index = static_cast<EquipIndex>(slot);
        const auto* item = get_equipped_item(actor, index);
        if (!item) { continue; }
        ++equipped_count;
        markup += "<div class=\"preview_item_row\"><span>"
            + std::string{equip_index_to_string(index)} + ": " + item_name(*item) + "</span>";
        append_item_action(preview, markup, {item->handle(), slot, PreviewItemAction::unequip}, "Unequip");
        markup += "</div>";
    }
    if (!equipped_count) { markup += "<p>Nothing equipped</p>"; }
    doc->GetElementById("preview_inventory_rows")->SetInnerRML(markup);
    doc->GetElementById("preview_inventory_message")->SetInnerRML(Rml::StringUtilities::EncodeRml(preview.item_message));
    if (!doc->IsVisible()) { doc->Show(Rml::ModalFlag::Modal); }
}

void PlayPreviewInventoryListener::ProcessEvent(Rml::Event& event)
{
    if (event.GetType() != "click" || !preview_.inventory_visible
        || !preview_.session.active()) { return; }
    auto* target = event.GetTargetElement();
    if (!target || target->GetOwnerDocument() != preview_.inventory_document) { return; }
    for (auto* element = target; element; element = element->GetParentNode()) {
        if (element->GetId() == "preview_inventory_close") {
            preview_.inventory_visible = false;
            sync_play_preview_inventory(preview_);
            event.StopPropagation();
            return;
        }
        if (!element->IsClassSet("preview_item_action")) { continue; }
        event.StopPropagation();
        if (preview_.pending_item || preview_.inventory_actor != preview_.session.actor()) { return; }
        const auto text = element->GetAttribute<Rml::String>("data-index", "");
        size_t index = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), index);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
            || index >= preview_.inventory_actions.size()) { return; }
        auto request = preview_.inventory_actions[index];
        if (request.action == PreviewItemAction::equip) {
            auto* slot = dynamic_cast<Rml::ElementFormControlSelect*>(
                preview_.inventory_document->GetElementById("preview_inventory_slot"));
            if (!slot || slot->GetSelection() < 0 || slot->GetSelection() >= 14) { return; }
            request.slot = slot->GetSelection();
        }
        preview_.pending_item = request;
        return;
    }
}

} // namespace nw::toolset
