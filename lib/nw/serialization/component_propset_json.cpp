#include "component_propset_json.hpp"

#include "../kernel/Kernel.hpp"
#include "../kernel/Strings.hpp"
#include "../objects/Creature.hpp"
#include "../objects/Item.hpp"
#include "../objects/ObjectBase.hpp"
#include "../objects/ObjectComponentSystem.hpp"
#include "../objects/ObjectManager.hpp"
#include "../objects/Placeable.hpp"
#include "../objects/Store.hpp"
#include "../smalls/propset_json.hpp"
#include "../smalls/runtime.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <span>
#include <utility>
#include <vector>

namespace nw {

namespace {

ComponentPropsetJsonResult failed(SerializationProfile profile, std::string error,
    ObjectType type = ObjectType::invalid)
{
    return ComponentPropsetJsonResult{
        .ok = false,
        .object_type = type,
        .profile = profile,
        .error = std::move(error),
    };
}

ComponentPropsetJsonResult succeeded(SerializationProfile profile, ObjectType type)
{
    return ComponentPropsetJsonResult{
        .ok = true,
        .object_type = type,
        .profile = profile,
    };
}

std::string type_name_string(smalls::Runtime& rt, smalls::TypeID tid)
{
    StringView name = rt.type_name(tid);
    return std::string{name.data(), name.size()};
}

nlohmann::json object_base_to_json(const ObjectBase& obj)
{
    nlohmann::json out = nlohmann::json::object();
    out["resref"] = obj.resref;
    out["tag"] = obj.tag ? obj.tag.view() : "";
    out["name"] = obj.name;
    out["comment"] = obj.comment;
    out["palette_id"] = obj.palette_id;
    if (!obj.uuid.is_nil()) {
        out["uuid"] = uuids::to_string(obj.uuid);
    }
    return out;
}

bool object_base_from_json(ObjectBase& obj, const nlohmann::json& in, std::string& error)
{
    if (!in.is_object()) {
        error = "object section must be an object";
        return false;
    }

    try {
        if (auto it = in.find("resref"); it != in.end()) {
            it->get_to(obj.resref);
        }
        if (auto it = in.find("tag"); it != in.end()) {
            String tag;
            it->get_to(tag);
            obj.tag = tag.empty() ? InternedString{} : kernel::strings().intern(tag);
        }
        if (auto it = in.find("name"); it != in.end()) {
            it->get_to(obj.name);
        }
        if (auto it = in.find("comment"); it != in.end()) {
            it->get_to(obj.comment);
        }
        if (auto it = in.find("palette_id"); it != in.end()) {
            it->get_to(obj.palette_id);
        }
        if (auto it = in.find("uuid"); it != in.end()) {
            String uuid_string;
            it->get_to(uuid_string);
            if (auto uuid = uuids::uuid::from_string(uuid_string)) {
                obj.uuid = *uuid;
            } else {
                error = fmt::format("invalid object uuid '{}'", uuid_string);
                return false;
            }
        }
    } catch (const nlohmann::json::exception& e) {
        error = fmt::format("invalid object section: {}", e.what());
        return false;
    }

    return true;
}

nlohmann::json spawn_point_to_json(const ObjectSpawnPoint& self)
{
    return {
        {"position", {self.position.x, self.position.y, self.position.z}},
        {"orientation", self.orientation},
    };
}

bool vec3_from_json(const nlohmann::json& in, glm::vec3& out)
{
    if (!in.is_array() || in.size() != 3) { return false; }
    if (!in[0].is_number() || !in[1].is_number() || !in[2].is_number()) { return false; }

    out = glm::vec3{in[0].get<float>(), in[1].get<float>(), in[2].get<float>()};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

bool spawn_point_from_json(const nlohmann::json& in, ObjectSpawnPoint& out)
{
    if (!in.is_object()) { return false; }
    auto position = in.find("position");
    auto orientation = in.find("orientation");
    if (position == in.end() || orientation == in.end() || !orientation->is_number()) {
        return false;
    }

    if (!vec3_from_json(*position, out.position)) { return false; }
    out.orientation = orientation->get<float>();
    return std::isfinite(out.orientation);
}

Inventory* object_inventory(ObjectBase& obj)
{
    if (auto* creature = obj.as_creature()) { return &creature->inventory(); }
    if (auto* item = obj.as_item()) { return &item->inventory(); }
    if (auto* placeable = obj.as_placeable()) { return &placeable->inventory(); }
    return nullptr;
}

bool has_store_inventory_json(const nlohmann::json& in)
{
    return in.contains("armor")
        || in.contains("miscellaneous")
        || in.contains("potions")
        || in.contains("rings")
        || in.contains("weapons")
        || in.contains("will_not_buy")
        || in.contains("will_only_buy");
}

void serialize_component_json_sections(const ObjectBase* obj,
    nlohmann::json& out, SerializationProfile profile)
{
    if (!obj) { return; }

    out["object"] = object_base_to_json(*obj);

    auto& component_system = kernel::objects().components();
    auto& components = out["components"] = nlohmann::json::object();
    component_system.to_json_spatial(obj->handle(), components, profile);
    component_system.to_json_locals(obj->handle(), components, profile);

    nlohmann::json ability_loadout = component_system.ability_loadout_to_json(obj->handle());
    if (!ability_loadout.empty()) {
        components["ability_loadout"] = std::move(ability_loadout);
    }

    if (const auto* geometry = component_system.find_geometry(obj->handle())) {
        if (geometry->highlight_height != 0.0f) {
            components["geometry_highlight_height"] = geometry->highlight_height;
        }

        if (profile == SerializationProfile::instance
            || profile == SerializationProfile::savegame) {
            if (!geometry->points.empty()) {
                auto& points = components["geometry"] = nlohmann::json::array();
                for (const auto& point : geometry->points) {
                    points.push_back(nlohmann::json::array({point.x, point.y, point.z}));
                }
            }

            if (!geometry->spawn_points.empty()) {
                auto& spawn_points = components["spawn_points"] = nlohmann::json::array();
                for (const auto& point : geometry->spawn_points) {
                    spawn_points.push_back(spawn_point_to_json(point));
                }
            }
        }
    }

    if (const auto* inventory = component_system.find_inventory(*obj)) {
        components["inventory"] = inventory->to_json(profile);
    }

    if (const Creature* creature = obj->as_creature()) {
        components["equipment"] = creature->equipment.to_json(profile);
    }

    if (const auto* store = component_system.find_store_inventory(*obj)) {
        auto& store_inventory = components["store_inventory"] = nlohmann::json::object();
        store_inventory["armor"] = store->armor.to_json(profile);
        store_inventory["miscellaneous"] = store->miscellaneous.to_json(profile);
        store_inventory["potions"] = store->potions.to_json(profile);
        store_inventory["rings"] = store->rings.to_json(profile);
        store_inventory["weapons"] = store->weapons.to_json(profile);
        store_inventory["will_not_buy"] = store->will_not_buy;
        store_inventory["will_only_buy"] = store->will_only_buy;
    }
}

bool deserialize_geometry_json_section(ObjectBase& obj, const nlohmann::json& in, std::string& error)
{
    if (!in.contains("geometry")
        && !in.contains("spawn_points")
        && !in.contains("geometry_highlight_height")) {
        return true;
    }

    auto& components = kernel::objects().components();
    if (auto it = in.find("geometry_highlight_height"); it != in.end()) {
        if (!it->is_number()) {
            error = "geometry_highlight_height must be a finite number";
            return false;
        }

        const float value = it->get<float>();
        if (!std::isfinite(value) || !components.set_highlight_height(obj.handle(), value)) {
            error = "geometry_highlight_height must be a finite number";
            return false;
        }
    }

    if (auto it = in.find("geometry"); it != in.end()) {
        if (!it->is_array()) {
            error = "geometry section must be an array";
            return false;
        }

        Vector<glm::vec3> points;
        points.reserve(it->size());
        for (const auto& point_json : *it) {
            glm::vec3 point{0.0f};
            if (!vec3_from_json(point_json, point)) {
                error = "geometry point must be a finite [x, y, z] array";
                return false;
            }
            points.push_back(point);
        }
        if (!components.set_geometry(obj.handle(), std::span<const glm::vec3>{points.data(), points.size()})) {
            error = "failed to set object geometry";
            return false;
        }
    }

    if (auto it = in.find("spawn_points"); it != in.end()) {
        if (!it->is_array()) {
            error = "spawn_points section must be an array";
            return false;
        }

        Vector<ObjectSpawnPoint> spawn_points;
        spawn_points.reserve(it->size());
        for (const auto& point_json : *it) {
            ObjectSpawnPoint point;
            if (!spawn_point_from_json(point_json, point)) {
                error = "spawn point must contain finite position and orientation";
                return false;
            }
            spawn_points.push_back(point);
        }
        if (!components.set_spawn_points(obj.handle(),
                std::span<const ObjectSpawnPoint>{spawn_points.data(), spawn_points.size()})) {
            error = "failed to set object spawn points";
            return false;
        }
    }

    return true;
}

bool deserialize_inventory_json_section(ObjectBase& obj, const nlohmann::json& components_json,
    SerializationProfile profile, std::string& error)
{
    if (auto it = components_json.find("inventory"); it != components_json.end()) {
        Inventory* inventory = object_inventory(obj);
        if (!inventory) {
            error = fmt::format("object type '{}' does not support an inventory section", int(obj.handle().type));
            return false;
        }
        if (!inventory->from_json(*it, profile)) {
            error = "failed to deserialize inventory section";
            return false;
        }
    }

    auto store_inventory = components_json.find("store_inventory");
    if (store_inventory == components_json.end()) { return true; }
    if (!store_inventory->is_object() || !has_store_inventory_json(*store_inventory)) {
        error = "store_inventory component must contain store inventory sections";
        return false;
    }

    Store* store = obj.as_store();
    if (!store) {
        error = fmt::format("object type '{}' does not support store inventory sections", int(obj.handle().type));
        return false;
    }

    try {
        StoreInventory& inventory = store->inventory();
        if (!inventory.armor.from_json(store_inventory->at("armor"), profile)
            || !inventory.miscellaneous.from_json(store_inventory->at("miscellaneous"), profile)
            || !inventory.potions.from_json(store_inventory->at("potions"), profile)
            || !inventory.rings.from_json(store_inventory->at("rings"), profile)
            || !inventory.weapons.from_json(store_inventory->at("weapons"), profile)) {
            error = "failed to deserialize store inventory sections";
            return false;
        }
        store_inventory->at("will_not_buy").get_to(inventory.will_not_buy);
        store_inventory->at("will_only_buy").get_to(inventory.will_only_buy);
    } catch (const nlohmann::json::exception& e) {
        error = fmt::format("invalid store inventory section: {}", e.what());
        return false;
    }

    return true;
}

bool deserialize_creature_equipment_json_section(Creature& creature, const nlohmann::json& components_json,
    SerializationProfile profile, std::string& error)
{
    auto equipment = components_json.find("equipment");
    if (equipment == components_json.end()) {
        error = "missing creature equipment section";
        return false;
    }
    if (!creature.equipment.from_json(*equipment, profile)) {
        error = "failed to deserialize creature equipment";
        return false;
    }

    return true;
}

bool deserialize_component_json_sections(ObjectBase* obj, const nlohmann::json& in,
    SerializationProfile profile, std::string& error)
{
    if (!obj) {
        error = "missing object";
        return false;
    }
    if (!in.is_object()) {
        error = "component/propset JSON must be an object";
        return false;
    }

    if (auto object_it = in.find("object"); object_it != in.end()) {
        if (!object_base_from_json(*obj, *object_it, error)) { return false; }
    }

    auto components_it = in.find("components");
    if (components_it == in.end() || !components_it->is_object()) {
        error = "missing components section";
        return false;
    }

    auto& components = kernel::objects().components();
    if (!components.from_json_spatial(obj->handle(), *components_it, profile)) {
        error = "failed to deserialize spatial component";
        return false;
    }
    if (!components.from_json_locals(obj->handle(), *components_it)) {
        error = "failed to deserialize local data component";
        return false;
    }
    if (auto loadout = components_it->find("ability_loadout"); loadout != components_it->end()
        && !components.from_json_ability_loadout(obj->handle(), *loadout)) {
        error = "failed to deserialize ability loadout component";
        return false;
    }

    if (!deserialize_geometry_json_section(*obj, *components_it, error)
        || !deserialize_inventory_json_section(*obj, *components_it, profile, error)) {
        return false;
    }

    if (auto* creature = obj->as_creature()) {
        return deserialize_creature_equipment_json_section(*creature, *components_it, profile, error);
    }

    return true;
}

bool serialize_propset_json_sections(const ObjectBase* obj,
    nlohmann::json& out,
    smalls::Runtime* rt)
{
    if (!obj || !rt) { return false; }

    std::vector<smalls::TypeID> propset_types;
    rt->object_propset_types(obj->handle().type, propset_types);

    bool wrote_propset = false;
    smalls::PropsetJsonSerializer serializer{rt};
    for (smalls::TypeID tid : propset_types) {
        const smalls::StructDef* def = rt->get_struct_def(tid);
        if (!def || !def->is_propset || def->is_transient) { continue; }

        smalls::Value ref = rt->find_propset_ref(tid, obj->handle());
        if (ref.type_id == smalls::invalid_type_id) { continue; }

        out[type_name_string(*rt, tid)] = serializer.serialize(ref, def);
        wrote_propset = true;
    }

    return wrote_propset;
}

bool deserialize_propset_json_sections(ObjectBase* obj, const nlohmann::json& in,
    smalls::Runtime* rt, std::string& error)
{
    if (!obj) {
        error = "missing object";
        return false;
    }
    if (!rt) {
        error = "missing Smalls runtime";
        return false;
    }
    if (!in.is_object()) {
        error = "component/propset JSON must be an object";
        return false;
    }

    std::vector<smalls::TypeID> propset_types;
    rt->object_propset_types(obj->handle().type, propset_types);
    if (propset_types.empty()) {
        error = fmt::format("no propsets registered for object type '{}'", int(obj->handle().type));
        return false;
    }

    rt->init_object_propsets(obj->handle());

    bool read_propset = false;
    smalls::PropsetJsonSerializer serializer{rt};
    for (smalls::TypeID tid : propset_types) {
        const smalls::StructDef* def = rt->get_struct_def(tid);
        if (!def || !def->is_propset || def->is_transient) { continue; }

        const std::string section = type_name_string(*rt, tid);
        auto section_it = in.find(section);
        if (section_it == in.end()) {
            error = fmt::format("missing propset JSON section '{}'", section);
            return false;
        }
        if (!section_it->is_object()) {
            error = fmt::format("propset JSON section '{}' must be an object", section);
            return false;
        }

        smalls::Value ref = rt->get_or_create_propset_ref(tid, obj->handle());
        if (ref.type_id == smalls::invalid_type_id) {
            error = fmt::format("failed to create propset '{}'", section);
            return false;
        }
        if (!serializer.deserialize(*section_it, ref, def)) {
            error = fmt::format("failed to deserialize propset '{}'", section);
            return false;
        }
        read_propset = true;
    }

    if (!read_propset) {
        error = fmt::format("no propset JSON sections read for object type '{}'", int(obj->handle().type));
        return false;
    }
    return true;
}

} // namespace

ComponentPropsetJsonResult object_to_component_propset_json(
    const ObjectBase* obj,
    nlohmann::json& out,
    smalls::Runtime* rt,
    SerializationProfile profile)
{
    out = nlohmann::json{};

    if (!obj) {
        return failed(profile, "missing object");
    }
    if (!rt) {
        return failed(profile, "missing Smalls runtime", obj->handle().type);
    }

    const ObjectType type = obj->handle().type;
    if (type == ObjectType::invalid) {
        return failed(profile,
            fmt::format("unsupported object type '{}'", int(type)),
            type);
    }

    serialize_component_json_sections(obj, out, profile);
    if (!serialize_propset_json_sections(obj, out, rt)) {
        out = nlohmann::json{};
        return failed(profile,
            fmt::format("no propset JSON sections produced for object type '{}'", int(type)),
            type);
    }

    return succeeded(profile, type);
}

ComponentPropsetJsonResult object_from_component_propset_json(
    ObjectBase* obj,
    const nlohmann::json& in,
    smalls::Runtime* rt,
    SerializationProfile profile)
{
    if (!obj) {
        return failed(profile, "missing object");
    }
    if (!rt) {
        return failed(profile, "missing Smalls runtime", obj->handle().type);
    }

    const ObjectType type = obj->handle().type;
    if (type == ObjectType::invalid) {
        return failed(profile,
            fmt::format("unsupported object type '{}'", int(type)),
            type);
    }

    std::string error;
    if (!deserialize_component_json_sections(obj, in, profile, error)) {
        return failed(profile, error, type);
    }
    if (!deserialize_propset_json_sections(obj, in, rt, error)) {
        return failed(profile, error, type);
    }

    return succeeded(profile, type);
}

} // namespace nw
