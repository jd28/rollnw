#include "propset_gff_importer.hpp"

#include "../../kernel/Rules.hpp"
#include "../../kernel/Strings.hpp"
#include "../../kernel/TwoDACache.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/Door.hpp"
#include "../../objects/Encounter.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectBase.hpp"
#include "../../objects/Placeable.hpp"
#include "../../objects/Sound.hpp"
#include "../../objects/Store.hpp"
#include "../../objects/Trigger.hpp"
#include "../../objects/Waypoint.hpp"
#include "../../resources/assets.hpp"
#include "../../rules/RuntimeObject.hpp"
#include "../../serialization/Gff.hpp"
#include "../../smalls/Array.hpp"
#include "../../smalls/runtime.hpp"
#include "../../util/HandlePool.hpp"
#include "propset_gff_object_io.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>

namespace nwn1 {

namespace {

template <typename T, typename ImportFn>
void import_object_propsets_from_gff(nw::smalls::Runtime* rt, T* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile, ImportFn import_fn)
{
    if (!rt || !obj || obj->handle().id == nw::object_invalid) { return; }

    rt->init_object_propsets(obj->handle());
    PropsetGffImporter importer{rt, &propset_gff_policy_registry()};
    (importer.*import_fn)(obj, gff, profile);
}

constexpr size_t item_model_color_count = 6;
constexpr size_t item_model_part_count = 19;

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

bool read_part_field(const nw::GffStruct& gff, const ItemPartGffField& field, int32_t& out)
{
    uint16_t extended = 0;
    if (field.extended && gff.get_to(field.extended, extended, false)) {
        out = extended;
        return true;
    }

    uint8_t legacy = 0;
    if (field.legacy && gff.get_to(field.legacy, legacy, false)) {
        out = legacy;
        return true;
    }

    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

PropsetGffImporter::PropsetGffImporter(nw::smalls::Runtime* rt,
    const PropsetGffPolicyRegistry* registry)
    : rt_(rt)
    , registry_(registry)
{
    type_id_cache_.assign(registry_->policies().size(), nw::smalls::invalid_type_id);
}

nw::smalls::TypeID PropsetGffImporter::resolve_type_id(size_t idx) const
{
    if (idx >= type_id_cache_.size()) { return nw::smalls::invalid_type_id; }
    if (type_id_cache_[idx] == nw::smalls::invalid_type_id) {
        type_id_cache_[idx] = rt_->type_id(registry_->policies()[idx].qualified_name, false);
    }
    return type_id_cache_[idx];
}

// ---------------------------------------------------------------------------
// Per-object entry points
// ---------------------------------------------------------------------------

void PropsetGffImporter::import_creature(nw::Creature* obj, const nw::GffStruct& gff,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* names[] = {
        "core.creature.CreatureDescriptor",
        "core.creature.CreatureAppearance",
        "core.creature.CreatureStats",
        "core.creature.CreatureHealth",
        "core.creature.CreatureLevels",
        "core.creature.CreatureCombat",
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

        import_propset(ref, def, *policy, gff, profile);
    }
}

void PropsetGffImporter::import_item(nw::Item* obj, const nw::GffStruct& gff,
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

        import_propset(ref, def, *policy, gff, profile);
    }
    import_item_visuals(obj, gff);
}

void PropsetGffImporter::import_door(nw::Door* obj, const nw::GffStruct& gff,
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

        import_propset(ref, def, *policy, gff, profile);
    }
}

void PropsetGffImporter::import_encounter(nw::Encounter* obj, const nw::GffStruct& gff,
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

        import_propset(ref, def, *policy, gff, profile);
    }
}

void PropsetGffImporter::import_placeable(nw::Placeable* obj, const nw::GffStruct& gff,
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

        import_propset(ref, def, *policy, gff, profile);
    }
}

void PropsetGffImporter::import_sound(nw::Sound* obj, const nw::GffStruct& gff,
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

        import_propset(ref, def, *policy, gff, profile);
    }
}

void PropsetGffImporter::import_store(nw::Store* obj, const nw::GffStruct& gff,
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

        import_propset(ref, def, *policy, gff, profile);
    }
}

void PropsetGffImporter::import_trigger(nw::Trigger* obj, const nw::GffStruct& gff,
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

        import_propset(ref, def, *policy, gff, profile);
    }
}

