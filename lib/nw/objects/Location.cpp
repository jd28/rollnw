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

bool Location::from_gff(const GffStruct gff, SerializationProfile profile)
{
    if (profile != SerializationProfile::any
        && profile != SerializationProfile::instance) {
        return false;
    }

    bool valid = true;

    auto field = gff["Area"];
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

    if (!valid) {
        area = object_invalid;
    }

    return area != object_invalid;
}

void from_json(const nlohmann::json& json, Location& loc)
{
    loc.area = static_cast<ObjectID>(json.at("area").get<uint32_t>());

    json.at("position")[0].get_to(loc.position.x);
    json.at("position")[1].get_to(loc.position.y);
    json.at("position")[2].get_to(loc.position.z);

    json.at("orientation")[0].get_to(loc.orientation.x);
    json.at("orientation")[1].get_to(loc.orientation.y);
    json.at("orientation")[2].get_to(loc.orientation.z);
}

void to_json(nlohmann::json& json, const Location& loc)
{
    json = nlohmann::json{
        {"area", loc.area},
        {"position", {loc.position.x, loc.position.y, loc.position.z}},
        {"orientation", {loc.orientation.x, loc.orientation.y, loc.orientation.z}}};
}

} // namespace nw
