#include "Common.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool Common::from_json(const nlohmann::json& archive, SerializationProfile profile, ObjectType object_type)
{

    archive.at("object_type").get_to(object_type);
    archive.at("resref").get_to(resref);
    archive.at("tag").get_to(tag);

    if (object_type != ObjectType::creature) {
        archive.at("name").get_to(name);
    }

    locals.from_json(archive.at("locals"));

    if (profile == SerializationProfile::instance || profile == SerializationProfile::savegame) {
        archive.at("location").get_to(location);
    }

    if (profile == SerializationProfile::blueprint) {
        archive.at("comment").get_to(comment);
        archive.at("palette_id").get_to(palette_id);
    }

    return true;
}

nlohmann::json Common::to_json(SerializationProfile profile, ObjectType object_type) const
{
    nlohmann::json j;

    j["object_type"] = object_type;
    j["resref"] = resref;
    j["tag"] = tag;

    if (object_type != ObjectType::creature) {
        j["name"] = name;
    }

    j["locals"] = locals.to_json(profile);

    if (profile == SerializationProfile::instance || profile == SerializationProfile::savegame) {
        j["location"] = location;
    }

    if (profile == SerializationProfile::blueprint) {
        j["comment"] = comment;
        j["palette_id"] = palette_id;
    }

    return j;
}

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Common& self, const GffStruct& archive, SerializationProfile profile, ObjectType object_type)
{
    deserialize(self.location, archive, profile);
    deserialize(self.locals, archive);

    if (!archive.get_to("TemplateResRef", self.resref, false)
        && !archive.get_to("ResRef", self.resref, false)) { // Store blueprints do their own thing
        LOG_F(ERROR, "invalid object no resref");
        return false;
    }

    if (object_type != ObjectType::creature
        && object_type != ObjectType::area
        && !archive.get_to("LocalizedName", self.name, false)
        && !archive.get_to("LocName", self.name, false)) {
        LOG_F(WARNING, "object no localized name");
    }

    archive.get_to("Tag", self.tag);

    if (profile == SerializationProfile::blueprint) {
        archive.get_to("Comment", self.comment);
        archive.get_to("PaletteID", self.palette_id);
    }

    return true;
}
#endif

} // namespace nw
