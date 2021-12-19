#pragma once

#include "../../resources/Resref.hpp"
#include "../../serialization/Archives.hpp"
#include "../ObjectBase.hpp"
#include "LocalData.hpp"
#include "Location.hpp"

#include <string>

namespace nw {

struct Common {
    Common() = default;
    Common(ObjectType obj_type);
    Common(ObjectType obj_type, const GffInputArchiveStruct& archive, SerializationProfile profile);
    virtual ~Common() = default;

    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    nlohmann::json to_json(SerializationProfile profile) const;

    virtual bool valid() { return valid_; }

    ObjectID id = object_invalid;
    ObjectType object_type = ObjectType::invalid;
    Resref resref;
    std::string tag;
    LocString name;

    LocalData local_data;
    Location location;

    uint32_t faction = 0;

    // Blueprint only
    std::string comment;
    uint8_t palette_id = std::numeric_limits<uint8_t>::max();

protected:
    bool valid_ = false;
};

} // namespace nw
