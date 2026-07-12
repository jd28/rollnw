#pragma once

#include "../../objects/ObjectHandle.hpp"
#include "../../serialization/Serialization.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>

namespace nw {
struct Gff;
} // namespace nw

namespace nw::smalls {
struct Runtime;
} // namespace nw::smalls

namespace nwn1 {

class PropsetGffPolicyRegistry;

struct GffToPropsetComponentJsonResult {
    bool ok = false;
    nw::ObjectType object_type = nw::ObjectType::invalid;
    nw::SerializationProfile profile = nw::SerializationProfile::any;
    std::string error;

    explicit operator bool() const noexcept { return ok; }
};

/// Convert one NWN1 object GFF into the propset/component JSON envelope.
///
/// This is the legacy GFF feeder. The durable conversion output is JSON carrying
/// object components and propset sections keyed by Smalls type, not legacy C++
/// object holdover fields.
///
/// The caller owns runtime setup: kernel services must be running and the
/// required Smalls modules for the object type must already be loaded. This
/// function creates a transient object, deserializes the GFF, imports propsets,
/// serializes component/propset JSON, then frees the transient object and propsets.
[[nodiscard]] GffToPropsetComponentJsonResult gff_to_propset_component_json(
    const nw::Gff& gff,
    nlohmann::json& out,
    nw::smalls::Runtime* rt,
    const PropsetGffPolicyRegistry* registry,
    nw::SerializationProfile profile = nw::SerializationProfile::blueprint);

[[nodiscard]] GffToPropsetComponentJsonResult gff_file_to_propset_component_json(
    const std::filesystem::path& path,
    nlohmann::json& out,
    nw::smalls::Runtime* rt,
    const PropsetGffPolicyRegistry* registry,
    nw::SerializationProfile profile = nw::SerializationProfile::blueprint);

} // namespace nwn1
