#pragma once
#include "../formats/Gff.hpp"
#include "Object.hpp"

namespace nw {

struct Waypoint : public Object {
    Waypoint(const GffStruct gff, SerializationProfile profile);
};

} // namespace nw
