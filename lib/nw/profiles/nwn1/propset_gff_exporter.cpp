#include "propset_gff_exporter.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../kernel/Rules.hpp"
#include "../../kernel/Strings.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/Door.hpp"
#include "../../objects/Encounter.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectBase.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../objects/Placeable.hpp"
#include "../../objects/Sound.hpp"
#include "../../objects/Store.hpp"
#include "../../objects/Trigger.hpp"
#include "../../objects/Waypoint.hpp"
#include "../../resources/assets.hpp"
#include "../../rules/RuntimeObject.hpp"
#include "../../serialization/GffBuilder.hpp"
#include "../../smalls/Array.hpp"
#include "../../smalls/runtime.hpp"
#include "../../util/HandlePool.hpp"
#include "legacy_spellbook_gff.hpp"
#include "propset_gff_object_io.hpp"

#include <fmt/format.h>

#include <array>

namespace nwn1 {

namespace {

constexpr size_t item_model_color_count = 6;
constexpr size_t item_model_part_count = 19;
constexpr size_t creature_class_slot_count = 8;

struct ItemPartGffField {
    size_t part = 0;
    const char* extended = nullptr;
    const char* legacy = nullptr;
};

constexpr ItemPartGffField armor_part_fields[] = {
    {nw::ItemModelParts::armor_belt, "xArmorPart_Belt", "ArmorPart_Belt"},
    {nw::ItemModelParts::armor_lbicep, "xArmorPart_LBice", "ArmorPart_LBicep"},
    {nw::ItemModelParts::armor_lfarm, "xArmorPart_LFArm", "ArmorPart_LFArm"},
    {nw::ItemModelParts::armor_lfoot, "xArmorPart_LFoot", "ArmorPart_LFoot"},
    {nw::ItemModelParts::armor_lhand, "xArmorPart_LHand", "ArmorPart_LHand"},
    {nw::ItemModelParts::armor_lshin, "xArmorPart_LShin", "ArmorPart_LShin"},
    {nw::ItemModelParts::armor_lshoul, "xArmorPart_LShou", "ArmorPart_LShoul"},
    {nw::ItemModelParts::armor_lthigh, "xArmorPart_LThig", "ArmorPart_LThigh"},
    {nw::ItemModelParts::armor_neck, "xArmorPart_Neck", "ArmorPart_Neck"},
    {nw::ItemModelParts::armor_pelvis, "xArmorPart_Pelvi", "ArmorPart_Pelvis"},
    {nw::ItemModelParts::armor_rbicep, "xArmorPart_RBice", "ArmorPart_RBicep"},
    {nw::ItemModelParts::armor_rfarm, "xArmorPart_RFArm", "ArmorPart_RFArm"},
    {nw::ItemModelParts::armor_rfoot, "xArmorPart_RFoot", "ArmorPart_RFoot"},
    {nw::ItemModelParts::armor_rhand, "xArmorPart_RHand", "ArmorPart_RHand"},
    {nw::ItemModelParts::armor_robe, "xArmorPart_Robe", "ArmorPart_Robe"},
    {nw::ItemModelParts::armor_rshin, "xArmorPart_RShin", "ArmorPart_RShin"},
    {nw::ItemModelParts::armor_rshoul, "xArmorPart_RShou", "ArmorPart_RShoul"},
    {nw::ItemModelParts::armor_rthigh, "xArmorPart_RThig", "ArmorPart_RThigh"},
    {nw::ItemModelParts::armor_torso, "xArmorPart_Torso", "ArmorPart_Torso"},
};

constexpr ItemPartGffField model_part_fields[] = {
    {nw::ItemModelParts::model1, "xModelPart1", "ModelPart1"},
    {nw::ItemModelParts::model2, "xModelPart2", "ModelPart2"},
    {nw::ItemModelParts::model3, "xModelPart3", "ModelPart3"},
};

constexpr const char* color_fields[] = {
    "Cloth1Color",
    "Cloth2Color",
    "Leather1Color",
    "Leather2Color",
    "Metal1Color",
    "Metal2Color",
};

struct ItemVisualData {
    std::array<uint8_t, item_model_color_count> model_colors{};
    std::array<uint16_t, item_model_part_count> model_parts{};
    std::array<std::array<uint8_t, item_model_color_count>, item_model_part_count> part_colors{};
};

ItemVisualData default_item_visual_data()
{
    ItemVisualData result{};
    for (auto& part_color : result.part_colors) {
        part_color.fill(255);
    }
    return result;
}

LegacySpellBook export_ability_loadout_as_spellbook(nw::ObjectHandle owner, nw::Class class_id)
{
    LegacySpellBook result;
    if (class_id == nw::Class::invalid()) { return result; }

    const auto* loadout = nw::kernel::objects().components().find_ability_loadout(owner);
    if (!loadout) { return result; }

    const int32_t source = *class_id;
    const size_t levels = nw::kernel::rules().maximum_spell_levels();
    for (const nw::ObjectAbilityLoadoutEntry& entry : loadout->entries) {
        if (entry.source != source
            || entry.tier < 0
            || static_cast<size_t>(entry.tier) >= levels) {
            continue;
        }

        auto tier = static_cast<size_t>(entry.tier);
        if (entry.slot == -1) {
            if (entry.ability >= 0) {
                result.known[tier].push_back(nw::Spell::make(entry.ability));
            }
            continue;
        }

        if (entry.slot < 0) { continue; }

        const size_t slot = static_cast<size_t>(entry.slot);
        if (result.memorized[tier].size() <= slot) {
            result.memorized[tier].resize(slot + 1);
        }
        if (entry.ability >= 0) {
            result.memorized[tier][slot] = LegacySpellBookEntry{
                .spell = nw::Spell::make(entry.ability),
                .meta = nw::MetaMagicCode::make(entry.modifier),
                .flags = static_cast<nw::SpellFlags>(entry.flags),
            };
        }
    }

    return result;
}

uint8_t clamp_visual_u8(int32_t value) noexcept
{
    if (value < 0) { return 0; }
    if (value > 255) { return 255; }
    return static_cast<uint8_t>(value);
}

uint16_t clamp_visual_u16(int32_t value) noexcept
{
    if (value < 0) { return 0; }
    if (value > 65535) { return 65535; }
    return static_cast<uint16_t>(value);
}

bool fixed_int_array_offset(
    nw::smalls::Runtime& rt,
    const nw::smalls::StructDef* def,
    const char* name,
    size_t expected_count,
    uint32_t& out)
{
    if (!def) { return false; }

    uint32_t field = def->field_index(name);
    if (field == UINT32_MAX) { return false; }

    const nw::smalls::Type* type = rt.get_type(def->fields[field].type_id);
    if (!type || type->type_kind != nw::smalls::TK_fixed_array
        || !type->type_params[0].is<nw::smalls::TypeID>()
        || !type->type_params[1].is<int32_t>()) {
        return false;
    }

    const nw::smalls::Type* elem = rt.get_type(type->type_params[0].as<nw::smalls::TypeID>());
    if (!elem || elem->type_kind != nw::smalls::TK_primitive
        || elem->primitive_kind != nw::smalls::PK_int) {
        return false;
    }

    int32_t count = type->type_params[1].as<int32_t>();
    if (count < 0 || static_cast<size_t>(count) < expected_count) {
        return false;
    }

    out = def->fields[field].offset;
    return true;
}

template <size_t N>
bool read_visual_u8_array(
    nw::smalls::Runtime& rt,
    const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def,
    const char* name,
    std::array<uint8_t, N>& out)
{
    uint32_t offset = 0;
    if (!fixed_int_array_offset(rt, def, name, N, offset)) {
        return false;
    }

    for (size_t i = 0; i < N; ++i) {
        nw::smalls::Value value = rt.read_value_field_at_offset(
            ref, offset + uint32_t(i) * 4u, rt.int_type());
        out[i] = clamp_visual_u8(value.data.ival);
    }
    return true;
}

template <size_t N>
bool read_visual_u16_array(
    nw::smalls::Runtime& rt,
    const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def,
    const char* name,
    std::array<uint16_t, N>& out)
{
    uint32_t offset = 0;
    if (!fixed_int_array_offset(rt, def, name, N, offset)) {
        return false;
    }

    for (size_t i = 0; i < N; ++i) {
        nw::smalls::Value value = rt.read_value_field_at_offset(
            ref, offset + uint32_t(i) * 4u, rt.int_type());
        out[i] = clamp_visual_u16(value.data.ival);
    }
    return true;
}

bool read_visual_part_colors(
    nw::smalls::Runtime& rt,
    const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def,
    std::array<std::array<uint8_t, item_model_color_count>, item_model_part_count>& out)
{
    constexpr size_t total_count = item_model_part_count * item_model_color_count;

    uint32_t offset = 0;
    if (!fixed_int_array_offset(rt, def, "part_colors", total_count, offset)) {
        return false;
    }

    for (size_t i = 0; i < total_count; ++i) {
        nw::smalls::Value value = rt.read_value_field_at_offset(
            ref, offset + uint32_t(i) * 4u, rt.int_type());
        out[i / item_model_color_count][i % item_model_color_count] = clamp_visual_u8(value.data.ival);
    }
    return true;
}

bool read_item_visual_data_from_propset(nw::smalls::Runtime& rt, nw::ObjectHandle handle, ItemVisualData& out)
{
    if (handle.id == nw::object_invalid || handle.type != nw::ObjectType::item) {
        return false;
    }

    nw::smalls::TypeID tid = rt.type_id("core.item.ItemVisuals", false);
    if (tid == nw::smalls::invalid_type_id) {
        return false;
    }

    nw::smalls::Value ref = rt.find_propset_ref(tid, handle);
    if (ref.type_id == nw::smalls::invalid_type_id) {
        return false;
    }

    const nw::smalls::StructDef* def = rt.get_struct_def(tid);
    if (!def) {
        return false;
    }

    ItemVisualData data{};
    if (!read_visual_u8_array(rt, ref, def, "model_colors", data.model_colors)
        || !read_visual_u16_array(rt, ref, def, "model_parts", data.model_parts)
        || !read_visual_part_colors(rt, ref, def, data.part_colors)) {
        return false;
    }

    out = data;
    return true;
}

nw::ItemModelType item_model_type_for_export(nw::smalls::Runtime& rt, nw::ObjectHandle handle)
{
    if (handle.id == nw::object_invalid || handle.type != nw::ObjectType::item) {
        return nw::ItemModelType::simple;
    }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::detail::make_value(&rt, handle));
    auto result = rt.execute_script("nwn1.item", "get_model_type", args);
    if (!result.ok() || result.value.type_id != rt.int_type()) {
        return nw::ItemModelType::simple;
    }

