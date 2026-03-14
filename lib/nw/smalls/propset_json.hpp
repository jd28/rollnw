#pragma once

#include "types.hpp"

#include <nlohmann/json_fwd.hpp>

namespace nw::smalls {

struct Runtime;
struct StructDef;
struct Value;

/// Generic propset ↔ JSON serializer.
///
/// Uses smalls field names as JSON keys; no GFF or NWN1 dependency.
/// Handles:
///   - TK_primitive PK_int   → JSON int
///   - TK_primitive PK_float → JSON float
///   - TK_primitive PK_bool  → JSON bool
///   - TK_fixed_array int[N] → JSON array of N ints
///   - TK_array (unmanaged)  → JSON array of ints
///   - ObjectHandle (immediate) → JSON number (serialised as uint64)
/// Heap-ref types (TK_struct strings) → "null" placeholder with a log
/// warning (strings need GC root management — V2 scope).
class PropsetJsonSerializer {
public:
    explicit PropsetJsonSerializer(Runtime* rt);

    /// Serialize a propset value to JSON.
    nlohmann::json serialize(const Value& ref, const StructDef* def) const;

    /// Deserialize JSON into an existing propset (overwrites mapped fields).
    /// Returns false if any field could not be written.
    bool deserialize(const nlohmann::json& j, const Value& ref,
        const StructDef* def) const;

private:
    Runtime* rt_;
};

} // namespace nw::smalls
