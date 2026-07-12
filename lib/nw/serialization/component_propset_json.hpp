#pragma once

#include "../objects/ObjectHandle.hpp"
#include "Serialization.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace nw {

struct ObjectBase;

namespace smalls {
struct Runtime;
} // namespace smalls

struct ComponentPropsetJsonResult {
    bool ok = false;
    ObjectType object_type = ObjectType::invalid;
    SerializationProfile profile = SerializationProfile::any;
    std::string error;

    explicit operator bool() const noexcept { return ok; }
};

/// Serialize one loaded object through the component/propset JSON path.
///
/// This is a generic engine transform: object components are emitted as fixed
/// sections and Smalls propsets are emitted by qualified type name. It has no
/// NWN1 or GFF policy dependency.
[[nodiscard]] ComponentPropsetJsonResult object_to_component_propset_json(
    const ObjectBase* obj,
    nlohmann::json& out,
    smalls::Runtime* rt,
    SerializationProfile profile = SerializationProfile::instance);

[[nodiscard]] ComponentPropsetJsonResult object_from_component_propset_json(
    ObjectBase* obj,
    const nlohmann::json& in,
    smalls::Runtime* rt,
    SerializationProfile profile = SerializationProfile::instance);

} // namespace nw