    switch (result.value.data.ival) {
    case static_cast<int32_t>(nw::ItemModelType::layered):
        return nw::ItemModelType::layered;
    case static_cast<int32_t>(nw::ItemModelType::composite):
        return nw::ItemModelType::composite;
    case static_cast<int32_t>(nw::ItemModelType::armor):
        return nw::ItemModelType::armor;
    default:
        return nw::ItemModelType::simple;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PropsetGffExporter::PropsetGffExporter(nw::smalls::Runtime* rt,
    const PropsetGffPolicyRegistry* registry)
    : rt_(rt)
    , registry_(registry)
{
}

// ---------------------------------------------------------------------------
// Per-object entry points
// ---------------------------------------------------------------------------

bool PropsetGffExporter::export_object_propset(const nw::ObjectBase* obj, const char* qname,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile) const
{
    if (!obj || !qname || !rt_ || !registry_) { return false; }

    const PropsetGffPolicy* policy = registry_->find(qname);
    if (!policy) { return false; }

    nw::smalls::TypeID tid = rt_->type_id(qname, false);
    if (tid == nw::smalls::invalid_type_id) { return false; }

    nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return false; }

    const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
    if (!def) { return false; }

    return export_propset(ref, def, *policy, out, profile);
}

void PropsetGffExporter::export_creature(const nw::Creature* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.creature.CreatureDescriptor",
        "core.creature.CreatureAppearance",
        "core.creature.CreatureStats",
        "core.creature.CreatureHealth",
        "core.creature.CreatureCombat",
    };
    for (auto* qname : names) {
        export_object_propset(obj, qname, out, profile);
    }
    export_creature_levels(obj, out, profile);
}

void PropsetGffExporter::export_creature_levels(const nw::Creature* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile /*profile*/)
{
    auto& class_list = out.add_list("ClassList");
    if (!obj || !rt_) {
        out.add_field("WalkRate", 0);
        return;
    }

    nw::smalls::TypeID tid = rt_->type_id("core.creature.CreatureLevels", false);
    if (tid == nw::smalls::invalid_type_id) {
        out.add_field("WalkRate", 0);
        return;
    }

    nw::smalls::Value ref = rt_->find_propset_ref(tid, obj->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) {
        out.add_field("WalkRate", 0);
        return;
    }

    const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
    if (!def) {
        out.add_field("WalkRate", 0);
        return;
    }

    auto field_offset = [def](const char* field) -> uint32_t {
        uint32_t index = def->field_index(field);
        return index == UINT32_MAX ? UINT32_MAX : def->fields[index].offset;
    };

    const uint32_t classes = field_offset("classes");
    const uint32_t class_levels = field_offset("class_levels");
    if (classes != UINT32_MAX && class_levels != UINT32_MAX) {
        for (size_t i = 0; i < creature_class_slot_count; ++i) {
            nw::Class class_id = nw::Class::make(read_int(ref, classes + uint32_t(i) * 4u));
            if (class_id == nw::Class::invalid()) { continue; }

            auto& class_struct = class_list.push_back(3)
                                     .add_field("Class", *class_id)
                                     .add_field("ClassLevel", read_int(ref, class_levels + uint32_t(i) * 4u));
            LegacySpellBook spellbook = export_ability_loadout_as_spellbook(obj->handle(), class_id);
            serialize_legacy_spellbook(spellbook, class_struct);
        }
    }

    const uint32_t walkrate = field_offset("walkrate");
    out.add_field("WalkRate", walkrate == UINT32_MAX ? 0 : read_int(ref, walkrate));
}

void PropsetGffExporter::export_item(const nw::Item* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.item.ItemDescriptor",
        "core.item.ItemStats",
    };
    for (auto* qname : names) {
        const PropsetGffPolicy* policy = registry_->find(qname);
        if (!policy) { continue; }

        nw::smalls::TypeID tid = rt_->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { continue; }

        const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
        if (!def) { continue; }

        export_propset(ref, def, *policy, out, profile);
    }

