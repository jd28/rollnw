#pragma once

#include "../../serialization/Serialization.hpp"

namespace nw {
struct GffBuilderStruct;
struct GffStruct;
struct Store;
} // namespace nw

namespace nwn1 {

[[nodiscard]] bool import_store_inventory_from_gff(
    nw::Store& store,
    const nw::GffStruct& archive,
    nw::SerializationProfile profile);

[[nodiscard]] bool export_store_inventory_to_gff(
    const nw::Store& store,
    nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile);

} // namespace nwn1
