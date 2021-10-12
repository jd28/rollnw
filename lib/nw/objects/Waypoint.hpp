#pragma once
#include "../formats/Gff.hpp"
#include "Object.hpp"

#include <string>

namespace nw {

struct Waypoint : public Object {
    Waypoint(const GffStruct gff, SerializationProfile profile);

    LocString description;
    std::string linked_to;
    LocString map_note;

    uint8_t appearance;
    uint8_t has_map_note = 0;
    uint8_t map_note_enabled = 0;
};

} // namespace nw