    export_item_visuals(obj, out, profile);
}

void PropsetGffExporter::export_item_visuals(const nw::Item* obj,
    nw::GffBuilderStruct& out,
    nw::SerializationProfile /*profile*/) const
{
    if (!obj || !rt_) { return; }

    ItemVisualData visual_data = default_item_visual_data();
    read_item_visual_data_from_propset(*rt_, obj->handle(), visual_data);
    nw::ItemModelType model_type = item_model_type_for_export(*rt_, obj->handle());

    if (model_type == nw::ItemModelType::armor) {
        for (const auto& field : armor_part_fields) {
            out.add_field(field.legacy, static_cast<uint8_t>(visual_data.model_parts[field.part]));
            out.add_field(field.extended, visual_data.model_parts[field.part]);
        }

        for (size_t part = 0; part < item_model_part_count; ++part) {
            for (size_t color = 0; color < item_model_color_count; ++color) {
                if (visual_data.part_colors[part][color] == 255) { continue; }
                out.add_field(fmt::format("APart_{}_Col_{}", part, color),
                    visual_data.part_colors[part][color]);
            }
        }
    } else if (model_type == nw::ItemModelType::composite) {
        for (const auto& field : model_part_fields) {
            out.add_field(field.legacy, static_cast<uint8_t>(visual_data.model_parts[field.part]));
            out.add_field(field.extended, visual_data.model_parts[field.part]);
        }
    } else {
        out.add_field("ModelPart1", static_cast<uint8_t>(visual_data.model_parts[nw::ItemModelParts::model1]));
        out.add_field("xModelPart1", visual_data.model_parts[nw::ItemModelParts::model1]);
    }

    bool composite_has_model_colors = false;
    if (model_type == nw::ItemModelType::composite) {
        for (auto color : visual_data.model_colors) {
            if (color != 0) {
                composite_has_model_colors = true;
                break;
            }
        }
    }

    if (model_type == nw::ItemModelType::layered
        || model_type == nw::ItemModelType::armor
        || composite_has_model_colors) {
        for (size_t i = 0; i < item_model_color_count; ++i) {
            out.add_field(color_fields[i], visual_data.model_colors[i]);
        }
    }
}

