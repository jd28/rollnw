#include "Waypoint.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Waypoint::deserialize(flecs::entity entity, const GffInputArchiveStruct& archive, SerializationProfile profile)
{
    auto common = entity.get_mut<Common>();
    common->from_gff(archive, profile);

    auto way = entity.get_mut<Waypoint>();
    archive.get_to("Description", way->description);
    archive.get_to("LinkedTo", way->linked_to);
    archive.get_to("MapNote", way->map_note);

    archive.get_to("Appearance", way->appearance);
    archive.get_to("HasMapNote", way->has_map_note);
    archive.get_to("MapNoteEnabled", way->map_note_enabled);

    return true;
}

bool Waypoint::deserialize(flecs::entity entity, const nlohmann::json& archive,
    SerializationProfile profile)
{
    if (archive.at("$type").get<std::string>() != "UTW") {
        LOG_F(ERROR, "waypoint: invalid json type");
        return false;
    }

    auto common = entity.get_mut<Common>();
    common->from_json(archive.at("common"), profile);

    auto way = entity.get_mut<Waypoint>();
    archive.at("appearance").get_to(way->appearance);
    archive.at("description").get_to(way->description);
    archive.at("has_map_note").get_to(way->has_map_note);
    archive.at("linked_to").get_to(way->linked_to);
    archive.at("map_note_enabled").get_to(way->map_note_enabled);
    archive.at("map_note").get_to(way->map_note);

    return true;
}

bool Waypoint::serialize(const flecs::entity entity, GffOutputArchiveStruct& archive,
    SerializationProfile profile)
{
    auto common = entity.get_mut<Common>();
    if (!common) { return false; }

    archive.add_field("TemplateResRef", common->resref)
        .add_field("LocalizedName", common->name)
        .add_field("Tag", common->tag);
    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", common->comment);
        archive.add_field("PaletteID", common->palette_id);
    } else {
        archive.add_field("PositionX", common->location.position.x)
            .add_field("PositionY", common->location.position.y)
            .add_field("PositionZ", common->location.position.z)
            .add_field("OrientationX", common->location.orientation.x)
            .add_field("OrientationY", common->location.orientation.y);
    }

    if (common->locals.size()) {
        common->locals.to_gff(archive);
    }

    auto way = entity.get<Waypoint>();
    if (!way) { return false; }

    archive.add_field("Description", way->description)
        .add_field("LinkedTo", way->linked_to)
        .add_field("MapNote", way->map_note);

    archive.add_field("Appearance", way->appearance)
        .add_field("HasMapNote", way->has_map_note)
        .add_field("MapNoteEnabled", way->map_note_enabled);

    return true;
}

GffOutputArchive Waypoint::serialize(const flecs::entity ent, SerializationProfile profile)
{
    GffOutputArchive out{"UTW"};
    Waypoint::serialize(ent, out.top, profile);
    out.build();
    return out;
}

void Waypoint::serialize(const flecs::entity entity, nlohmann::json& archive,
    SerializationProfile profile)
{
    archive["$type"] = "UTW";
    archive["$version"] = Waypoint::json_archive_version;

    auto common = entity.get<Common>();
    auto way = entity.get<Waypoint>();

    archive["common"] = common->to_json(profile);
    archive["description"] = way->description;
    archive["linked_to"] = way->linked_to;
    archive["map_note"] = way->map_note;

    archive["appearance"] = way->appearance;
    archive["has_map_note"] = way->has_map_note;
    archive["map_note_enabled"] = way->map_note_enabled;
}

} // namespace nw
