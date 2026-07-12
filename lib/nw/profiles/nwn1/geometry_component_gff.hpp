#pragma once

#include "../../objects/ObjectHandle.hpp"
#include "../../serialization/Serialization.hpp"

namespace nw {
struct GffBuilderStruct;
struct GffStruct;
} // namespace nw

namespace nwn1 {

[[nodiscard]] bool import_trigger_geometry_from_gff(
    nw::ObjectHandle owner,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile);

[[nodiscard]] bool export_trigger_geometry_to_gff(
    nw::ObjectHandle owner,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile);

[[nodiscard]] bool import_encounter_geometry_from_gff(
    nw::ObjectHandle owner,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile);

[[nodiscard]] bool export_encounter_geometry_to_gff(
    nw::ObjectHandle owner,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile);

} // namespace nwn1
