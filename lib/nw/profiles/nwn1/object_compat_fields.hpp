#pragma once

#include "../../objects/ObjectBase.hpp"
#include "../../serialization/Serialization.hpp"

#include <nlohmann/json_fwd.hpp>

namespace nwn1 {

bool obj_compat_fields_from_gff(nw::ObjectBase& obj, const nw::GffStruct& archive,
    nw::SerializationProfile profile, nw::ObjectType object_type);
bool obj_compat_fields_from_json(nw::ObjectBase& obj, const nlohmann::json& archive,
    nw::SerializationProfile profile, nw::ObjectType object_type);
bool obj_compat_fields_to_gff(const nw::ObjectBase& obj, nw::GffBuilderStruct& archive,
    nw::SerializationProfile profile, nw::ObjectType object_type);
nlohmann::json obj_compat_fields_to_json(const nw::ObjectBase& obj,
    nw::SerializationProfile profile, nw::ObjectType object_type);

} // namespace nwn1
