#include "gff_propset_component_json.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/Door.hpp"
#include "../../objects/Encounter.hpp"
#include "../../objects/Inventory.hpp"
#include "../../objects/Item.hpp"
#include "../../objects/ObjectComponentSystem.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../objects/Placeable.hpp"
#include "../../objects/Sound.hpp"
#include "../../objects/Store.hpp"
#include "../../objects/Trigger.hpp"
#include "../../objects/Waypoint.hpp"
#include "../../serialization/Gff.hpp"
#include "../../serialization/component_propset_json.hpp"
#include "../../smalls/runtime.hpp"
#include "propset_gff_importer.hpp"
#include "propset_gff_policy.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <utility>

namespace nwn1 {

namespace {

GffToPropsetComponentJsonResult failed(nw::SerializationProfile profile, std::string error,
    nw::ObjectType type = nw::ObjectType::invalid)
{
    return GffToPropsetComponentJsonResult{
        .ok = false,
        .object_type = type,
        .profile = profile,
        .error = std::move(error),
    };
}

GffToPropsetComponentJsonResult succeeded(nw::SerializationProfile profile, nw::ObjectType type)
{
    return GffToPropsetComponentJsonResult{
        .ok = true,
        .object_type = type,
        .profile = profile,
    };
}

std::string serial_id_string(nw::StringView serial_id)
{
    return std::string{serial_id.data(), serial_id.size()};
}

nw::ObjectBase* make_transient_object(nw::ObjectType type)
{
    auto& objects = nw::kernel::objects();
    switch (type) {
    default:
        return nullptr;
    case nw::ObjectType::creature:
        return objects.make<nw::Creature>();
    case nw::ObjectType::door:
        return objects.make<nw::Door>();
    case nw::ObjectType::encounter:
        return objects.make<nw::Encounter>();
    case nw::ObjectType::item:
        return objects.make<nw::Item>();
    case nw::ObjectType::placeable:
        return objects.make<nw::Placeable>();
    case nw::ObjectType::sound:
        return objects.make<nw::Sound>();
    case nw::ObjectType::store:
        return objects.make<nw::Store>();
    case nw::ObjectType::trigger:
        return objects.make<nw::Trigger>();
    case nw::ObjectType::waypoint:
        return objects.make<nw::Waypoint>();
    }
}

struct TransientObject {
    nw::ObjectBase* obj = nullptr;
    nw::smalls::Runtime* rt = nullptr;

    TransientObject(nw::ObjectBase* obj_, nw::smalls::Runtime* rt_) noexcept
        : obj{obj_}
        , rt{rt_}
    {
    }

    ~TransientObject()
    {
        if (!obj) { return; }
        if (rt) {
            rt->free_object_propsets(obj->handle());
        }

        nw::smalls::Runtime* kernel_rt = &nw::kernel::runtime();
        if (kernel_rt != rt) {
            kernel_rt->free_object_propsets(obj->handle());
        }

        nw::kernel::objects().destroy(obj->handle());
    }

