#pragma once

#include "Common.hpp"
#include "Inventory.hpp"
#include "Item.hpp"
#include "Lock.hpp"
#include "ObjectBase.hpp"
#include "Saves.hpp"
#include "Trap.hpp"

#include <flecs/flecs.h>

namespace nw {

enum struct PlaceableAnimationState : uint8_t {
    none = 0, // Technically "default"
    open = 1,
    closed = 2,
    destroyed = 3,
    activated = 4,
    deactivated = 5
};

struct PlaceableScripts {

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    Resref on_click;
    Resref on_closed;
    Resref on_damaged;
    Resref on_death;
    Resref on_disarm;
    Resref on_heartbeat;
    Resref on_inventory_disturbed;
    Resref on_lock;
    Resref on_melee_attacked;
    Resref on_open;
    Resref on_spell_cast_at;
    Resref on_trap_triggered;
    Resref on_unlock;
    Resref on_used;
    Resref on_user_defined;
};

struct Placeable {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::placeable;

    // Serialization
    static bool deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile);
    static bool deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile);
    static GffOutputArchive serialize(const flecs::entity ent, SerializationProfile profile);
    static bool serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile);
    static bool serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile);

    Resref conversation;
    LocString description;
    Saves saves;

    uint32_t appearance;
    uint32_t faction = 0;

    int16_t hp = 0;
    int16_t hp_current = 0;
    uint16_t portrait_id;

    PlaceableAnimationState animation_state;
    uint8_t bodybag = 0;
    uint8_t hardness;
    bool has_inventory = false;
    bool interruptable = 0;
    bool plot = 0;
    bool static_ = false;
    bool useable = false;
};

} // namespace nw