void PropsetGffExporter::export_door(const nw::Door* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.door.DoorState",
    };
    for (auto* qname : names) {
        const PropsetGffPolicy* policy = registry_->find(qname);
        if (!policy) { continue; }

        nw::smalls::TypeID tid = rt_->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { continue; }

        const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
        if (!def) { continue; }

        export_propset(ref, def, *policy, out, profile);
    }
}

void PropsetGffExporter::export_encounter(const nw::Encounter* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.encounter.EncounterState",
    };
    for (auto* qname : names) {
        const PropsetGffPolicy* policy = registry_->find(qname);
        if (!policy) { continue; }

        nw::smalls::TypeID tid = rt_->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { continue; }

        const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
        if (!def) { continue; }

        export_propset(ref, def, *policy, out, profile);
    }
}

void PropsetGffExporter::export_placeable(const nw::Placeable* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.placeable.PlaceableState",
    };
    for (auto* qname : names) {
        const PropsetGffPolicy* policy = registry_->find(qname);
        if (!policy) { continue; }

        nw::smalls::TypeID tid = rt_->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { continue; }

        const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
        if (!def) { continue; }

        export_propset(ref, def, *policy, out, profile);
    }
}

void PropsetGffExporter::export_sound(const nw::Sound* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.sound.SoundState",
    };
    for (auto* qname : names) {
        const PropsetGffPolicy* policy = registry_->find(qname);
        if (!policy) { continue; }

        nw::smalls::TypeID tid = rt_->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { continue; }

        const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
        if (!def) { continue; }

        export_propset(ref, def, *policy, out, profile);
    }
}

