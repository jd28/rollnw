#pragma once

#include "propset_gff_policy.hpp"
#include "../../serialization/Serialization.hpp"
#include "../../smalls/types.hpp"

#include <string_view>
#include <vector>

namespace nw {
struct Creature;
struct Door;
struct GffStruct;
struct Item;
struct Placeable;
} // namespace nw

namespace nw::smalls {
struct IArray;
struct Runtime;
struct StructDef;
struct Value;
} // namespace nw::smalls

namespace nwn1 {

/// Reflection-based GFF → propset importer.
///
/// Uses PropsetGffPolicyRegistry to know how to map GFF fields into propset
/// fields, eliminating the hand-written populate_*_propsets() functions.
///
/// Wire-up: call import_creature / import_item / import_door / import_placeable
/// from the GFF deserialize path (after the C++ object is loaded), replacing
/// or supplementing populate_creature_propsets().
class PropsetGffImporter {
public:
    PropsetGffImporter(nw::smalls::Runtime* rt, const PropsetGffPolicyRegistry* registry);

    void import_creature(nw::Creature* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_item(nw::Item* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_door(nw::Door* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_placeable(nw::Placeable* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);

private:
    nw::smalls::Runtime* rt_;
    const PropsetGffPolicyRegistry* registry_;

    // Lazily-resolved TypeID cache keyed on qualified_name index in registry.
    // Populated on first use.
    mutable std::vector<nw::smalls::TypeID> type_id_cache_; // parallel to registry_->policies()
    nw::smalls::TypeID resolve_type_id(size_t policy_index) const;

    /// Import a single propset from a GFF struct using the given policy.
    bool import_propset(const nw::smalls::Value& ref,
        const nw::smalls::StructDef* def,
        const PropsetGffPolicy& policy,
        const nw::GffStruct& gff,
        nw::SerializationProfile profile) const;

    // Encoding-specific helpers
    void import_scalar    (const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
                           const nw::smalls::Value& ref, const nw::GffStruct& gff) const;
    void import_spread    (const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
                           const nw::smalls::Value& ref, const nw::GffStruct& gff) const;
    void import_list_scalar(const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
                           const nw::smalls::Value& ref, const nw::GffStruct& gff) const;
    void import_list_paired(const FieldGffPolicy& fp,
                           const nw::smalls::FieldDef* field_a,
                           const nw::smalls::FieldDef* field_b,
                           const nw::smalls::Value& ref, const nw::GffStruct& gff) const;

    // Helper: write an integer value into a propset field
    void write_int(const nw::smalls::Value& ref, uint32_t offset, int32_t v) const;
    void write_float(const nw::smalls::Value& ref, uint32_t offset, float v) const;

    // Helper: get IArray* from a propset unmanaged-array field
    nw::smalls::IArray* get_array(const nw::smalls::Value& ref,
        const nw::smalls::FieldDef& field) const;
};

} // namespace nwn1
