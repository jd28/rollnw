#pragma once

#include "../resources/Resref.hpp"
#include "../serialization/Archives.hpp"
#include "LocalData.hpp"
#include "Location.hpp"
#include "ObjectBase.hpp"

#include <string>

namespace nw {

struct Common {
    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile, ObjectType object_type);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile, ObjectType object_type);
    nlohmann::json to_json(SerializationProfile profile, ObjectType object_type) const;

    bool valid() { return valid_; }

    Resref resref;
    std::string tag;
    LocString name;

    LocalData locals;
    Location location;

    // Blueprint only
    std::string comment;
    uint8_t palette_id = std::numeric_limits<uint8_t>::max();

protected:
    bool valid_ = false;
};

} // namespace nw
