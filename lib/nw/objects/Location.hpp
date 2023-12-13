#pragma once

#include "ObjectBase.hpp"

#include <glm/glm.hpp>

namespace nw {

// This currently does not work for all locations, some things, viz., situated objects,
// use 'Bearing' in radians measured counterclockwise from north instead of a vec2.
// What is most advantageous to convert to is unclear.  As for now, it can read either
// and convert to a heading vector.

struct Location {
    Location();

    operator bool() { return area != object_invalid; }
    bool operator==(const Location&) const = default;

    ObjectID area;
    glm::vec3 position;
    glm::vec3 orientation;
};

void from_json(const nlohmann::json& json, Location& loc);
void to_json(nlohmann::json& json, const Location& loc);

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Location& self, const GffStruct gff, SerializationProfile profile);
#endif

} // namespace nw
