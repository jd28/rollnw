#include "object_name_preview.hpp"

#include "../../kernel/Strings.hpp"
#include "../../resources/assets.hpp"
#include "../../serialization/Gff.hpp"

#include <nlohmann/json.hpp>

#include <cstring>

namespace nwn1 {

namespace {

bool has_gff_signature(const nw::ResourceData& rdata, nw::StringView serial_id)
{
    return rdata.bytes.size() > 8
        && serial_id.size() == 3
        && std::memcmp(rdata.bytes.data(), serial_id.data(), 3) == 0
        && std::memcmp(rdata.bytes.data() + 3, " V3.2", 5) == 0;
}

nw::StringView name_field_name(nw::ObjectType object_type)
{
    switch (object_type) {
    case nw::ObjectType::door:
    case nw::ObjectType::placeable:
    case nw::ObjectType::sound:
    case nw::ObjectType::store:
        return "LocName";
    default:
        return "LocalizedName";
    }
}

nw::String localized_text(const nw::LocString& value)
{
    return nw::kernel::strings().get(value);
}

bool read_json_object_name(const nlohmann::json& j, nw::LocString& out)
{
    auto object = j.find("object");
    if (object == j.end() || !object->is_object()) { return false; }

    auto name = object->find("name");
    if (name == object->end()) { return false; }

    name->get_to(out);
    return true;
}

bool read_json_creature_name(const nlohmann::json& j, nw::LocString& first,
    nw::LocString& last)
{
    auto descriptor = j.find("core.creature.CreatureDescriptor");
    if (descriptor == j.end() || !descriptor->is_object()) { return false; }

    descriptor->at("name_first").get_to(first);
    descriptor->at("name_last").get_to(last);
    return true;
}

bool read_json_area_name(const nlohmann::json& j, nw::LocString& out)
{
    auto name = j.find("name");
    if (name == j.end()) { return false; }

    name->get_to(out);
    return true;
}

} // namespace

nw::String preview_object_name_from_file(const std::filesystem::path& path,
    nw::StringView gff_serial_id, nw::ObjectType object_type)
{
    nw::LocString name;
    auto rdata = nw::ResourceData::from_file(path);
    if (rdata.bytes.size() <= 8) { return {}; }

    if (has_gff_signature(rdata, gff_serial_id)) {
        nw::Gff gff(std::move(rdata));
        if (!gff.valid()) { return {}; }
        gff.toplevel().get_to(name_field_name(object_type), name);
    } else {
        try {
            nlohmann::json j = nlohmann::json::parse(rdata.bytes.string_view());
            if (!read_json_object_name(j, name)) { return {}; }
        } catch (...) {
            return {};
        }
    }

    return localized_text(name);
}

nw::String preview_creature_name_from_file(const std::filesystem::path& path)
{
    nw::LocString first;
    nw::LocString last;
    auto rdata = nw::ResourceData::from_file(path);
    if (rdata.bytes.size() <= 8) { return {}; }

    if (has_gff_signature(rdata, "UTC")) {
        nw::Gff gff(std::move(rdata));
        if (!gff.valid()) { return {}; }
        gff.toplevel().get_to("FirstName", first);
        gff.toplevel().get_to("LastName", last);
    } else {
        try {
            nlohmann::json j = nlohmann::json::parse(rdata.bytes.string_view());
            if (!read_json_creature_name(j, first, last)) { return {}; }
        } catch (...) {
            return {};
        }
    }

    auto result = localized_text(first);
    if (!result.empty()) {
        auto last_name = localized_text(last);
        if (!last_name.empty()) {
            result += " " + last_name;
        }
    }
    return result;
}

nw::String preview_area_name_from_file(const std::filesystem::path& path)
{
    nw::LocString name;
    auto rdata = nw::ResourceData::from_file(path);
    if (rdata.bytes.size() <= 8) { return {}; }

    if (has_gff_signature(rdata, "ARE")) {
        nw::Gff gff(std::move(rdata));
        if (!gff.valid()) { return {}; }
        gff.toplevel().get_to("Name", name);
    } else {
        try {
            nlohmann::json j = nlohmann::json::parse(rdata.bytes.string_view());
            if (!read_json_area_name(j, name)) { return {}; }
        } catch (...) {
            return {};
        }
    }

    return localized_text(name);
}

} // namespace nwn1
