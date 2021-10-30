#include "Location.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>

namespace nw {

Location::Location()
    : area{object_invalid}
    , position{}
    , orientation{}
{
}

Location::Location(const GffStruct gff, SerializationProfile profile)
    : Location()
{
    if (profile != SerializationProfile::any
        && profile != SerializationProfile::instance) {
        return;
    }

    bool valid = true;

    GffField field = gff["Area"];
    if (field.valid()) {
        if (auto y = field.get<uint32_t>()) {
            area = static_cast<ObjectID>(*y);
        }
    }

    valid = (gff.get_to("PositionX", position[0], false)
                && gff.get_to("PositionY", position[1], false)
                && gff.get_to("PositionZ", position[2], false))
        || (gff.get_to("XPosition", position[0], false)
            && gff.get_to("YPosition", position[1], false)
            && gff.get_to("ZPosition", position[2], false));

    if (gff.has_field("Bearing")) {
        float rad;
        if (gff.get_to("Bearing", rad)) {
            orientation = {std::cos(rad), std::sin(rad), 0.0f};
        }
    } else if (valid) {
        valid = (gff.get_to("OrientationX", orientation[0], false)
                    && gff.get_to("OrientationY", orientation[1], false))
            || (gff.get_to("XOrientation", orientation[0], false)
                && gff.get_to("YOrientation", orientation[1], false));

        // No clue why there is an Z, maybe for camera?
        // Not including it in validity check.
        gff.get_to("OrientationZ", orientation[2], false);
    }

    if (!valid) { area = object_invalid; }
}

} // namespace nw
