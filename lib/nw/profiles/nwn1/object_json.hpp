#pragma once

#include "../../serialization/Serialization.hpp"

#include <nlohmann/json_fwd.hpp>

namespace nw {
struct Creature;
} // namespace nw

namespace nw::smalls {
struct Runtime;
} // namespace nw::smalls

namespace nwn1 {

class PropsetGffPolicyRegistry;

/// Serialize full object state to JSON (C++ holdovers + propsets).
///
/// The output is a flat-section envelope: C++ component sections alongside
/// propset sections (keyed by unqualified type name, e.g. "CreatureStats").
/// This format is a runtime/debugging snapshot — not stable on-disk format.
/// GFF remains the authoritative persistence format.
nlohmann::json serialize_object_json(const nw::Creature* cre,
    nw::smalls::Runtime* rt, const PropsetGffPolicyRegistry* registry,
    nw::SerializationProfile profile);

/// Deserialize a JSON object produced by serialize_object_json.
///
/// Restores C++ holdovers via existing from_json path and propset fields via
/// PropsetJsonSerializer. Returns false if any section failed.
bool deserialize_object_json(const nlohmann::json& j, nw::Creature* cre,
    nw::smalls::Runtime* rt, const PropsetGffPolicyRegistry* registry,
    nw::SerializationProfile profile);

} // namespace nwn1