void PropsetGffImporter::import_waypoint(nw::Waypoint* obj, const nw::GffStruct& gff,
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

        import_propset(ref, def, *policy, gff, profile);
    }
}

// ---------------------------------------------------------------------------
// Core import logic
// ---------------------------------------------------------------------------

bool PropsetGffImporter::import_propset(const nw::smalls::Value& ref,
    const nw::smalls::StructDef* def,
    const PropsetGffPolicy& policy,
    const nw::GffStruct& gff,
    nw::SerializationProfile profile) const
{
    for (const auto& fp : policy.fields) {
        if (fp.encoding == GffEncoding::skip) { continue; }
        if (fp.instance_only && profile == nw::SerializationProfile::blueprint) { continue; }
        if (fp.blueprint_only && profile != nw::SerializationProfile::blueprint) { continue; }

        uint32_t idx_a = def->field_index(fp.propset_field_name);
        if (idx_a == UINT32_MAX) { continue; }
        const nw::smalls::FieldDef& field_a = def->fields[idx_a];

        switch (fp.encoding) {
        case GffEncoding::scalar:
            import_scalar(fp, field_a, ref, gff);
            break;
        case GffEncoding::spread:
            import_spread(fp, field_a, ref, gff);
            break;
        case GffEncoding::list_scalar:
            import_list_scalar(fp, field_a, ref, gff);
            break;
        case GffEncoding::list_paired: {
            // The secondary field name is in list_paired.propset_field_b
            uint32_t idx_b = def->field_index(fp.list_paired.propset_field_b);
            if (idx_b == UINT32_MAX) { break; }
            import_list_paired(fp, &field_a, &def->fields[idx_b], ref, gff);
            break;
        }
        case GffEncoding::list_struct:
            import_list_struct(fp, field_a, ref, gff);
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

void PropsetGffImporter::write_int(const nw::smalls::Value& ref, uint32_t offset, int32_t v) const
{
    rt_->write_value_field_at_offset(ref, offset, rt_->int_type(), nw::smalls::Value::make_int(v));
}

void PropsetGffImporter::write_float(const nw::smalls::Value& ref, uint32_t offset, float v) const
{
    rt_->write_value_field_at_offset(ref, offset, rt_->float_type(),
        nw::smalls::Value::make_float(v));
}

void PropsetGffImporter::write_resref(const nw::smalls::Value& ref,
    const nw::smalls::FieldDef& field,
    const nw::Resref& v) const
{
    rt_->write_value_field_at_offset(ref, field.offset, field.type_id,
        nw::smalls::detail::make_value(rt_, v));
}

void PropsetGffImporter::write_string(const nw::smalls::Value& ref,
    const nw::smalls::FieldDef& field,
    std::string_view v) const
{
    rt_->write_value_field_at_offset(ref, field.offset, field.type_id,
        nw::smalls::Value::make_string(rt_->alloc_string(nw::StringView{v.data(), v.size()})));
}

void PropsetGffImporter::write_locstring(const nw::smalls::Value& ref,
    const nw::smalls::FieldDef& field,
    const nw::LocString& v) const
{
    nw::TextRef text_ref = nw::kernel::strings().make_text_ref(v);
    rt_->write_value_field_at_offset(ref, field.offset, field.type_id,
        nw::smalls::detail::make_value(rt_, text_ref));
}

nw::smalls::IArray* PropsetGffImporter::get_array(const nw::smalls::Value& ref,
    const nw::smalls::FieldDef& field) const
{
    nw::smalls::Value arr_val = rt_->read_value_field_at_offset(ref, field.offset, field.type_id);
    nw::TypedHandle h = nw::TypedHandle::from_ull(arr_val.data.handle);
    return rt_->object_pool().get_unmanaged_array(h);
}

void PropsetGffImporter::import_scalar(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef& field,
    const nw::smalls::Value& ref,
    const nw::GffStruct& gff) const
{
    const auto& sm = fp.scalar;

    // Helper lambda: try to read gff_label with given type, return true on success.
    auto try_read = [&](const char* label, nw::SerializationType::type gff_type) -> bool {
        if (!label) { return false; }
        switch (gff_type) {
        case nw::SerializationType::uint8: {
            uint8_t v = 0;
            if (!gff.get_to(label, v, false)) { return false; }
            write_int(ref, field.offset, static_cast<int32_t>(v));
            return true;
        }
        case nw::SerializationType::int8: {
            int8_t v = 0;
            if (!gff.get_to(label, v, false)) { return false; }
            write_int(ref, field.offset, static_cast<int32_t>(v));
            return true;
        }
        case nw::SerializationType::uint16: {
            uint16_t v = 0;
            if (!gff.get_to(label, v, false)) { return false; }
            write_int(ref, field.offset, static_cast<int32_t>(v));
            return true;
        }
        case nw::SerializationType::int16: {
            int16_t v = 0;
            if (!gff.get_to(label, v, false)) { return false; }
            write_int(ref, field.offset, static_cast<int32_t>(v));
            return true;
        }
        case nw::SerializationType::uint32: {
            uint32_t v = 0;
            if (!gff.get_to(label, v, false)) { return false; }
            write_int(ref, field.offset, static_cast<int32_t>(v));
            return true;
        }
        case nw::SerializationType::int32: {
            int32_t v = 0;
            if (!gff.get_to(label, v, false)) { return false; }
            write_int(ref, field.offset, v);
            return true;
        }
        case nw::SerializationType::float_: {
            float v = 0.0f;
            if (!gff.get_to(label, v, false)) { return false; }
            write_float(ref, field.offset, v);
            return true;
        }
        case nw::SerializationType::resref: {
            nw::Resref v;
            if (!gff.get_to(label, v, false)) { return false; }
            write_resref(ref, field, v);
            return true;
        }
        case nw::SerializationType::string: {
            nw::String v;
            if (!gff.get_to(label, v, false)) { return false; }
            write_string(ref, field, nw::StringView{v});
            return true;
        }
        case nw::SerializationType::locstring: {
            nw::LocString v;
            if (!gff.get_to(label, v, false)) { return false; }
            write_locstring(ref, field, v);
            return true;
        }
        default:
            return false;
        }
    };

    bool read = try_read(sm.gff_label, sm.gff_type);
    if (!read && sm.fallback_gff_label) {
        read = try_read(sm.fallback_gff_label, sm.fallback_gff_type);
    }
    if (!read && sm.default_int_on_missing) {
        write_int(ref, field.offset, sm.missing_int_value);
    }
    // also_write_gff_label is export-only — ignored here.
}

void PropsetGffImporter::import_spread(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef& field,
    const nw::smalls::Value& ref,
    const nw::GffStruct& gff) const
{
    const auto& sm = fp.spread;
    for (uint8_t i = 0; i < sm.count; ++i) {
        const char* label = sm.gff_labels[i];
        if (!label) { break; }
        uint32_t offset = field.offset + uint32_t(i) * 4u;
        switch (sm.element_gff_type) {
        case nw::SerializationType::uint8: {
            uint8_t v = 0;
            if (gff.get_to(label, v, false)) { write_int(ref, offset, v); }
            break;
        }
        case nw::SerializationType::int8: {
            int8_t v = 0;
            if (gff.get_to(label, v, false)) { write_int(ref, offset, v); }
            break;
        }
        case nw::SerializationType::uint16: {
            uint16_t v = 0;
            if (gff.get_to(label, v, false)) { write_int(ref, offset, v); }
            break;
        }
        case nw::SerializationType::int16: {
            int16_t v = 0;
            if (gff.get_to(label, v, false)) { write_int(ref, offset, v); }
            break;
        }
        default:
            break;
        }
    }
}

void PropsetGffImporter::import_list_scalar(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef& field,
    const nw::smalls::Value& ref,
    const nw::GffStruct& gff) const
{
    const auto& lm = fp.list_scalar;
    auto list = gff[lm.list_name];
    if (!list.valid() && !lm.pad_to_rules_skill_count) { return; }

    nw::smalls::IArray* arr = get_array(ref, field);
    if (!arr) { return; }

    size_t sz = list.valid() ? list.size() : 0;
    size_t out_size = sz;
    if (lm.pad_to_rules_skill_count) {
        size_t rules_skill_count = nw::kernel::rules().skills.entries.size();
        if (out_size < rules_skill_count) { out_size = rules_skill_count; }
    }
    std::vector<nw::smalls::Value> values;
    values.reserve(out_size);
    for (size_t i = 0; i < sz; ++i) {
        switch (lm.element_gff_type) {
        case nw::SerializationType::uint8: {
            uint8_t v = 0;
            list[i].get_to(lm.element_field, v, false);
            values.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(v)));
            break;
        }
        case nw::SerializationType::uint16: {
            uint16_t v = 0;
            list[i].get_to(lm.element_field, v, false);
            values.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(v)));
            break;
        }
        case nw::SerializationType::int32: {
            int32_t v = 0;
            list[i].get_to(lm.element_field, v, false);
            values.push_back(nw::smalls::Value::make_int(v));
            break;
        }
        case nw::SerializationType::resref: {
            nw::Resref v;
            list[i].get_to(lm.element_field, v, false);
            values.push_back(nw::smalls::detail::make_value(rt_, v));
            break;
        }
        default:
            break;
        }
    }
    for (size_t i = sz; i < out_size; ++i) {
        values.push_back(nw::smalls::Value::make_int(0));
    }

    if (lm.sort_int_values) {
        std::sort(values.begin(), values.end(), [](nw::smalls::Value lhs, nw::smalls::Value rhs) {
            return lhs.data.ival < rhs.data.ival;
        });
    }

    arr->clear(); // overwrite, not accumulate
    arr->reserve(values.size());
    for (nw::smalls::Value value : values) {
        arr->append_value(value, *rt_);
    }
}

