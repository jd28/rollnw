#include "Waypoint.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Waypoint::Waypoint()
    : common_{ObjectType::waypoint}
{
}

Waypoint::Waypoint(const GffInputArchiveStruct& archive, SerializationProfile profile)
    : common_{ObjectType::waypoint}
{
    valid_ = this->from_gff(archive, profile);
}

Waypoint::Waypoint(const nlohmann::json& archive, SerializationProfile profile)
{
    valid_ = this->from_json(archive, profile);
}

bool Waypoint::from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    common_.from_gff(archive, profile);

    archive.get_to("Appearance", appearance);
    archive.get_to("Description", description);
    archive.get_to("HasMapNote", has_map_note);
    archive.get_to("LinkedTo", linked_to);
    archive.get_to("MapNote", map_note);
    archive.get_to("MapNoteEnabled", map_note_enabled);

    return true;
}

bool Waypoint::from_json(const nlohmann::json& archive, SerializationProfile profile)
{
    if (archive.at("$type").get<std::string>() != "UTW") {
        LOG_F(ERROR, "waypoint: invalid json type");
        return false;
    }

    archive.at("appearance").get_to(appearance);
    common_.from_json(archive.at("common"), profile);
    archive.at("description").get_to(description);
    archive.at("has_map_note").get_to(has_map_note);
    archive.at("linked_to").get_to(linked_to);
    archive.at("map_note").get_to(map_note);
    archive.at("map_note_enabled").get_to(map_note_enabled);

    return true;
}

nlohmann::json Waypoint::to_json(SerializationProfile profile) const
{
    nlohmann::json j;

    j["$type"] = "UTW";
    j["$version"] = LIBNW_JSON_ARCHIVE_VERSION;

    j["appearance"] = appearance;
    j["common"] = common_.to_json(profile);
    j["description"] = description;
    j["has_map_note"] = has_map_note;
    j["linked_to"] = linked_to;
    j["map_note"] = map_note;
    j["map_note_enabled"] = map_note_enabled;

    return j;
}

} // namespace nw
