#include "object_document.hpp"

#include "resource_document.hpp"

#include <nw/kernel/Strings.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/objects/Trigger.hpp>
#include <nw/objects/Waypoint.hpp>
#include <nw/serialization/Serialization.hpp>
#include <nw/smalls/runtime.hpp>

#include <nlohmann/json.hpp>

#include <exception>

namespace nw::toolset {
namespace {

template <typename T>
void append_placed_area_object_rows(
    std::vector<PlacedAreaObjectRow>& rows, const Vector<T*>& members)
{
    for (const auto* member : members) {
        if (!member || !kernel::objects().valid(member->handle())) {
            continue;
        }
        rows.push_back({
            .object = member->handle(),
            .name = live_object_display_name(member->handle()),
        });
    }
}

template <typename T>
bool serialize_json_blueprint(const T* object, nlohmann::json& output)
{
    bool (*serializer)(const T*, nlohmann::json&, SerializationProfile) = serialize;
    return serializer && serializer(object, output, SerializationProfile::blueprint);
}

bool serialize_blueprint(ObjectBase* object, nlohmann::json& output)
{
    if (!object) {
        return false;
    }

    switch (object->handle().type) {
    case ObjectType::creature:
        return serialize_json_blueprint(object->as_creature(), output);
    case ObjectType::door:
        return serialize_json_blueprint(object->as_door(), output);
    case ObjectType::encounter:
        return serialize_json_blueprint(object->as_encounter(), output);
    case ObjectType::item:
        return serialize_json_blueprint(object->as_item(), output);
    case ObjectType::placeable:
        return serialize_json_blueprint(object->as_placeable(), output);
    case ObjectType::player: {
        bool (*serializer)(const Player*, nlohmann::json&) = serialize;
        return serializer && serializer(object->as_player(), output);
    }
    case ObjectType::sound:
        return serialize_json_blueprint(object->as_sound(), output);
    case ObjectType::store:
        return serialize_json_blueprint(object->as_store(), output);
    case ObjectType::trigger:
        return serialize_json_blueprint(object->as_trigger(), output);
    case ObjectType::waypoint:
        return serialize_json_blueprint(object->as_waypoint(), output);
    default:
        return false;
    }
}

std::string creature_name_field(smalls::Runtime& runtime,
    const smalls::Value& propset,
    const smalls::StructDef& definition,
    smalls::TypeID text_ref_type,
    std::string_view field_name)
{
    const uint32_t field_index = definition.field_index(field_name);
    if (field_index == UINT32_MAX) {
        return {};
    }
    const auto& field = definition.fields[field_index];
    if (field.type_id != text_ref_type) {
        return {};
    }
    const auto value = runtime.read_value_field_at_offset(
        propset, field.offset, field.type_id);
    const auto* text_ref = static_cast<const TextRef*>(runtime.get_value_data_ptr(value));
    return text_ref ? kernel::strings().get(*text_ref) : std::string{};
}

std::string live_creature_name(ObjectHandle object)
{
    auto& runtime = kernel::runtime();
    const auto descriptor_type = runtime.type_id(
        "nwn1.propsets.CreatureDescriptor", false);
    const auto* definition = runtime.get_struct_def(descriptor_type);
    if (!definition) {
        return {};
    }
    const auto propset = runtime.find_propset_ref(descriptor_type, object);
    if (propset.type_id == smalls::invalid_type_id) {
        return {};
    }
    const auto text_ref_type = runtime.type_id("core.types.TextRef", false);
    if (text_ref_type == smalls::invalid_type_id) {
        return {};
    }
    std::string result = creature_name_field(
        runtime, propset, *definition, text_ref_type, "name_first");
    const std::string last = creature_name_field(
        runtime, propset, *definition, text_ref_type, "name_last");
    if (!result.empty() && !last.empty()) {
        result.push_back(' ');
    }
    result += last;
    return result;
}

} // namespace

std::string_view placed_area_object_type_label(ObjectType type) noexcept
{
    switch (type) {
    case ObjectType::creature:
        return "Creature";
    case ObjectType::door:
        return "Door";
    case ObjectType::encounter:
        return "Encounter";
    case ObjectType::item:
        return "Item";
    case ObjectType::placeable:
        return "Placeable";
    case ObjectType::sound:
        return "Sound";
    case ObjectType::store:
        return "Store";
    case ObjectType::trigger:
        return "Trigger";
    case ObjectType::waypoint:
        return "Waypoint";
    default:
        return "Object";
    }
}

void build_placed_area_object_rows(
    const Area& area, std::vector<PlacedAreaObjectRow>& rows)
{
    rows.clear();
    rows.reserve(area.creatures.size()
        + area.doors.size()
        + area.encounters.size()
        + area.items.size()
        + area.placeables.size()
        + area.sounds.size()
        + area.stores.size()
        + area.triggers.size()
        + area.waypoints.size());
    append_placed_area_object_rows(rows, area.creatures);
    append_placed_area_object_rows(rows, area.doors);
    append_placed_area_object_rows(rows, area.encounters);
    append_placed_area_object_rows(rows, area.items);
    append_placed_area_object_rows(rows, area.placeables);
    append_placed_area_object_rows(rows, area.sounds);
    append_placed_area_object_rows(rows, area.stores);
    append_placed_area_object_rows(rows, area.triggers);
    append_placed_area_object_rows(rows, area.waypoints);
}

std::string live_object_display_name(ObjectHandle object)
{
    const auto* live_object = kernel::objects().get_object_base(object);
    if (!live_object) {
        return {};
    }

    if (object.type == ObjectType::creature) {
        if (auto name = live_creature_name(object); !name.empty()) {
            return name;
        }
    }
    if (auto name = kernel::strings().get(live_object->name); !name.empty()) {
        return name;
    }
    if (!live_object->tag.view().empty()) {
        return std::string{live_object->tag.view()};
    }
    if (!live_object->resref.empty()) {
        return std::string{live_object->resref.view()};
    }
    return std::string{placed_area_object_type_label(object.type)};
}

bool save_live_blueprint_json_atomic(
    ObjectHandle object, const std::filesystem::path& target, std::string& error)
{
    error.clear();
    auto* live_object = kernel::objects().get_object_base(object);
    if (!live_object) {
        error = "Live object is invalid or stale";
        return false;
    }

    nlohmann::json serialized;
    if (!serialize_blueprint(live_object, serialized)) {
        error = "Live object type does not support blueprint JSON serialization";
        return false;
    }
    return save_json_resource_document_atomic(target, serialized, error);
}

bool save_live_area_json_atomic(
    ObjectHandle object, const std::filesystem::path& target, std::string& error)
{
    error.clear();
    if (object.type != ObjectType::area) {
        error = "Live area is invalid or stale";
        return false;
    }
    auto* area = kernel::objects().get<Area>(object);
    if (!area) {
        error = "Live area is invalid or stale";
        return false;
    }

    nlohmann::json serialized;
    try {
        serialize(area, serialized);
    } catch (const std::exception& e) {
        error = "Failed to serialize live area: " + std::string{e.what()};
        return false;
    }
    return save_json_resource_document_atomic(target, serialized, error);
}

} // namespace nw::toolset