void PropsetGffImporter::import_list_paired(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef* field_a,
    const nw::smalls::FieldDef* field_b,
    const nw::smalls::Value& ref,
    const nw::GffStruct& gff) const
{
    const auto& lm = fp.list_paired;

    // Determine array capacity from the fixed-array type.
    int32_t max_slots = 8;
    const nw::smalls::Type* arr_type = rt_->get_type(field_a->type_id);
    if (arr_type && arr_type->type_kind == nw::smalls::TK_fixed_array) {
        max_slots = arr_type->type_params[1].as<int32_t>();
    }

    // Pre-fill all slots with sentinel values (-1 / 0) so absent list entries
    // have the same shape as default-initialized propset arrays.
    for (int32_t i = 0; i < max_slots; ++i) {
        write_int(ref, field_a->offset + uint32_t(i) * 4u, -1);
        write_int(ref, field_b->offset + uint32_t(i) * 4u, 0);
    }

    auto list = gff[lm.list_name];
    if (!list.valid()) { return; }

    size_t sz = std::min(size_t(max_slots), list.size());
    for (size_t i = 0; i < sz; ++i) {
        int32_t va = -1, vb = 0;
        list[i].get_to(lm.field_a, va, false);
        list[i].get_to(lm.field_b, vb, false);
        write_int(ref, field_a->offset + uint32_t(i) * 4u, va);
        write_int(ref, field_b->offset + uint32_t(i) * 4u, vb);
    }
}

