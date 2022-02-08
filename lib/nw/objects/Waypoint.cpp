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

bool Waypoint::to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const
{
    archive.add_field("TemplateResRef", common_.resref)
        .add_field("LocalizedName", common_.name)
        .add_field("Tag", common_.tag);
    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common_.comment);
        archive.add_field("PaletteID", common_.palette_id);
    } else {
        archive.add_field("PositionX", common_.location.position.x)
            .add_field("PositionY", common_.location.position.y)
            .add_field("PositionZ", common_.location.position.z)
            .add_field("OrientationX", common_.location.orientation.x)
            .add_field("OrientationY", common_.location.orientation.y);
    }

    if (common_.locals.size()) {
        common_.locals.to_gff(archive);
    }

    archive.add_field("Appearance", appearance)
        .add_field("Description", description)
        .add_field("HasMapNote", has_map_note)
        .add_field("LinkedTo", linked_to)
        .add_field("MapNote", map_note)
        .add_field("MapNoteEnabled", map_note_enabled);

    return true;
}

GffOutputArchive Waypoint::to_gff(SerializationProfile profile) const
{
    GffOutputArchive out{"UTW"};
    this->to_gff(out.top, profile);
    out.build();
    return out;
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
    j["$version"] = json_archive_version;

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