void PropsetGffExporter::export_store(const nw::Store* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.store.StoreState",
    };
    for (auto* qname : names) {
        const PropsetGffPolicy* policy = registry_->find(qname);
        if (!policy) { continue; }

        nw::smalls::TypeID tid = rt_->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { continue; }

        const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
        if (!def) { continue; }

        export_propset(ref, def, *policy, out, profile);
    }
}

void PropsetGffExporter::export_trigger(const nw::Trigger* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.trigger.TriggerState",
    };
    for (auto* qname : names) {
        const PropsetGffPolicy* policy = registry_->find(qname);
        if (!policy) { continue; }

        nw::smalls::TypeID tid = rt_->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { continue; }

        const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
        if (!def) { continue; }

        export_propset(ref, def, *policy, out, profile);
    }
}

void PropsetGffExporter::export_waypoint(const nw::Waypoint* obj, nw::GffBuilderStruct& out,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.waypoint.WaypointState",
    };
    for (auto* qname : names) {
        const PropsetGffPolicy* policy = registry_->find(qname);
        if (!policy) { continue; }

        nw::smalls::TypeID tid = rt_->type_id(qname, false);
        if (tid == nw::smalls::invalid_type_id) { continue; }

        nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
        if (ref.type_id == nw::smalls::invalid_type_id) { continue; }

        const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
        if (!def) { continue; }

        export_propset(ref, def, *policy, out, profile);
    }
}

// ---------------------------------------------------------------------------
// Core export logic
// ---------------------------------------------------------------------------

