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

void ObjectSystem::destroy(flecs::entity ent) const
{
    ent.destruct();
}

flecs::entity ObjectSystem::make(ObjectType type) const
{
    flecs::entity ent = world().entity();
    switch (type) {
    default:
        break;
    case ObjectType::area:
        ent.add<Area>();
        break;
    case ObjectType::areaofeffect:
        break;
    case ObjectType::creature:
        ent.add<Creature>();
        break;
    case ObjectType::door:
        ent.add<Door>();
        break;
    case ObjectType::encounter:
        ent.add<Encounter>();
        break;
    case ObjectType::gui:
        break;
    case ObjectType::item:
        ent.add<Item>();
        break;
    case ObjectType::module:
        ent.add<Module>();
        break;
    case ObjectType::placeable:
        ent.add<Placeable>();
        break;
    case ObjectType::portal:
        break;
    case ObjectType::projectile:
        break;
    case ObjectType::sound:
        ent.add<Sound>();
        break;
    case ObjectType::store:
        ent.add<Store>();
        break;
    case ObjectType::tile:
        break;
    case ObjectType::trigger:
        ent.add<Trigger>();
        break;
    case ObjectType::waypoint:
        ent.add<Waypoint>();
        break;
    }

    return ent;
}

flecs::entity ObjectSystem::make(std::string_view resref, ObjectType type) const
{
    flecs::entity ent = make(type);
    ByteArray ba;

    if (type == ObjectType::creature) {
        ba = nw::kernel::resman().demand({resref, ResourceType::utc});
        if (ba.size()) {
            GffInputArchive in{ba};
            if (in.valid()) {
                ent.get_mut<Creature>()->from_gff(in.toplevel(), SerializationProfile::blueprint);
            }
        }
    }

    return ent;
}

flecs::entity ObjectSystem::make(ObjectType type, const GffInputArchiveStruct& archive,
    SerializationProfile profile) const
{
    flecs::entity ent = make(type);
    switch (type) {
    default:
        break;
    case ObjectType::creature:
        ent.get_mut<Creature>()->from_gff(archive, profile);
        break;
    case ObjectType::door:
        ent.get_mut<Door>()->from_gff(archive, profile);
        break;
    case ObjectType::encounter:
        ent.get_mut<Encounter>()->from_gff(archive, profile);
        break;
    case ObjectType::gui:
        break;
    case ObjectType::item:
        ent.get_mut<Item>()->from_gff(archive, profile);
        break;
    case ObjectType::placeable:
        ent.get_mut<Placeable>()->from_gff(archive, profile);
        break;
    case ObjectType::portal:
        break;
    case ObjectType::projectile:
        break;
    case ObjectType::sound:
        ent.get_mut<Sound>()->from_gff(archive, profile);
        break;
    case ObjectType::store:
        ent.get_mut<Store>()->from_gff(archive, profile);
        break;
    case ObjectType::tile:
        break;
    case ObjectType::trigger:
        ent.get_mut<Trigger>()->from_gff(archive, profile);
        break;
    case ObjectType::waypoint:
        ent.get_mut<Waypoint>()->from_gff(archive, profile);
        break;
    }

    return ent;
}

flecs::entity ObjectSystem::make(ObjectType type, const nlohmann::json& archive,
    SerializationProfile profile) const
{
    flecs::entity ent = make(type);
    switch (type) {
    default:
        break;
    case ObjectType::creature:
        ent.get_mut<Creature>()->from_json(archive, profile);
        break;
    case ObjectType::door:
        ent.get_mut<Door>()->from_json(archive, profile);
        break;
    case ObjectType::encounter:
        ent.get_mut<Encounter>()->from_json(archive, profile);
        break;
    case ObjectType::gui:
        break;
    case ObjectType::item:
        ent.get_mut<Item>()->from_json(archive, profile);
        break;
    case ObjectType::placeable:
        ent.get_mut<Placeable>()->from_json(archive, profile);
        break;
    case ObjectType::portal:
        break;
    case ObjectType::projectile:
        break;
    case ObjectType::sound:
        ent.get_mut<Sound>()->from_json(archive, profile);
        break;
    case ObjectType::store:
        ent.get_mut<Store>()->from_json(archive, profile);
        break;
    case ObjectType::tile:
        break;
    case ObjectType::trigger:
        ent.get_mut<Trigger>()->from_json(archive, profile);
        break;
    case ObjectType::waypoint:
        ent.get_mut<Waypoint>()->from_json(archive, profile);
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
    ent.get_mut<Area>()->from_gff(are.toplevel(), git.toplevel(), gic.toplevel());
    return ent;
}

flecs::entity ObjectSystem::make_module() const
{
    flecs::entity ent = make(ObjectType::module);
    auto mod = ent.get_mut<Module>();

    auto ba = nw::kernel::resman().demand({"module"sv, ResourceType::ifo});

    if (!ba.size()) {
        LOG_F(ERROR, "Unable to load module.ifo from resman");
        ent.destruct();
        return ent;
    }

    if (ba.size() > 8 && memcmp(ba.data(), "IFO V3.2", 8) == 0) {
        GffInputArchive in{std::move(ba)};
        if (in.valid()) {
            mod->from_gff(in.toplevel());
        }
    } else {
        auto sv = ba.string_view();
        try {
            nlohmann::json j = nlohmann::json::parse(sv.begin(), sv.end());
            mod->from_json(j);
        } catch (std::exception& e) {
            ent.destruct();
            return ent;
        }
    }

    return ent;
}

bool ObjectSystem::valid(flecs::entity ent) const
{
    return ent.is_alive();
}

} // namespace nw::kernel
