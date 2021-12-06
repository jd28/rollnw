#pragma once

#include "ObjectBase.hpp"
#include "components/Common.hpp"

#include <string>

namespace nw {

struct Waypoint : public ObjectBase {
    Waypoint(const GffInputArchiveStruct gff, SerializationProfile profile);
    virtual Common* common() { return &common_; }

    Common common_;
    LocString description;
    std::string linked_to;
    LocString map_note;

    uint8_t appearance;
    uint8_t has_map_note = 0;
    uint8_t map_note_enabled = 0;
};

} // namespace nw
