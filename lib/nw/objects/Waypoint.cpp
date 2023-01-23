#include "Waypoint.hpp"

#include <nlohmann/json.hpp>

namespace nw {

Waypoint::Waypoint()
{
    set_handle({object_invalid, ObjectType::waypoint, 0});
}

bool Waypoint::deserialize(Waypoint* obj, const nlohmann::json& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    if (archive.at("$type").get<std::string>() != "UTW") {
        LOG_F(ERROR, "waypoint: invalid json type");
        return false;
    }

    obj->common.from_json(archive.at("common"), profile, ObjectType::waypoint);

    archive.at("appearance").get_to(obj->appearance);
    archive.at("description").get_to(obj->description);
    archive.at("has_map_note").get_to(obj->has_map_note);
    archive.at("linked_to").get_to(obj->linked_to);
    archive.at("map_note_enabled").get_to(obj->map_note_enabled);
    archive.at("map_note").get_to(obj->map_note);

    return true;
}

void Waypoint::serialize(const Waypoint* obj, nlohmann::json& archive,
    SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive["$type"] = "UTW";
    archive["$version"] = Waypoint::json_archive_version;

    archive["common"] = obj->common.to_json(profile, ObjectType::waypoint);
    archive["description"] = obj->description;
    archive["linked_to"] = obj->linked_to;
    archive["map_note"] = obj->map_note;

    archive["appearance"] = obj->appearance;
    archive["has_map_note"] = obj->has_map_note;
    archive["map_note_enabled"] = obj->map_note_enabled;
}

// == Waypoint - Serialization - Gff ==========================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(Waypoint* obj, const GffStruct& archive, SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    deserialize(obj->common, archive, profile, ObjectType::waypoint);

    archive.get_to("Description", obj->description);
    archive.get_to("LinkedTo", obj->linked_to);
    archive.get_to("MapNote", obj->map_note);

    archive.get_to("Appearance", obj->appearance);
    archive.get_to("HasMapNote", obj->has_map_note);
    archive.get_to("MapNoteEnabled", obj->map_note_enabled);

    return true;
}

bool serialize(const Waypoint* obj, GffBuilderStruct& archive,
    SerializationProfile profile)
{
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    archive.add_field("TemplateResRef", obj->common.resref)
        .add_field("LocalizedName", obj->common.name)
        .add_field("Tag", obj->common.tag);
    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", obj->common.comment);
        archive.add_field("PaletteID", obj->common.palette_id);
    } else {
        archive.add_field("PositionX", obj->common.location.position.x)
            .add_field("PositionY", obj->common.location.position.y)
            .add_field("PositionZ", obj->common.location.position.z)
            .add_field("OrientationX", obj->common.location.orientation.x)
            .add_field("OrientationY", obj->common.location.orientation.y);
    }

    if (obj->common.locals.size()) {
        serialize(obj->common.locals, archive, profile);
    }

    archive.add_field("Description", obj->description)
        .add_field("LinkedTo", obj->linked_to)
        .add_field("MapNote", obj->map_note);

    archive.add_field("Appearance", obj->appearance)
        .add_field("HasMapNote", obj->has_map_note)
        .add_field("MapNoteEnabled", obj->map_note_enabled);

    return true;
}

GffBuilder serialize(const Waypoint* obj, SerializationProfile profile)
{
    GffBuilder out{"UTW"};
    if (!obj) {
        throw std::runtime_error("unable to serialize null object");
    }

    serialize(obj, out.top, profile);
    out.build();
    return out;
}

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
