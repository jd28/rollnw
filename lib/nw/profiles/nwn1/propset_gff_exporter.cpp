#include "propset_gff_exporter.hpp"

#include "../../objects/Creature.hpp"
#include "../../objects/Door.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectBase.hpp"
#include "../../objects/Placeable.hpp"
#include "../../rules/RuntimeObject.hpp"
#include "../../serialization/GffBuilder.hpp"
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

PropsetGffExporter::PropsetGffExporter(nw::smalls::Runtime* rt,
    const PropsetGffPolicyRegistry* registry)
    : rt_(rt)
    , registry_(registry)
{
}

// ---------------------------------------------------------------------------
// Per-object entry points
// ---------------------------------------------------------------------------

void PropsetGffExporter::export_creature(const nw::Creature* obj, nw::GffBuilderStruct& out,
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

        export_propset(ref, def, *policy, out, profile);
    }
}

void PropsetGffExporter::export_item(const nw::Item* obj, nw::GffBuilderStruct& out,
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

    export_propset(ref, def, *policy, out, profile);
}

void PropsetGffExporter::export_door(const nw::Door* obj, nw::GffBuilderStruct& out,
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

    export_propset(ref, def, *policy, out, profile);
}

void PropsetGffExporter::export_placeable(const nw::Placeable* obj, nw::GffBuilderStruct& out,
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

    export_propset(ref, def, *policy, out, profile);
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
        if (fp.instance_only && profile == nw::SerializationProfile::blueprint) { continue; }

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
        default: break;
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
        default: break;
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
        if (lm.element_is_uint16) {
            entry.add_field(lm.element_field, static_cast<uint16_t>(v.data.ival));
        } else {
            entry.add_field(lm.element_field, static_cast<uint8_t>(v.data.ival));
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
    // Max is 8 slots (LevelStats::max_classes).
    for (size_t i = 0; i < 8; ++i) {
        int32_t va = read_int(ref, field_a->offset + uint32_t(i) * 4u);
        if (va == -1) { continue; } // skip invalid (sparse) class slots
        int32_t vb = read_int(ref, field_b->offset + uint32_t(i) * 4u);
        gff_list.push_back(lm.gff_struct_id)
            .add_field(lm.field_a, static_cast<int32_t>(va))
            .add_field(lm.field_b, static_cast<int32_t>(vb));
    }
}

} // namespace nwn1
