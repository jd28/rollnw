#include "object_compat_fields.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../kernel/Strings.hpp"
#include "../../log.hpp"
#include "../../serialization/Gff.hpp"
#include "../../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

#include <limits>

namespace nwn1 {

using nw::GffStruct;
using nw::InternedString;
using nw::LocString;
using nw::ObjectBase;
using nw::ObjectType;
using nw::Resref;
using nw::SerializationProfile;
using nw::String;
using nw::StringView;

namespace {

StringView resref_field_name(ObjectType object_type)
{
    if (object_type == ObjectType::store) { return "ResRef"; }
    return "TemplateResRef";
}

bool object_has_localized_name(ObjectType object_type)
{
    return object_type != ObjectType::creature && object_type != ObjectType::area;
}

StringView name_field_name(ObjectType object_type)
{
    switch (object_type) {
    case ObjectType::door:
    case ObjectType::placeable:
    case ObjectType::sound:
    case ObjectType::store:
        return "LocName";
    default:
        return "LocalizedName";
    }
}

StringView palette_field_name(ObjectType object_type)
{
    if (object_type == ObjectType::store) { return "ID"; }
    return "PaletteID";
}

} // namespace

bool obj_compat_fields_from_gff(ObjectBase& obj, const GffStruct& archive,
    SerializationProfile profile, ObjectType object_type)
{
    Resref resref_value;
    if (!archive.get_to("TemplateResRef", resref_value, false)
        && !archive.get_to("ResRef", resref_value, false)) { // Store blueprints do their own thing
        LOG_F(ERROR, "invalid object no resref");
        return false;
    }
    obj.resref = resref_value;

    LocString name_value;
    if (object_type != ObjectType::creature
        && object_type != ObjectType::area
        && !archive.get_to("LocalizedName", name_value, false)
        && !archive.get_to("LocName", name_value, false)) {
        LOG_F(WARNING, "object no localized name");
    } else if (object_type != ObjectType::creature && object_type != ObjectType::area) {
        obj.name = name_value;
    }

    String temp;
    archive.get_to("Tag", temp);
    obj.tag = InternedString{};
    if (!temp.empty()) { obj.tag = nw::kernel::strings().intern(temp); }

    if (profile == SerializationProfile::blueprint) {
        archive.get_to("Comment", obj.comment);
        uint8_t palette_id_value = obj.palette_id;
        archive.get_to(palette_field_name(object_type), palette_id_value);
        obj.palette_id = palette_id_value;
    }

    return true;
}

bool obj_compat_fields_from_json(ObjectBase& obj, const nlohmann::json& archive,
    SerializationProfile profile, ObjectType object_type)
{
    String temp;
    archive.at("object_type").get_to(object_type);

    Resref resref_value;
    archive.at("resref").get_to(resref_value);
    obj.resref = resref_value;

    archive.at("tag").get_to(temp);
    obj.tag = InternedString{};
    if (!temp.empty()) { obj.tag = nw::kernel::strings().intern(temp); }

    if (object_type != ObjectType::creature) {
        LocString name_value;
        archive.at("name").get_to(name_value);
        obj.name = name_value;
    }

    if (profile == SerializationProfile::blueprint) {
        archive.at("comment").get_to(obj.comment);
        uint8_t palette_id_value = std::numeric_limits<uint8_t>::max();
        archive.at("palette_id").get_to(palette_id_value);
        obj.palette_id = palette_id_value;
    }

    return true;
}

bool obj_compat_fields_to_gff(const ObjectBase& obj, nw::GffBuilderStruct& archive,
    SerializationProfile profile, ObjectType object_type)
{
    archive.add_field(resref_field_name(object_type), obj.resref);
    if (object_has_localized_name(object_type)) {
        archive.add_field(name_field_name(object_type), obj.name);
    }
    archive.add_field("Tag", String(obj.tag ? obj.tag.view() : StringView()));

    if (profile == SerializationProfile::blueprint) {
        archive.add_field("Comment", obj.comment);
        archive.add_field(palette_field_name(object_type), obj.palette_id);
    }

    return true;
}

nlohmann::json obj_compat_fields_to_json(const ObjectBase& obj,
    SerializationProfile profile, ObjectType object_type)
{
    nlohmann::json j;

    j["object_type"] = object_type;
    j["resref"] = obj.resref;
    j["tag"] = obj.tag ? obj.tag.view() : "";

    if (object_type != ObjectType::creature) {
        j["name"] = obj.name;
    }

    if (profile == SerializationProfile::blueprint) {
        j["comment"] = obj.comment;
        j["palette_id"] = obj.palette_id;
    }

    return j;
}

} // namespace nwn1
