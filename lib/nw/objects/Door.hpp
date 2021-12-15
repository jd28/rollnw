#pragma once

#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Lock.hpp"
#include "components/Saves.hpp"
#include "components/Trap.hpp"

namespace nw {

struct Door : public ObjectBase {
    enum struct AnimationState : uint8_t {
        closed = 0,
        opened1 = 1,
        opened2 = 2
    };

    Door(const GffInputArchiveStruct gff, SerializationProfile profile);
    virtual Common* common() override { return &common_; }

    Common common_;

    AnimationState animation_state = AnimationState::closed;
    std::string linked_to;
    Resref on_click;
    Resref on_open_failure;
    uint16_t loadscreen = 0;
    uint8_t generic_type = 0;
    uint8_t linked_to_flags = 0;

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
