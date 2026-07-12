#pragma once

#include "../../serialization/Serialization.hpp"

#include <string_view>
#include <vector>

// Pure policy data: GFF label / encoding descriptions for propset fields.
// Zero runtime dependencies — no TypeID, no Runtime pointer, no smalls types.

namespace nwn1 {

// -- GFF encoding variants ---------------------------------------------------

enum class GffEncoding : uint8_t {
    scalar,      // scalar field      → single GFF label
    spread,      // int[N] array      → N separate GFF labels (one per element)
    list_scalar, // array!(scalar)    → GFF List with one field per sub-struct
    list_paired, // two int[N] arrays → one GFF List, two fields per sub-struct
    list_struct, // array!(struct)    → GFF List with one row per struct element
    skip,        // runtime-only field — not present in GFF
};

// -- Encoding-specific mapping descriptors -----------------------------------

struct ScalarMapping {
    const char* gff_label = nullptr;
    nw::SerializationType::type gff_type = nw::SerializationType::invalid;

    // Optional: if reading gff_label fails, try this fallback.
    const char* fallback_gff_label = nullptr;
    nw::SerializationType::type fallback_gff_type = nw::SerializationType::invalid;

    // Optional: on export, also write this extra label.
    // Used for starting_package: emit "xStartingPackage" (uint32) AND "StartingPackage" (uint8).
    const char* also_write_gff_label = nullptr;
    nw::SerializationType::type also_write_gff_type = nw::SerializationType::invalid;

    // Optional: if the primary and fallback labels are absent, write this int value.
    bool default_int_on_missing = false;
    int32_t missing_int_value = 0;
};

struct SpreadMapping {
    // Parallel GFF labels for each array element; array is null-terminated.
    const char* gff_labels[8] = {};
    nw::SerializationType::type element_gff_type = nw::SerializationType::invalid;
    uint8_t count = 0; // number of elements / labels used
};

struct ListScalarMapping {
    const char* list_name = nullptr;     // GFF list label
    const char* element_field = nullptr; // field label inside each sub-struct
    nw::SerializationType::type element_gff_type = nw::SerializationType::invalid;
    uint32_t gff_struct_id = 0; // id passed to push_back() when emitting sub-structs
    bool pad_to_rules_skill_count = false;
    bool sort_int_values = false;
};

struct ListPairedMapping {
    const char* list_name = nullptr;       // GFF list label
    const char* field_a = nullptr;         // GFF label for the primary propset field
    const char* field_b = nullptr;         // GFF label for the secondary propset field
    const char* propset_field_b = nullptr; // secondary propset field name
    uint32_t gff_struct_id = 3;            // id passed to push_back() when emitting sub-structs
};

struct ListStructFieldMapping {
    const char* propset_field_name = nullptr; // field inside the element struct
    const char* gff_label = nullptr;          // field label inside each GFF list row
    nw::SerializationType::type gff_type = nw::SerializationType::invalid;
};

struct ListStructMapping {
    const char* list_name = nullptr; // GFF list label
    uint32_t gff_struct_id = 0;      // id passed to push_back() when emitting rows
    std::vector<ListStructFieldMapping> fields;
};

// -- Per-field policy --------------------------------------------------------

struct FieldGffPolicy {
    const char* propset_field_name = nullptr;
    GffEncoding encoding = GffEncoding::skip;

    // Only the member matching 'encoding' is meaningful.
    ScalarMapping scalar{};
    SpreadMapping spread{};
    ListScalarMapping list_scalar{};
    ListPairedMapping list_paired{};
    ListStructMapping list_struct{};

    // Skip this field when serializing a blueprint (instance-only data).
    bool instance_only = false;

    // Skip this field when serializing an instance (blueprint-only data).
    bool blueprint_only = false;

    // Read from GFF but do not write back. Used for legacy fields accepted by
    // importers but not emitted by the current writer contract.
    bool import_only = false;
};

// -- Per-propset policy ------------------------------------------------------

struct PropsetGffPolicy {
    const char* qualified_name = nullptr; // e.g. "core.creature.CreatureStats"
    std::vector<FieldGffPolicy> fields;
    // No TypeID here — pure data, resolved lazily in importer/exporter.

    // Skip this propset when serializing a blueprint (instance-only runtime state).
    bool instance_only = false;
};

// -- Registry ----------------------------------------------------------------

class PropsetGffPolicyRegistry {
public:
    void register_policy(PropsetGffPolicy policy);

    /// Find by qualified name (e.g. "core.creature.CreatureStats").
    const PropsetGffPolicy* find(std::string_view qualified_name) const;

    const std::vector<PropsetGffPolicy>& policies() const { return policies_; }

private:
    std::vector<PropsetGffPolicy> policies_;
};

/// Build the hardcoded NWN1 propset → GFF mapping table.
PropsetGffPolicyRegistry make_nwn1_propset_policy_registry();

/// Shared NWN1 propset → GFF mapping table for import/export conversion paths.
const PropsetGffPolicyRegistry& propset_gff_policy_registry();

} // namespace nwn1
