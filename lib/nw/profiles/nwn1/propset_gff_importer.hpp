#pragma once

#include "../../serialization/Serialization.hpp"
#include "../../smalls/types.hpp"
#include "propset_gff_policy.hpp"

#include <string_view>
#include <vector>

namespace nw {
struct Creature;
struct Door;
struct Encounter;
struct GffStruct;
struct Item;
struct Placeable;
struct Resref;
struct Sound;
struct Store;
struct Trigger;
struct Waypoint;
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
/// fields. This is the transition adapter for deserialize-time propset
/// initialization and the eventual standalone object conversion tooling.
///
/// Wire-up: call import_* from the GFF deserialize path after the C++ object is
/// loaded. Do not add runtime service state here.
class PropsetGffImporter {
public:
    PropsetGffImporter(nw::smalls::Runtime* rt, const PropsetGffPolicyRegistry* registry);

    void import_creature(nw::Creature* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_item(nw::Item* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_door(nw::Door* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_encounter(nw::Encounter* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_placeable(nw::Placeable* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_sound(nw::Sound* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_store(nw::Store* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_trigger(nw::Trigger* obj, const nw::GffStruct& gff,
        nw::SerializationProfile profile);
    void import_waypoint(nw::Waypoint* obj, const nw::GffStruct& gff,
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
    void import_scalar(const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
        const nw::smalls::Value& ref, const nw::GffStruct& gff) const;
    void import_spread(const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
        const nw::smalls::Value& ref, const nw::GffStruct& gff) const;
    void import_list_scalar(const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
        const nw::smalls::Value& ref, const nw::GffStruct& gff) const;
    void import_list_paired(const FieldGffPolicy& fp,
        const nw::smalls::FieldDef* field_a,
        const nw::smalls::FieldDef* field_b,
        const nw::smalls::Value& ref, const nw::GffStruct& gff) const;
    void import_list_struct(const FieldGffPolicy& fp, const nw::smalls::FieldDef& field,
        const nw::smalls::Value& ref, const nw::GffStruct& gff) const;
    void import_item_visuals(nw::Item* obj, const nw::GffStruct& gff) const;
    void write_item_armor_stats(nw::Item* obj, int32_t armor_id) const;

    // Helper: write an integer value into a propset field
    void write_int(const nw::smalls::Value& ref, uint32_t offset, int32_t v) const;
    void write_float(const nw::smalls::Value& ref, uint32_t offset, float v) const;
    void write_resref(const nw::smalls::Value& ref, const nw::smalls::FieldDef& field,
        const nw::Resref& v) const;
    void write_string(const nw::smalls::Value& ref, const nw::smalls::FieldDef& field,
        std::string_view v) const;
    void write_locstring(const nw::smalls::Value& ref, const nw::smalls::FieldDef& field,
        const nw::LocString& v) const;

    // Helper: get IArray* from a propset unmanaged-array field
    nw::smalls::IArray* get_array(const nw::smalls::Value& ref,
        const nw::smalls::FieldDef& field) const;
};

} // namespace nwn1