    TransientObject(const TransientObject&) = delete;
    TransientObject& operator=(const TransientObject&) = delete;
};

bool deserialize_gff_object(nw::ObjectBase* obj, const nw::GffStruct& gff,
    nw::SerializationProfile profile)
{
    if (!obj) { return false; }
    if (auto* cre = obj->as_creature()) { return nw::deserialize(cre, gff, profile); }
    if (auto* door = obj->as_door()) { return nw::deserialize(door, gff, profile); }
    if (auto* enc = obj->as_encounter()) { return nw::deserialize(enc, gff, profile); }
    if (auto* item = obj->as_item()) { return nw::deserialize(item, gff, profile); }
    if (auto* plc = obj->as_placeable()) { return nw::deserialize(plc, gff, profile); }
    if (auto* sound = obj->as_sound()) { return nw::deserialize(sound, gff, profile); }
    if (auto* store = obj->as_store()) { return nw::deserialize(store, gff, profile); }
    if (auto* trigger = obj->as_trigger()) { return nw::deserialize(trigger, gff, profile); }
    if (auto* waypoint = obj->as_waypoint()) { return nw::deserialize(waypoint, gff, profile); }
    return false;
}

bool import_gff_propsets(nw::ObjectBase* obj, const nw::GffStruct& gff,
    nw::smalls::Runtime* rt, const PropsetGffPolicyRegistry* registry,
    nw::SerializationProfile profile)
{
    if (!obj || !rt || !registry) { return false; }

    rt->init_object_propsets(obj->handle());
    PropsetGffImporter importer{rt, registry};
    if (auto* cre = obj->as_creature()) {
        importer.import_creature(cre, gff, profile);
    } else if (auto* door = obj->as_door()) {
        importer.import_door(door, gff, profile);
    } else if (auto* enc = obj->as_encounter()) {
        importer.import_encounter(enc, gff, profile);
    } else if (auto* item = obj->as_item()) {
        importer.import_item(item, gff, profile);
    } else if (auto* plc = obj->as_placeable()) {
        importer.import_placeable(plc, gff, profile);
    } else if (auto* sound = obj->as_sound()) {
        importer.import_sound(sound, gff, profile);
    } else if (auto* store = obj->as_store()) {
        importer.import_store(store, gff, profile);
    } else if (auto* trigger = obj->as_trigger()) {
        importer.import_trigger(trigger, gff, profile);
    } else if (auto* waypoint = obj->as_waypoint()) {
        importer.import_waypoint(waypoint, gff, profile);
    } else {
        return false;
    }

    return true;
}

} // namespace

GffToPropsetComponentJsonResult gff_to_propset_component_json(
    const nw::Gff& gff,
    nlohmann::json& out,
    nw::smalls::Runtime* rt,
    const PropsetGffPolicyRegistry* registry,
    nw::SerializationProfile profile)
{
    out = nlohmann::json{};

    if (!rt) {
        return failed(profile, "missing Smalls runtime");
    }
    if (!registry) {
        return failed(profile, "missing propset GFF policy registry");
    }
    if (!gff.valid()) {
        return failed(profile, fmt::format("invalid GFF: {}", gff.error()));
    }

    const auto serial_id = gff.type();
    const nw::ObjectType type = nw::serial_id_to_obj_type(serial_id);
    if (type == nw::ObjectType::invalid || type == nw::ObjectType::player) {
        return failed(profile,
            fmt::format("unsupported GFF object type '{}'", serial_id_string(serial_id)),
            type);
    }

    TransientObject transient{make_transient_object(type), rt};
    if (!transient.obj) {
        return failed(profile,
            fmt::format("failed to allocate transient object for GFF type '{}'",
                serial_id_string(serial_id)),
            type);
    }

    if (!deserialize_gff_object(transient.obj, gff.toplevel(), profile)) {
        return failed(profile,
            fmt::format("failed to deserialize GFF type '{}'", serial_id_string(serial_id)),
            type);
    }

    if (!import_gff_propsets(transient.obj, gff.toplevel(), rt, registry, profile)) {
        return failed(profile,
            fmt::format("failed to import propsets for GFF type '{}'",
                serial_id_string(serial_id)),
            type);
    }

    auto json_result = nw::object_to_component_propset_json(transient.obj, out, rt, profile);
    if (!json_result) {
        return failed(profile, json_result.error, type);
    }

    return succeeded(profile, type);
}

GffToPropsetComponentJsonResult gff_file_to_propset_component_json(
    const std::filesystem::path& path,
    nlohmann::json& out,
    nw::smalls::Runtime* rt,
    const PropsetGffPolicyRegistry* registry,
    nw::SerializationProfile profile)
{
    out = nlohmann::json{};
    if (!std::filesystem::exists(path)) {
        return failed(profile, fmt::format("GFF file does not exist: {}", path.string()));
    }

    nw::Gff gff{path};
    return gff_to_propset_component_json(gff, out, rt, registry, profile);
}

} // namespace nwn1