void PropsetGffImporter::import_list_struct(const FieldGffPolicy& fp,
    const nw::smalls::FieldDef& field,
    const nw::smalls::Value& ref,
    const nw::GffStruct& gff) const
{
    const auto& lm = fp.list_struct;
    auto list = gff[lm.list_name];
    if (!list.valid()) { return; }

    nw::smalls::IArray* arr = get_array(ref, field);
    if (!arr) { return; }

    nw::smalls::TypeID elem_tid = arr->element_type();
    const nw::smalls::Type* elem_type = rt_->get_type(elem_tid);
    const nw::smalls::StructDef* elem_def = rt_->get_struct_def(elem_tid);
    if (!elem_type || !elem_def) { return; }

    arr->clear();
    arr->reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        nw::smalls::HeapPtr ptr = rt_->heap_.allocate(elem_type->size, elem_type->alignment, elem_tid);
        auto* data = static_cast<uint8_t*>(rt_->heap_.get_ptr(ptr));
        if (!data) { continue; }
        rt_->initialize_zero_defaults(elem_tid, data);
        nw::smalls::Value elem_value = nw::smalls::Value::make_heap(ptr, elem_tid);

        for (const auto& fm : lm.fields) {
            uint32_t idx = elem_def->field_index(fm.propset_field_name);
            if (idx == UINT32_MAX) { continue; }
            const nw::smalls::FieldDef& elem_field = elem_def->fields[idx];
            switch (fm.gff_type) {
            case nw::SerializationType::uint8: {
                uint8_t v = 0;
                if (list[i].get_to(fm.gff_label, v, false)) {
                    write_int(elem_value, elem_field.offset, static_cast<int32_t>(v));
                }
                break;
            }
            case nw::SerializationType::uint16: {
                uint16_t v = 0;
                if (list[i].get_to(fm.gff_label, v, false)) {
                    write_int(elem_value, elem_field.offset, static_cast<int32_t>(v));
                }
                break;
            }
            case nw::SerializationType::int32: {
                int32_t v = 0;
                if (list[i].get_to(fm.gff_label, v, false)) {
                    write_int(elem_value, elem_field.offset, v);
                }
                break;
            }
            case nw::SerializationType::float_: {
                float v = 0.0f;
                if (list[i].get_to(fm.gff_label, v, false)) {
                    write_float(elem_value, elem_field.offset, v);
                }
                break;
            }
            case nw::SerializationType::resref: {
                nw::Resref v;
                if (list[i].get_to(fm.gff_label, v, false)) {
                    write_resref(elem_value, elem_field, v);
                }
                break;
            }
            default:
                break;
            }
        }

        arr->append_value(elem_value, *rt_);
    }
}

