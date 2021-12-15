#pragma once

#include "Item.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Inventory.hpp"
#include "components/Lock.hpp"
#include "components/Saves.hpp"
#include "components/Trap.hpp"

namespace nw {

struct Placeable : public ObjectBase {
    enum struct AnimationState : uint8_t {
        none = 0, // Technically "default"
        open = 1,
        closed = 2,
        destroyed = 3,
        activated = 4,
        deactivated = 5
    };

    Placeable(const GffInputArchiveStruct gff, SerializationProfile profile);
    virtual Common* common() override { return &common_; }

    Common common_;

    Inventory inventory;
    AnimationState animation_state;

    uint8_t bodybag = 0;
    uint8_t has_inventory = 0;
    uint8_t useable = 0;
    uint8_t static_ = 0;

    Resref on_inv_disturbed;
    Resref on_used;

    uint32_t appearance;
    uint16_t portrait_id;
    int16_t hp = 0;
    int16_t hp_current = 0;
    uint8_t hardness;
    bool interruptable = 0;
    bool plot = 0;

    LocString description;
    Resref conversation;
    Saves saves;
    Lock lock;
    Trap trap;

    // Script Events
    Resref on_closed;
    Resref on_damaged;
    Resref on_death;
    Resref on_disarm;
    Resref on_heartbeat;
    Resref on_lock;
    Resref on_melee_attacked;
    Resref on_open;
    Resref on_spell_cast_at;
    Resref on_trap_triggered;
    Resref on_unlock;
    Resref on_user_defined;
};

} // namespace nw
