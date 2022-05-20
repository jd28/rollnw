#pragma once

#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Lock.hpp"
#include "components/Saves.hpp"
#include "components/Trap.hpp"

#include <flecs/flecs.h>

namespace nw {

enum struct DoorAnimationState : uint8_t {
    closed = 0,
    opened1 = 1,
    opened2 = 2
};

struct DoorScripts {
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
    Resref on_lock;
    Resref on_melee_attacked;
    Resref on_open;
    Resref on_open_failure;
    Resref on_spell_cast_at;
    Resref on_trap_triggered;
    Resref on_unlock;
    Resref on_user_defined;
};

struct Door {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::door;

    // Serialization
    static bool deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile);
    static bool deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile);
    static bool serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile);
    static GffOutputArchive serialize(const flecs::entity ent, SerializationProfile profile);
    static bool serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile);

    Resref conversation;
    LocString description;
    std::string linked_to;
    Saves saves;

    uint32_t appearance;
    uint32_t faction = 0;
    uint32_t generic_type = 0;

    int16_t hp = 0;
    int16_t hp_current = 0;
    uint16_t loadscreen = 0;
    uint16_t portrait_id;

    DoorAnimationState animation_state = DoorAnimationState::closed;
    uint8_t hardness;
    bool interruptable = 0;
    uint8_t linked_to_flags = 0;
    bool plot = false;
};

} // namespace nw