void PropsetGffImporter::import_item_visuals(nw::Item* obj, const nw::GffStruct& gff) const
{
    if (!obj) { return; }

    nw::smalls::TypeID tid = rt_->type_id("core.item.ItemVisuals", false);
    if (tid == nw::smalls::invalid_type_id) {
        write_item_armor_stats(obj, -1);
        return;
    }

    nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) {
        write_item_armor_stats(obj, -1);
        return;
    }

    const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
    if (!def) {
        write_item_armor_stats(obj, -1);
        return;
    }

    std::array<int32_t, item_model_color_count> model_colors{};
    std::array<int32_t, item_model_part_count> model_parts{};
    std::array<int32_t, item_model_part_count * item_model_color_count> part_colors{};
    part_colors.fill(255);

    const bool is_armor = gff.has_field("ArmorPart_Belt");
    const bool is_composite = gff.has_field("ModelPart2");
    const bool has_model_colors = gff.has_field("Cloth1Color");

    if (is_armor) {
        for (const auto& field : armor_part_fields) {
            int32_t value = 0;
            if (read_part_field(gff, field, value) && field.part < model_parts.size()) {
                model_parts[field.part] = value;
            }
        }
    } else if (is_composite) {
        for (const auto& field : model_part_fields) {
            int32_t value = 0;
            if (read_part_field(gff, field, value) && field.part < model_parts.size()) {
                model_parts[field.part] = value;
            }
        }
    } else {
        int32_t value = 0;
        if (read_part_field(gff, model_part_fields[0], value)) {
            model_parts[nw::ItemModelParts::model1] = value;
        }
    }

    if (is_armor || is_composite || has_model_colors) {
        for (size_t i = 0; i < model_colors.size(); ++i) {
            uint8_t value = 0;
            if (gff.get_to(color_fields[i], value, false)) {
                model_colors[i] = value;
            }
        }
    }

    if (is_armor) {
        for (size_t part = 0; part < item_model_part_count; ++part) {
            for (size_t color = 0; color < item_model_color_count; ++color) {
                uint8_t value = 255;
                auto field_name = fmt::format("APart_{}_Col_{}", part, color);
                if (gff.get_to(field_name, value, false)) {
                    part_colors[part * item_model_color_count + color] = value;
                }
            }
        }
    }

    auto write_fixed_array = [&](const char* name, const auto& values) {
        uint32_t field = def->field_index(name);
        if (field == UINT32_MAX) { return; }
        uint32_t offset = def->fields[field].offset;
        for (size_t i = 0; i < values.size(); ++i) {
            write_int(ref, offset + uint32_t(i) * 4u, values[i]);
        }
    };

    write_fixed_array("model_colors", model_colors);
    write_fixed_array("model_parts", model_parts);
    write_fixed_array("part_colors", part_colors);

    int32_t armor_id = -1;
    if (is_armor) {
        auto* parts_chest = nw::kernel::twodas().get("parts_chest");
        float value = 0.0f;
        if (parts_chest
            && parts_chest->get_to(static_cast<size_t>(model_parts[nw::ItemModelParts::armor_torso]), "ACBonus", value)) {
            armor_id = static_cast<int32_t>(value);
        }
    }
    write_item_armor_stats(obj, armor_id);
}

