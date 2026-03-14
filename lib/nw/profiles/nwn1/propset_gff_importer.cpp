#include "propset_gff_importer.hpp"

#include "../../objects/Creature.hpp"
#include "../../objects/Door.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectBase.hpp"
#include "../../objects/Placeable.hpp"
#include "../../rules/RuntimeObject.hpp"
#include "../../serialization/Gff.hpp"
#include "../../smalls/Array.hpp"
#include "../../smalls/runtime.hpp"
#include "../../util/HandlePool.hpp"

namespace nwn1 {

// Helper to get the StructDef from a TypeID.
static const nw::smalls::StructDef* struct_def_from_tid(nw::smalls::Runtime* rt,
    nw::smalls::TypeID tid)
{
    const nw::smalls::Type* type = rt->get_type(tid);
    if (!type || type->type_kind != nw::smalls::TK_struct) { return nullptr; }
    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    return rt->type_table_.get(struct_id);
}

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

        const nw::smalls::StructDef* def = struct_def_from_tid(rt_, tid);
        if (!def) { continue; }

        import_propset(ref, def, *policy, gff, profile);
    }
}

void PropsetGffImporter::import_item(nw::Item* obj, const nw::GffStruct& gff,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* qname = "core.item.ItemStats";
    const PropsetGffPolicy* policy = registry_->find(qname);
    if (!policy) { return; }

    nw::smalls::TypeID tid = rt_->type_id(qname, false);
    if (tid == nw::smalls::invalid_type_id) { return; }

    nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return; }

    const nw::smalls::StructDef* def = struct_def_from_tid(rt_, tid);
    if (!def) { return; }

    import_propset(ref, def, *policy, gff, profile);
}

void PropsetGffImporter::import_door(nw::Door* obj, const nw::GffStruct& gff,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* qname = "core.door.DoorState";
    const PropsetGffPolicy* policy = registry_->find(qname);
    if (!policy) { return; }

    nw::smalls::TypeID tid = rt_->type_id(qname, false);
    if (tid == nw::smalls::invalid_type_id) { return; }

    nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return; }

    const nw::smalls::StructDef* def = struct_def_from_tid(rt_, tid);
    if (!def) { return; }

    import_propset(ref, def, *policy, gff, profile);
}

void PropsetGffImporter::import_placeable(nw::Placeable* obj, const nw::GffStruct& gff,
    nw::SerializationProfile profile)
{
    if (!obj) { return; }
    const char* qname = "core.placeable.PlaceableState";
    const PropsetGffPolicy* policy = registry_->find(qname);
    if (!policy) { return; }

    nw::smalls::TypeID tid = rt_->type_id(qname, false);
    if (tid == nw::smalls::invalid_type_id) { return; }

    nw::smalls::Value ref = rt_->get_or_create_propset_ref(tid, obj->handle());
    if (ref.type_id == nw::smalls::invalid_type_id) { return; }

    const nw::smalls::StructDef* def = struct_def_from_tid(rt_, tid);
    if (!def) { return; }

    import_propset(ref, def, *policy, gff, profile);
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
        default:
            return false;
        }
    };

    // Try primary label; on failure, try fallback.
    if (!try_read(sm.gff_label, sm.gff_type)) {
        if (sm.fallback_gff_label) {
            try_read(sm.fallback_gff_label, sm.fallback_gff_type);
        }
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
        default: break;
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
    if (!list.valid()) { return; }

    nw::smalls::IArray* arr = get_array(ref, field);
    if (!arr) { return; }

    size_t sz = list.size();
    arr->clear(); // overwrite, not accumulate
    arr->reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        if (lm.element_is_uint16) {
            uint16_t v = 0;
            list[i].get_to(lm.element_field, v, false);
            arr->append_value(nw::smalls::Value::make_int(static_cast<int32_t>(v)), *rt_);
        } else {
            uint8_t v = 0;
            list[i].get_to(lm.element_field, v, false);
            arr->append_value(nw::smalls::Value::make_int(static_cast<int32_t>(v)), *rt_);
        }
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

    // Pre-fill all slots with sentinel values (-1 / 0) so unused slots
    // match the populate_*_propsets behaviour.
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

} // namespace nwn1
