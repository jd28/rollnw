#include "Objects.hpp"

#include "../log.hpp"
#include "../objects/Area.hpp"
#include "../objects/Creature.hpp"
#include "../objects/Door.hpp"
#include "../objects/Encounter.hpp"
#include "../objects/Item.hpp"
#include "../objects/Module.hpp"
#include "../objects/Trigger.hpp"
#include "../util/templates.hpp"
#include "Kernel.hpp"

#include <nlohmann/json.hpp>

namespace nw::kernel {

void ObjectSystem::clear() const
{
}

flecs::entity ObjectSystem::deserialize(ObjectType type, const GffInputArchiveStruct& archive,
    SerializationProfile profile) const
{
    flecs::entity ent = make(type);
    switch (type) {
    default:
        break;
    case ObjectType::creature:
        Creature::deserialize(ent, archive, profile);
        break;
    case ObjectType::door:
        Door::deserialize(ent, archive, profile);
        break;
    case ObjectType::encounter:
        Encounter::deserialize(ent, archive, profile);
        break;
    case ObjectType::gui:
        break;
    case ObjectType::item:
        Item::deserialize(ent, archive, profile);
        break;
    case ObjectType::placeable:
        Placeable::deserialize(ent, archive, profile);
        break;
    case ObjectType::portal:
        break;
    case ObjectType::projectile:
        break;
    case ObjectType::sound:
        Sound::deserialize(ent, archive, profile);
        break;
    case ObjectType::store:
        Store::deserialize(ent, archive, profile);
        break;
    case ObjectType::tile:
        break;
    case ObjectType::trigger:
        Trigger::deserialize(ent, archive, profile);
        break;
    case ObjectType::waypoint:
        Waypoint::deserialize(ent, archive, profile);
        break;
    }

    return ent;
}

flecs::entity ObjectSystem::deserialize(ObjectType type, const nlohmann::json& archive,
    SerializationProfile profile) const
{
    flecs::entity ent = make(type);
    switch (type) {
    default:
        break;
    case ObjectType::creature:
        Creature::deserialize(ent, archive, profile);
        break;
    case ObjectType::door:
        Door::deserialize(ent, archive, profile);
        break;
    case ObjectType::encounter:
        Encounter::deserialize(ent, archive, profile);
        break;
    case ObjectType::gui:
        break;
    case ObjectType::item:
        Item::deserialize(ent, archive, profile);
        break;
    case ObjectType::placeable:
        Placeable::deserialize(ent, archive, profile);
        break;
    case ObjectType::portal:
        break;
    case ObjectType::projectile:
        break;
    case ObjectType::sound:
        Sound::deserialize(ent, archive, profile);
        break;
    case ObjectType::store:
        Store::deserialize(ent, archive, profile);
        break;
    case ObjectType::tile:
        break;
    case ObjectType::trigger:
        Trigger::deserialize(ent, archive, profile);
        break;
    case ObjectType::waypoint:
        Waypoint::deserialize(ent, archive, profile);
        break;
    }

    return ent;
}

void ObjectSystem::destroy(flecs::entity ent) const
{
    ent.destruct();
}

ObjectType serial_id_to_obj_type(std::string_view id)
{
    if (string::icmp("UTC", id)) {
        return ObjectType::creature;
    } else if (string::icmp("UTD", id)) {
        return ObjectType::door;
    } else if (string::icmp("UTE", id)) {
        return ObjectType::encounter;
    } else if (string::icmp("UTI", id)) {
        return ObjectType::item;
    } else if (string::icmp("UTM", id)) {
        return ObjectType::store;
    } else if (string::icmp("UTP", id)) {
        return ObjectType::placeable;
    } else if (string::icmp("UTS", id)) {
        return ObjectType::sound;
    } else if (string::icmp("UTT", id)) {
        return ObjectType::trigger;
    } else if (string::icmp("UTW", id)) {
        return ObjectType::waypoint;
    }

    return ObjectType::invalid;
}

flecs::entity ObjectSystem::load(const std::filesystem::path& archive,
    SerializationProfile profile) const
{
    flecs::entity ent;
    ObjectType type;
    ResourceType::type restype = ResourceType::from_extension(archive.extension().u8string());

    if (restype == ResourceType::json) {
        try {
            std::ifstream f{archive, std::ifstream::binary};
            nlohmann::json j = nlohmann::json::parse(f);
            std::string serial_id = j.at("$type").get<std::string>();
            type = serial_id_to_obj_type(serial_id);
            ent = deserialize(type, j, profile);
        } catch (std::exception& e) {
            LOG_F(ERROR, "Failed to parse json file '{}' because {}", archive, e.what());
        }
    } else if (ResourceType::check_category(ResourceType::gff_archive, restype)) {
        GffInputArchive in{ByteArray::from_file(archive)};
        if (in.valid()) {
            type = serial_id_to_obj_type(in.type());
            ent = deserialize(type, in.toplevel(), profile);
        }
    } else {
        LOG_F(ERROR, "Unable to load unknown file type: '{}'", archive);
    }
    return ent;
}

flecs::entity ObjectSystem::load(std::string_view resref, ObjectType type) const
{
    flecs::entity ent;
    ByteArray ba;

    if (type == ObjectType::creature) {
        ba = nw::kernel::resman().demand({resref, ResourceType::utc});
        if (ba.size()) {
            GffInputArchive in{ba};
            if (in.valid()) {
                return deserialize(type, in.toplevel(), SerializationProfile::blueprint);
            }
        }
    }

    return ent;
}

flecs::entity ObjectSystem::make(ObjectType type) const
{
    flecs::entity ent = world().entity();
    switch (type) {
    default:
        break;
    case ObjectType::area:
        ent.emplace<Common>(ObjectType::area);
        ent.add<Area>();
        ent.add<AreaScripts>();
        ent.add<AreaWeather>();
        break;
    case ObjectType::areaofeffect:
        break;
    case ObjectType::creature:
        ent.add<Creature>();
        ent.emplace<Common>(ObjectType::creature);
        ent.add<Appearance>();
        ent.add<CombatInfo>();
        ent.add<Equips>();
        ent.emplace<Inventory>(ent);
        ent.add<LevelStats>();
        ent.add<CreatureScripts>();
        ent.add<CreatureStats>();
        break;
    case ObjectType::door:
        ent.add<Door>();
        ent.add<DoorScripts>();
        ent.emplace<Common>(ObjectType::door);
        ent.add<Lock>();
        ent.add<Trap>();
        break;
    case ObjectType::encounter:
        ent.add<Encounter>();
        ent.add<EncounterScripts>();
        ent.emplace<Common>(ObjectType::encounter);
        break;
    case ObjectType::gui:
        break;
    case ObjectType::item:
        ent.add<Item>();
        ent.emplace<Common>(ObjectType::item);
        ent.emplace<Inventory>(ent);
        break;
    case ObjectType::module:
        ent.add<Module>();
        ent.add<ModuleScripts>();
        ent.add<LocalData>();
        break;
    case ObjectType::placeable:
        ent.add<Placeable>();
        ent.add<PlaceableScripts>();
        ent.emplace<Common>(ObjectType::placeable);
        ent.emplace<Inventory>(ent);
        ent.add<Lock>();
        ent.add<Trap>();
        break;
    case ObjectType::portal:
        break;
    case ObjectType::projectile:
        break;
    case ObjectType::sound:
        ent.add<Sound>();
        ent.emplace<Common>(ObjectType::sound);
        break;
    case ObjectType::store:
        ent.add<Store>();
        ent.emplace<StoreInventory>(ent);
        ent.add<StoreScripts>();
        ent.emplace<Common>(ObjectType::store);
        break;
    case ObjectType::tile:
        break;
    case ObjectType::trigger:
        ent.add<Trigger>();
        ent.add<TriggerScripts>();
        ent.emplace<Common>(ObjectType::trigger);
        ent.add<Trap>();
        break;
    case ObjectType::waypoint:
        ent.emplace<Common>(ObjectType::waypoint);
        ent.add<Waypoint>();
        break;
    }

    return ent;
}

flecs::entity ObjectSystem::make_area(Resref area) const
{
    GffInputArchive are{resman().demand({area, ResourceType::are})};
    GffInputArchive git{resman().demand({area, ResourceType::git})};
    GffInputArchive gic;
    flecs::entity ent = make(ObjectType::area);
    Area::deserialize(ent, are.toplevel(), git.toplevel(), gic.toplevel());
    return ent;
}

flecs::entity ObjectSystem::make_module() const
{
    flecs::entity ent = make(ObjectType::module);
    auto ba = nw::kernel::resman().demand({"module"sv, ResourceType::ifo});

    if (!ba.size()) {
        LOG_F(ERROR, "Unable to load module.ifo from resman");
        ent.destruct();
        return ent;
    }

    if (ba.size() > 8 && memcmp(ba.data(), "IFO V3.2", 8) == 0) {
        GffInputArchive in{std::move(ba)};
        if (in.valid()) {
            Module::deserialize(ent, in.toplevel());
        }
    } else {
        auto sv = ba.string_view();
        try {
            nlohmann::json j = nlohmann::json::parse(sv.begin(), sv.end());
            Module::deserialize(ent, j);
        } catch (std::exception& e) {
            ent.destruct();
            return ent;
        }
    }

    return ent;
}

GffOutputArchive ObjectSystem::serialize(flecs::entity ent,
    SerializationProfile profile) const
{
    if (ent.has<Area>()) {
        // Area::serialize(ent, archive, profile);
    } else if (ent.has<Creature>()) {
        return Creature::serialize(ent, profile);
    } else if (ent.has<Door>()) {
        return Door::serialize(ent, profile);
    } else if (ent.has<Encounter>()) {
        return Encounter::serialize(ent, profile);
    } else if (ent.has<Item>()) {
        return Item::serialize(ent, profile);
    } else if (ent.has<Placeable>()) {
        return Placeable::serialize(ent, profile);
    } else if (ent.has<Sound>()) {
        return Sound::serialize(ent, profile);
    } else if (ent.has<Store>()) {
        return Store::serialize(ent, profile);
    } else if (ent.has<Trigger>()) {
        return Trigger::serialize(ent, profile);
    } else if (ent.has<Waypoint>()) {
        return Waypoint::serialize(ent, profile);
    }
    return GffOutputArchive{"XXX"};
}

void ObjectSystem::serialize(flecs::entity ent, GffOutputArchiveStruct& archive,
    SerializationProfile profile) const
{
    if (ent.has<Area>()) {
        // Area::serialize(ent, archive, profile);
    } else if (ent.has<Creature>()) {
        Creature::serialize(ent, archive, profile);
    } else if (ent.has<Door>()) {
        Door::serialize(ent, archive, profile);
    } else if (ent.has<Encounter>()) {
        Encounter::serialize(ent, archive, profile);
    } else if (ent.has<Item>()) {
        Item::serialize(ent, archive, profile);
    } else if (ent.has<Placeable>()) {
        Placeable::serialize(ent, archive, profile);
    } else if (ent.has<Sound>()) {
        Sound::serialize(ent, archive, profile);
    } else if (ent.has<Store>()) {
        Store::serialize(ent, archive, profile);
    } else if (ent.has<Trigger>()) {
        Trigger::serialize(ent, archive, profile);
    } else if (ent.has<Waypoint>()) {
        Waypoint::serialize(ent, archive, profile);
    }
}

void ObjectSystem::serialize(flecs::entity ent, nlohmann::json& archive,
    SerializationProfile profile) const
{
    if (ent.has<Area>()) {
        Area::serialize(ent, archive);
    } else if (ent.has<Creature>()) {
        Creature::serialize(ent, archive, profile);
    } else if (ent.has<Door>()) {
        Door::serialize(ent, archive, profile);
    } else if (ent.has<Encounter>()) {
        Encounter::serialize(ent, archive, profile);
    } else if (ent.has<Item>()) {
        Item::serialize(ent, archive, profile);
    } else if (ent.has<Placeable>()) {
        Placeable::serialize(ent, archive, profile);
    } else if (ent.has<Sound>()) {
        Sound::serialize(ent, archive, profile);
    } else if (ent.has<Store>()) {
        Store::serialize(ent, archive, profile);
    } else if (ent.has<Trigger>()) {
        Trigger::serialize(ent, archive, profile);
    } else if (ent.has<Waypoint>()) {
        Waypoint::serialize(ent, archive, profile);
    }
}

bool ObjectSystem::valid(flecs::entity ent) const
{
    return ent.is_alive();
}

} // namespace nw::kernel
