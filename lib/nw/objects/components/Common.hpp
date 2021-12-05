#pragma once

#include "../../resources/Resref.hpp"
#include "../../serialization/Serialization.hpp"
#include "../ObjectBase.hpp"
#include "LocalData.hpp"
#include "Location.hpp"

#include <string>

namespace nw {

struct Common {
    Common() = default;
    Common(ObjectType obj_type, const GffInputArchiveStruct gff, SerializationProfile profile);
    virtual ~Common() = default;

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
    uint8_t palette_id = ~0;

protected:
    bool valid_ = false;
};

} // namespace nw
