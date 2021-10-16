#pragma once

#include "../formats/Gff.hpp"
#include "../resources/Resref.hpp"
#include "ObjectID.hpp"
#include "Serialization.hpp"
#include "components/LocalData.hpp"
#include "components/Location.hpp"

#include <string>

namespace nw {

struct Object {
    Object() = default;
    Object(ObjectType obj_type, const GffStruct gff, SerializationProfile profile);
    virtual ~Object() = default;

    virtual bool valid() { return valid_; }

    ObjectID id = object_invalid;
    ObjectType object_type = ObjectType::invalid;
    Resref resref;
    std::string tag;
    LocString name;

    LocalData locals;
    Location location;

    uint32_t faction = 0;

    // Blueprint only
    std::string comment;
    uint8_t palette_id = ~0;

protected:
    bool valid_ = false;
};

} // namespace nw