bool PropsetGffExporter::export_propset(const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def,
    const PropsetGffPolicy& policy,
    nw::GffBuilderStruct& out,
    nw::SerializationProfile profile) const
{
    for (const auto& fp : policy.fields) {
        if (fp.encoding == GffEncoding::skip) { continue; }
        if (fp.import_only) { continue; }
        if (fp.instance_only && profile == nw::SerializationProfile::blueprint) { continue; }
        if (fp.blueprint_only && profile != nw::SerializationProfile::blueprint) { continue; }

        uint32_t idx_a = def->field_index(fp.propset_field_name);
        if (idx_a == UINT32_MAX) { continue; }
        const nw::smalls::FieldDef& field_a = def->fields[idx_a];

        switch (fp.encoding) {
        case GffEncoding::scalar:
            export_scalar(fp, field_a, ref, out);
            break;
        case GffEncoding::spread:
            export_spread(fp, field_a, ref, out);
            break;
        case GffEncoding::list_scalar:
            export_list_scalar(fp, field_a, ref, out);
            break;
        case GffEncoding::list_paired: {
            uint32_t idx_b = def->field_index(fp.list_paired.propset_field_b);
            if (idx_b == UINT32_MAX) { break; }
            export_list_paired(fp, &field_a, &def->fields[idx_b], ref, out);
            break;
        }
        case GffEncoding::list_struct:
            export_list_struct(fp, field_a, ref, out);
            break;
        case GffEncoding::skip:
            break;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Encoding-specific helpers
// ---------------------------------------------------------------------------

int32_t PropsetGffExporter::read_int(const nw::smalls::Value& ref, uint32_t offset) const
{
    nw::smalls::Value v = rt_->read_value_field_at_offset(ref, offset, rt_->int_type());
    return v.data.ival;
}

float PropsetGffExporter::read_float(const nw::smalls::Value& ref, uint32_t offset) const
{
    nw::smalls::Value v = rt_->read_value_field_at_offset(ref, offset, rt_->float_type());
    return v.data.fval;
}

nw::Resref PropsetGffExporter::read_resref(const nw::smalls::Value& ref,
    const nw::smalls::FieldDef& field) const
{
    nw::smalls::Value v = rt_->read_value_field_at_offset(ref, field.offset, field.type_id);
    auto* ptr = static_cast<nw::Resref*>(rt_->get_value_data_ptr(v));
    return ptr ? *ptr : nw::Resref{};
}

nw::String PropsetGffExporter::read_string(const nw::smalls::Value& ref,
    const nw::smalls::FieldDef& field) const
{
    nw::smalls::Value value = rt_->read_value_field_at_offset(ref, field.offset, field.type_id);
    if (value.type_id != rt_->string_type() || value.data.hptr.value == 0) {
        return {};
    }
    return nw::String{nw::smalls::ScriptString{value.data.hptr}.view(*rt_)};
}

nw::LocString PropsetGffExporter::read_locstring(const nw::smalls::Value& ref,
    const nw::smalls::FieldDef& field) const
{
    nw::smalls::Value value = rt_->read_value_field_at_offset(ref, field.offset, field.type_id);
    auto* ptr = static_cast<nw::TextRef*>(rt_->get_value_data_ptr(value));
    return ptr ? nw::kernel::strings().to_locstring(*ptr) : nw::LocString{};
}

nw::smalls::IArray* PropsetGffExporter::get_array(const nw::smalls::Value& ref,
    const nw::smalls::FieldDef& field) const
{
    nw::smalls::Value arr_val = rt_->read_value_field_at_offset(ref, field.offset, field.type_id);
    nw::TypedHandle h = nw::TypedHandle::from_ull(arr_val.data.handle);
    return rt_->object_pool().get_unmanaged_array(h);
}

void PropsetGffExporter::export_scalar(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef& field,
    const nw::smalls::Value& ref,
    nw::GffBuilderStruct& out) const
{
    const auto& sm = fp.scalar;

    auto emit = [&](const char* label, nw::SerializationType::type gff_type) {
        if (!label) { return; }
        switch (gff_type) {
        case nw::SerializationType::uint8:
            out.add_field(label, static_cast<uint8_t>(read_int(ref, field.offset)));
            break;
        case nw::SerializationType::int8:
            out.add_field(label, static_cast<int8_t>(read_int(ref, field.offset)));
            break;
        case nw::SerializationType::uint16:
            out.add_field(label, static_cast<uint16_t>(read_int(ref, field.offset)));
            break;
        case nw::SerializationType::int16:
            out.add_field(label, static_cast<int16_t>(read_int(ref, field.offset)));
            break;
        case nw::SerializationType::uint32:
            out.add_field(label, static_cast<uint32_t>(read_int(ref, field.offset)));
            break;
        case nw::SerializationType::int32:
            out.add_field(label, read_int(ref, field.offset));
            break;
        case nw::SerializationType::float_:
            out.add_field(label, read_float(ref, field.offset));
            break;
        case nw::SerializationType::resref:
            out.add_field(label, read_resref(ref, field));
            break;
        case nw::SerializationType::string:
            out.add_field(label, read_string(ref, field));
            break;
        case nw::SerializationType::locstring:
            out.add_field(label, read_locstring(ref, field));
            break;
        default:
            break;
        }
    };

    emit(sm.gff_label, sm.gff_type);

    // also_write: e.g. "StartingPackage" (uint8) alongside "xStartingPackage" (uint32).
    if (sm.also_write_gff_label) {
        emit(sm.also_write_gff_label, sm.also_write_gff_type);
    }
}

void PropsetGffExporter::export_spread(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef& field,
    const nw::smalls::Value& ref,
    nw::GffBuilderStruct& out) const
{
    const auto& sm = fp.spread;
    for (uint8_t i = 0; i < sm.count; ++i) {
        const char* label = sm.gff_labels[i];
        if (!label) { break; }
        uint32_t offset = field.offset + uint32_t(i) * 4u;
        int32_t v = read_int(ref, offset);
        switch (sm.element_gff_type) {
        case nw::SerializationType::uint8:
            out.add_field(label, static_cast<uint8_t>(v));
            break;
        case nw::SerializationType::int8:
            out.add_field(label, static_cast<int8_t>(v));
            break;
        case nw::SerializationType::uint16:
            out.add_field(label, static_cast<uint16_t>(v));
            break;
        case nw::SerializationType::int16:
            out.add_field(label, static_cast<int16_t>(v));
            break;
        default:
            break;
        }
    }
}

void PropsetGffExporter::export_list_scalar(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef& field,
    const nw::smalls::Value& ref,
    nw::GffBuilderStruct& out) const
{
    const auto& lm = fp.list_scalar;
    nw::smalls::IArray* arr = get_array(ref, field);
    if (!arr) { return; }

    auto& gff_list = out.add_list(lm.list_name);
    size_t sz = arr->size();
    for (size_t i = 0; i < sz; ++i) {
        nw::smalls::Value v;
        if (!arr->get_value(i, v, *rt_)) { continue; }
        auto& entry = gff_list.push_back(lm.gff_struct_id);
        switch (lm.element_gff_type) {
        case nw::SerializationType::uint8:
            entry.add_field(lm.element_field, static_cast<uint8_t>(v.data.ival));
            break;
        case nw::SerializationType::uint16:
            entry.add_field(lm.element_field, static_cast<uint16_t>(v.data.ival));
            break;
        case nw::SerializationType::int32:
            entry.add_field(lm.element_field, v.data.ival);
            break;
        case nw::SerializationType::resref: {
            auto* ptr = static_cast<nw::Resref*>(rt_->get_value_data_ptr(v));
            entry.add_field(lm.element_field, ptr ? *ptr : nw::Resref{});
            break;
        }
        default:
            break;
        }
    }
}

void PropsetGffExporter::export_list_paired(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef* field_a,
    const nw::smalls::FieldDef* field_b,
    const nw::smalls::Value& ref,
    nw::GffBuilderStruct& out) const
{
    const auto& lm = fp.list_paired;
    auto& gff_list = out.add_list(lm.list_name);

    // ClassList has 8 slots; skip trailing invalid entries (class value == -1).
    // We determine the count from field_a (classes fixed array).
    // Max is 8 slots (core.creature.CreatureLevels.classes).
    for (size_t i = 0; i < 8; ++i) {
        int32_t va = read_int(ref, field_a->offset + uint32_t(i) * 4u);
        if (va == -1) { continue; } // skip invalid (sparse) class slots
        int32_t vb = read_int(ref, field_b->offset + uint32_t(i) * 4u);
        gff_list.push_back(lm.gff_struct_id)
            .add_field(lm.field_a, static_cast<int32_t>(va))
            .add_field(lm.field_b, static_cast<int32_t>(vb));
    }
}

void PropsetGffExporter::export_list_struct(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef& field,
    const nw::smalls::Value& ref,
    nw::GffBuilderStruct& out) const
{
    const auto& lm = fp.list_struct;
    nw::smalls::IArray* arr = get_array(ref, field);
    if (!arr) { return; }

    const nw::smalls::StructDef* elem_def = rt_->get_struct_def(arr->element_type());
    if (!elem_def) { return; }

    auto& gff_list = out.add_list(lm.list_name);
    for (size_t i = 0; i < arr->size(); ++i) {
        nw::smalls::Value elem_value;
        if (!arr->get_value(i, elem_value, *rt_)) { continue; }

        auto& entry = gff_list.push_back(lm.gff_struct_id);
        for (const auto& fm : lm.fields) {
            uint32_t idx = elem_def->field_index(fm.propset_field_name);
            if (idx == UINT32_MAX) { continue; }
            const nw::smalls::FieldDef& elem_field = elem_def->fields[idx];
            switch (fm.gff_type) {
            case nw::SerializationType::uint8:
                entry.add_field(fm.gff_label, static_cast<uint8_t>(read_int(elem_value, elem_field.offset)));
                break;
            case nw::SerializationType::uint16:
                entry.add_field(fm.gff_label, static_cast<uint16_t>(read_int(elem_value, elem_field.offset)));
                break;
            case nw::SerializationType::int32:
                entry.add_field(fm.gff_label, read_int(elem_value, elem_field.offset));
                break;
            case nw::SerializationType::float_:
                entry.add_field(fm.gff_label, read_float(elem_value, elem_field.offset));
                break;
            case nw::SerializationType::resref:
                entry.add_field(fm.gff_label, read_resref(elem_value, elem_field));
                break;
            default:
                break;
            }
        }
    }
}

namespace {

template <typename T, typename ExportFn>
void export_object_propsets_to_gff(nw::smalls::Runtime* rt, const T* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile, ExportFn fn)
{
    if (!rt || !obj) { return; }

    PropsetGffExporter exporter{rt, &propset_gff_policy_registry()};
    (exporter.*fn)(obj, out, profile);
}

} // namespace

void export_creature_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Creature* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile)
{
    if (!rt || !obj) { return; }

    PropsetGffExporter exporter{rt, &propset_gff_policy_registry()};
    exporter.export_creature_levels(obj, out, profile);
    exporter.export_object_propset(obj, "core.creature.CreatureDescriptor", out, profile);
    exporter.export_object_propset(obj, "core.creature.CreatureAppearance", out, profile);
    exporter.export_object_propset(obj, "core.creature.CreatureStats", out, profile);
    exporter.export_object_propset(obj, "core.creature.CreatureHealth", out, profile);
}

void export_item_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Item* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile)
{
    export_object_propsets_to_gff(rt, obj, out, profile, &PropsetGffExporter::export_item);
}

void export_door_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Door* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile)
{
    export_object_propsets_to_gff(rt, obj, out, profile, &PropsetGffExporter::export_door);
}

void export_encounter_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Encounter* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile)
{
    export_object_propsets_to_gff(rt, obj, out, profile, &PropsetGffExporter::export_encounter);
}

void export_placeable_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Placeable* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile)
{
    export_object_propsets_to_gff(rt, obj, out, profile, &PropsetGffExporter::export_placeable);
}

void export_sound_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Sound* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile)
{
    export_object_propsets_to_gff(rt, obj, out, profile, &PropsetGffExporter::export_sound);
}

void export_store_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Store* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile)
{
    export_object_propsets_to_gff(rt, obj, out, profile, &PropsetGffExporter::export_store);
}

void export_trigger_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Trigger* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile)
{
    export_object_propsets_to_gff(rt, obj, out, profile, &PropsetGffExporter::export_trigger);
}

void export_waypoint_propsets_to_gff(nw::smalls::Runtime* rt, const nw::Waypoint* obj,
    nw::GffBuilderStruct& out, nw::SerializationProfile profile)
{
    export_object_propsets_to_gff(rt, obj, out, profile, &PropsetGffExporter::export_waypoint);
}

} // namespace nwn1
