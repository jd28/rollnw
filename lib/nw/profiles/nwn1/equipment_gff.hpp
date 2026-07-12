#pragma once

#include "../../serialization/Serialization.hpp"

namespace nw {
struct Equips;
struct GffBuilderStruct;
struct GffStruct;
} // namespace nw

namespace nwn1 {

[[nodiscard]] bool import_creature_equipment_from_gff(
    nw::Equips& equipment,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile);

[[nodiscard]] bool export_creature_equipment_to_gff(
    const nw::Equips& equipment,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile);

} // namespace nwn1
