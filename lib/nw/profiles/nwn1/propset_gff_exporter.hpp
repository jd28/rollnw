#pragma once

#include "propset_gff_policy.hpp"
#include "../../serialization/Serialization.hpp"

namespace nw {
struct Creature;
struct Door;
struct GffBuilderStruct;
struct Item;
struct Placeable;
} // namespace nw

namespace nw::smalls {
struct FieldDef;
struct IArray;
struct Runtime;
struct StructDef;
struct Value;
} // namespace nw::smalls

namespace nwn1 {

/// Propset → GFF exporter.
///
/// Reads field values from propsets and writes them into a GffBuilderStruct.
/// This is the flush-back path: propset → GFF on save.
///
/// Wire-up: call export_creature / export_item / etc. from the GFF serialize
/// path, replacing the corresponding manual archive.add_field() calls once
/// the parity gate (Phase 4) is green.
class PropsetGffExporter {
public:
    PropsetGffExporter(nw::smalls::Runtime* rt, const PropsetGffPolicyRegistry* registry);

    void export_creature(const nw::Creature* obj, nw::GffBuilderStruct& out,
        nw::SerializationProfile profile);
    void export_item(const nw::Item* obj, nw::GffBuilderStruct& out,
        nw::SerializationProfile profile);
    void export_door(const nw::Door* obj, nw::GffBuilderStruct& out,
        nw::SerializationProfile profile);
    void export_placeable(const nw::Placeable* obj, nw::GffBuilderStruct& out,
        nw::SerializationProfile profile);

private:
    nw::smalls::Runtime* rt_;
    const PropsetGffPolicyRegistry* registry_;

    /// Export a single propset to a GffBuilderStruct using the given policy.
    bool export_propset(const nw::smalls::Value& ref,
        const nw::smalls::StructDef* def,
        const PropsetGffPolicy& policy,
        nw::GffBuilderStruct& out,
        nw::SerializationProfile profile) const;

    // Encoding-specific helpers
    void export_scalar    (const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
                           const nw::smalls::Value& ref, nw::GffBuilderStruct& out) const;
    void export_spread    (const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
                           const nw::smalls::Value& ref, nw::GffBuilderStruct& out) const;
    void export_list_scalar(const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
                           const nw::smalls::Value& ref, nw::GffBuilderStruct& out) const;
    void export_list_paired(const FieldGffPolicy& fp,
                           const nw::smalls::FieldDef* field_a,
                           const nw::smalls::FieldDef* field_b,
                           const nw::smalls::Value& ref, nw::GffBuilderStruct& out) const;

    // Helper: read int32 from a propset field
    int32_t read_int  (const nw::smalls::Value& ref, uint32_t offset) const;
    float   read_float(const nw::smalls::Value& ref, uint32_t offset) const;

    // Helper: get IArray* from a propset unmanaged-array field
    nw::smalls::IArray* get_array(const nw::smalls::Value& ref,
        const nw::smalls::FieldDef& field) const;
};

} // namespace nwn1
