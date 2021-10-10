#pragma once

#include "Object.hpp"
#include "Serialization.hpp"
#include "components/Lock.hpp"
#include "components/Saves.hpp"
#include "components/Trap.hpp"

namespace nw {

struct SituatedObject : public Object {
    SituatedObject() = default;
    SituatedObject(const GffStruct gff, SerializationProfile profile);

    uint32_t appearance;
    uint16_t portrait_id;
    uint8_t hardness;
    uint8_t interruptable = 0;
    uint8_t plot = 0;

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
    Resref on_meleeattacked;
    Resref on_open;
    Resref on_spellcastat;
    Resref on_traptriggered;
    Resref on_unlock;
    Resref on_userdefined;
};

} // namespace nw
