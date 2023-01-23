#pragma once

#include "Common.hpp"
#include "Lock.hpp"
#include "ObjectBase.hpp"
#include "Saves.hpp"
#include "Trap.hpp"

namespace nw {

enum struct DoorAnimationState : uint8_t {
    closed = 0,
    opened1 = 1,
    opened2 = 2
};

struct DoorScripts {
    bool from_json(const nlohmann::json& archive);
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

struct Door : public ObjectBase {
    Door();

    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::door;
    static constexpr ResourceType::type restype = ResourceType::utd;

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    virtual Door* as_door() override { return this; }
    virtual const Door* as_door() const override { return this; }
    virtual bool instantiate() override { return true; }

    // Serialization
    static bool deserialize(Door* obj, const nlohmann::json& archive, SerializationProfile profile);
    static bool serialize(const Door* obj, nlohmann::json& archive, SerializationProfile profile);

    Common common;
    DoorScripts scripts;
    Lock lock;
    Trap trap;
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

    bool instantiated_ = true;
};

// == Door - Serialization - Gff ==============================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Door* obj, const GffStruct& archive, SerializationProfile profile);
bool serialize(const Door* obj, GffBuilderStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Door* obj, SerializationProfile profile);
bool deserialize(DoorScripts& self, const GffStruct& archive);
bool serialize(const DoorScripts& self, GffBuilderStruct& archive);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