void PropsetGffImporter::write_item_armor_stats(nw::Item* obj, int32_t armor_id) const
{
    if (!obj) { return; }

    nw::smalls::TypeID tid = rt_->type_id("core.item.ItemStats", false);
    if (tid == nw::smalls::invalid_type_id) { return; }

    nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return; }

    const nw::smalls::StructDef* def = rt_->get_struct_def(tid);
    if (!def) { return; }

    int32_t armor_dex_bonus = 0;
    int32_t armor_dex_bonus_valid = 0;
    int32_t armor_ac_bonus = 0;
    int32_t armor_ac_bonus_valid = 0;
    if (armor_id != -1) {
        auto* armor = nw::kernel::twodas().get("armor");
        if (armor) {
            if (auto result = armor->get<int32_t>(static_cast<size_t>(armor_id), "DEXBONUS")) {
                armor_dex_bonus = *result;
                armor_dex_bonus_valid = 1;
            }
            if (auto result = armor->get<int32_t>(static_cast<size_t>(armor_id), "ACBONUS")) {
                armor_ac_bonus = *result;
                armor_ac_bonus_valid = 1;
            }
        }
    }

    auto write_field = [&](const char* name, int32_t value) {
        uint32_t field = def->field_index(name);
        if (field == UINT32_MAX) { return; }
        write_int(ref, def->fields[field].offset, value);
    };

    write_field("armor_id", armor_id);
    write_field("armor_dex_bonus", armor_dex_bonus);
    write_field("armor_dex_bonus_valid", armor_dex_bonus_valid);
    write_field("armor_ac_bonus", armor_ac_bonus);
    write_field("armor_ac_bonus_valid", armor_ac_bonus_valid);
}

void import_creature_propsets_from_gff(nw::smalls::Runtime* rt, nw::Creature* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile)
{
    import_object_propsets_from_gff(rt, obj, gff, profile, &PropsetGffImporter::import_creature);
}

void import_item_propsets_from_gff(nw::smalls::Runtime* rt, nw::Item* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile)
{
    import_object_propsets_from_gff(rt, obj, gff, profile, &PropsetGffImporter::import_item);
}

void import_door_propsets_from_gff(nw::smalls::Runtime* rt, nw::Door* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile)
{
    import_object_propsets_from_gff(rt, obj, gff, profile, &PropsetGffImporter::import_door);
}

void import_encounter_propsets_from_gff(nw::smalls::Runtime* rt, nw::Encounter* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile)
{
    import_object_propsets_from_gff(rt, obj, gff, profile, &PropsetGffImporter::import_encounter);
}

void import_placeable_propsets_from_gff(nw::smalls::Runtime* rt, nw::Placeable* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile)
{
    import_object_propsets_from_gff(rt, obj, gff, profile, &PropsetGffImporter::import_placeable);
}

void import_sound_propsets_from_gff(nw::smalls::Runtime* rt, nw::Sound* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile)
{
    import_object_propsets_from_gff(rt, obj, gff, profile, &PropsetGffImporter::import_sound);
}

void import_store_propsets_from_gff(nw::smalls::Runtime* rt, nw::Store* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile)
{
    import_object_propsets_from_gff(rt, obj, gff, profile, &PropsetGffImporter::import_store);
}

void import_trigger_propsets_from_gff(nw::smalls::Runtime* rt, nw::Trigger* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile)
{
    import_object_propsets_from_gff(rt, obj, gff, profile, &PropsetGffImporter::import_trigger);
}

void import_waypoint_propsets_from_gff(nw::smalls::Runtime* rt, nw::Waypoint* obj,
    const nw::GffStruct& gff, nw::SerializationProfile profile)
{
    import_object_propsets_from_gff(rt, obj, gff, profile, &PropsetGffImporter::import_waypoint);
}

} // namespace nwn1
