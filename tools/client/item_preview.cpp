#include "item_preview.hpp"

#include "client_edit_trace.hpp"

#include <RmlUi/Core.h>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/Serialization.hpp>
#include <nw/smalls/runtime.hpp>

#include <nlohmann/json.hpp>

#include <array>

namespace nw::toolset {
namespace {
smalls::Value typed_object(smalls::Runtime& runtime, ObjectHandle object, ObjectType type)
{
    auto value = smalls::Value::make_object(object);
    value.type_id = runtime.object_subtype_for_tag(type);
    return value;
}

// The private equipped copy supplies only base-item/model/color inputs. Names,
// properties and inventory are not read by this visual consumer.
bool refresh_mannequin_colors(ItemPreviewState& state, smalls::Runtime& runtime)
{
    auto& objects = kernel::objects();
    auto* actor = objects.get<Creature>(state.mannequin.object());
    auto* copy = actor ? nw::get_equipped_item(actor, EquipIndex::chest) : nullptr;
    auto& components = objects.components();
    const auto* original_visual = components.find_item_visuals(state.item);
    const auto* copied_visual = copy ? components.find_item_visuals(copy->handle()) : nullptr;
    if (!original_visual || !copied_visual || original_visual->model_parts != copied_visual->model_parts) { return false; }
    // Store writes may relocate the native component array.
    const auto visual = *original_visual;
    const auto same_base = runtime.execute_script("toolset.item_preview", "same_base_item",
        {typed_object(runtime, state.item, ObjectType::item), typed_object(runtime, copy->handle(), ObjectType::item)});
    if (!same_base.ok() || same_base.value.type_id != runtime.bool_type() || !same_base.value.data.bval
        || !components.set_item_visuals(copy->handle(), visual.model_colors, visual.model_parts, visual.part_colors)) { return false; }

    const auto actor_value = typed_object(runtime, actor->handle(), ObjectType::creature);
    const auto configured = runtime.execute_script("toolset.item_preview", "configure_mannequin",
        {actor_value, typed_object(runtime, state.source, ObjectType::creature), smalls::Value::make_int(state.gender)});
    if (!configured.ok() || configured.value.type_id != runtime.int_type()
        || configured.value.data.ival < 0 || configured.value.data.ival > 1) { return false; }
    const auto refreshed = runtime.execute_script("nwn1.creature", "update_appearance_preview",
        {actor_value, smalls::Value::make_bool(true)});
    if (!refreshed.ok() || refreshed.value.type_id != runtime.bool_type() || !refreshed.value.data.bval) { return false; }
    state.displayed_gender = configured.value.data.ival;
    return true;
}
} // namespace

bool refresh_item_preview(ItemPreviewState& state, ObjectHandle item,
    ObjectHandle source, uint64_t mutation_epoch)
{
    auto& objects = kernel::objects();
    if (state.service_generation != 0 && state.service_generation != kernel::services().generation()) {
        // The previous object service already destroyed its trees.
        (void)state.mannequin.release();
    }
    if (item.type != ObjectType::item || !objects.valid(item)) {
        if (state.item.type == ObjectType::invalid) { return false; }
        state = {};
        return true;
    }
    if (source.type != ObjectType::creature || !objects.valid(source)) { source = ObjectHandle{}; }
    const auto resources = kernel::resman().generation();
    const auto services = kernel::services().generation();
    const bool changed_item = state.item != item || state.service_generation != services;
    if (!changed_item && state.source == source && state.mutation_epoch == mutation_epoch
        && state.resource_generation == resources && !state.dirty) { return false; }
    const ScopedClientEditPhase trace{ClientEditPhase::item_preview};
    const bool retain_mannequin = !changed_item && state.source == source
        && state.resource_generation == resources && !state.dirty && state.armor;
    if (changed_item) {
        state.gender = -1;
        state.dragging = false;
    }
    state.item = item;
    state.source = source;
    state.mutation_epoch = mutation_epoch;
    state.resource_generation = resources;
    state.service_generation = services;
    state.dirty = false;
    ++state.revision;
    state.icon.clear();
    state.diagnostic.clear();
    state.armor = false;

    auto& runtime = kernel::runtime();
    const auto item_value = typed_object(runtime, item, ObjectType::item);
    const auto classified = runtime.execute_script("toolset.item_preview", "requires_mannequin", {item_value});
    if (!classified.ok() || classified.value.type_id != runtime.bool_type()) {
        state.mannequin.reset();
        state.diagnostic = "Item preview is unavailable.";
        return true;
    }
    state.armor = classified.value.data.bval;
    const auto updated = runtime.execute_script("nwn1.item", "update_item_icons", {item_value});
    if (updated.ok() && updated.value.type_id == runtime.bool_type() && updated.value.data.bval) {
        ItemIconBatch images;
        const std::array items{item};
        build_item_icon_images(0, items, state.icons, images);
        state.icon = std::move(images.sources.front());
    }
    if (state.icon.empty()) { state.diagnostic = "Inventory icon unavailable."; }
    if (!state.armor) {
        state.mannequin.reset();
        return true;
    }

    if (retain_mannequin && refresh_mannequin_colors(state, runtime)) { return true; }

    state.mannequin.reset();

    auto* actor = objects.make<Creature>();
    if (!actor || !state.mannequin.adopt(actor->handle())) {
        if (actor) { objects.destroy(actor->handle()); }
        state.diagnostic = "Could not create the armor mannequin.";
        return true;
    }
    const auto fail = [&] {
        state.mannequin.reset();
        state.diagnostic = "Armor mannequin unavailable. Check the loaded body and armor resources.";
    };
    nlohmann::json armor;
    if (!serialize(objects.get<Item>(item), armor, SerializationProfile::instance)) {
        fail();
        return true;
    }
    auto* copy = objects.make<Item>();
    if (!copy) {
        fail();
        return true;
    }
    actor->equipment.equips[static_cast<size_t>(EquipIndex::chest)] = copy->handle();
    if (!deserialize(copy, armor, SerializationProfile::instance)) {
        fail();
        return true;
    }
    const auto configured = runtime.execute_script("toolset.item_preview", "configure_mannequin",
        {typed_object(runtime, actor->handle(), ObjectType::creature),
            typed_object(runtime, source, ObjectType::creature), smalls::Value::make_int(state.gender)});
    if (!configured.ok() || configured.value.type_id != runtime.int_type()
        || configured.value.data.ival < 0 || configured.value.data.ival > 1 || !actor->instantiate()) {
        fail();
        return true;
    }
    state.displayed_gender = configured.value.data.ival;
    return true;
}

bool select_item_preview_gender(ItemPreviewState& state, ObjectHandle item, int32_t gender)
{
    if (state.item != item || !state.armor || gender < 0 || gender > 1
        || !kernel::objects().valid(item) || state.gender == gender) { return false; }
    state.gender = gender;
    state.dirty = true;
    return true;
}

void hydrate_item_preview(Rml::ElementDocument* document, const ItemPreviewState& state)
{
    if (!document) { return; }
    if (auto* panel = document->GetElementById("item_preview")) {
        panel->SetClass("armor", state.armor);
        panel->SetClass("preview_3d", state.preview_object().type != ObjectType::invalid);
        Rml::ElementList buttons;
        panel->GetElementsByClassName(buttons, "item_preview_gender");
        for (auto* button : buttons) {
            button->SetClass("active", button->GetAttribute<int>("data-gender", -1) == state.displayed_gender);
        }
    }
    if (auto* icon = document->GetElementById("item_preview_icon_image")) {
        if (icon->GetAttribute<Rml::String>("src", "") != state.icon) {
            icon->SetAttribute("src", state.icon);
        }
        icon->SetProperty("display", state.icon.empty() ? "none" : "block");
    }
    if (auto* status = document->GetElementById("item_preview_status")) {
        // Diagnostics are fixed UI text, never resource-authored markup.
        if (status->GetInnerRML() != state.diagnostic) { status->SetInnerRML(state.diagnostic); }
        status->SetClass("empty", state.diagnostic.empty());
    }
}

} // namespace nw::toolset
