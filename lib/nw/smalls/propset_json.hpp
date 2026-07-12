#pragma once

#include "types.hpp"

#include <nlohmann/json_fwd.hpp>

namespace nw::smalls {

struct Runtime;
struct StructDef;
struct Value;

/// Generic smalls value/struct ↔ JSON serializer.
///
/// Uses smalls field names as JSON keys; no GFF or NWN1 dependency.
/// Handles:
///   - TK_primitive PK_int   → JSON int
///   - TK_primitive PK_float → JSON float
///   - TK_primitive PK_bool  → JSON bool
///   - TK_fixed_array int[N] → JSON array of N ints
///   - TK_array (unmanaged)  → JSON array of primitives/objects/POD structs
///   - ObjectHandle (immediate) → JSON number (serialised as uint64)
/// Unsupported heap/native fields → "null" placeholder with a log warning.
class JsonSerializer {
public:
    explicit JsonSerializer(Runtime* rt);

    /// Serialize a smalls value using the declared type as the JSON contract.
    nlohmann::json serialize_value(const Value& value, TypeID declared_type) const;

    /// Deserialize JSON into a smalls value using the declared type as the JSON contract.
    /// Allocates heap-backed values when needed.
    bool deserialize_value(const nlohmann::json& j, TypeID declared_type, Value& out) const;

    /// Serialize a struct-like field block to JSON.
    nlohmann::json serialize_struct(const Value& ref, const StructDef* def) const;

    /// Deserialize JSON into an existing struct-like field block.
    /// Returns false if any field could not be written.
    bool deserialize_struct(const nlohmann::json& j, const Value& ref,
        const StructDef* def) const;

    /// Serialize a propset value to JSON.
    nlohmann::json serialize(const Value& ref, const StructDef* def) const;

    /// Deserialize JSON into an existing propset (overwrites mapped fields).
    /// Returns false if any field could not be written.
    bool deserialize(const nlohmann::json& j, const Value& ref,
        const StructDef* def) const;

private:
    Runtime* rt_;
};

using PropsetJsonSerializer = JsonSerializer;

} // namespace nw::smalls
