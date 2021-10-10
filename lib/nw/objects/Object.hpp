#pragma once

#include "../formats/Gff.hpp"
#include "../resources/Resref.hpp"
#include "ObjectID.hpp"
#include "Serialization.hpp"

#include <string>

namespace nw {

struct Object {
    Object() = default;
    Object(const GffStruct gff, SerializationProfile profile);
    virtual ~Object() = default;

    bool valid() { return valid_; }

    ObjectID id = object_invalid;
    Resref resref;
    std::string tag;
    LocString name;

    uint32_t faction;
    int16_t hp;
    int16_t hp_current;

    // Blueprint only
    std::string comment;
    uint8_t palette_id = ~0;

protected:
    bool valid_ = false;
};

} // namespace nw
