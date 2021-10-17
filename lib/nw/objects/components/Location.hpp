#pragma once

#include "../../formats/Gff.hpp"
#include "../ObjectBase.hpp"
#include "../Serialization.hpp"

#include <glm/glm.hpp>

namespace nw {

// This currently does not work for all locations, some things, viz., situated objects,
// use 'Bearing' in radians measured counterclockwise from north instead of a vec2.
// What is most advantageous to convert to is unclear.  As for now, it can read either
// and conver to a heading vector.

struct Location {
    Location();
    Location(const GffStruct gff, SerializationProfile profile);

    operator bool() { return area != object_invalid; }

    ObjectID area;
    glm::vec3 position;
    glm::vec3 orientation;
};

} // namespace nw
