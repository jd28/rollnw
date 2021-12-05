#pragma once

#include "ObjectBase.hpp"

#include "Item.hpp"
#include "components/Appearance.hpp"
#include "components/Combat.hpp"
#include "components/Common.hpp"
#include "components/CreatureStats.hpp"
#include "components/Equips.hpp"
#include "components/Inventory.hpp"
#include "components/Location.hpp"

#include <nlohmann/json.hpp>

namespace nw {

struct Creature : public ObjectBase {
    Creature(const GffInputArchiveStruct gff, SerializationProfile profile);
    virtual Common* common() override { return &common_; }

    Common common_;
    Appearance appearance;
    LocString name_first;
    LocString name_last;
    Combat combat_info;
    CreatureStats stats;
    Equips equipment;
    Inventory inventory;
    Resref conversation;
    std::string deity;
    LocString description;
    std::string subrace;

    // Scripts
    Resref on_attacked;
    Resref on_damaged;
    Resref on_death;
    Resref on_conversation;
    Resref on_disturbed;
    Resref on_endround;
    Resref on_heartbeat;
    Resref on_blocked;
    Resref on_perceived;
    Resref on_rested;
    Resref on_spawn;
    Resref on_spell_cast_at;
    Resref on_user_defined;

    float cr = 0.0;
    int32_t cr_adjust = 0;
    uint32_t decay_time;
    int32_t walkrate = 0;

    uint16_t faction_id = 0;
    int16_t hp = 0;
    int16_t hp_current = 0;
    int16_t hp_max = 0;
    uint16_t soundset;

    uint8_t bodybag = 0;
    uint8_t chunk_death = 0;
    uint8_t disarmable = 0;
    uint8_t gender = 0;
    uint8_t good_evil = 50;
    uint8_t interruptable = 0;
    uint8_t immortal = 0;
    uint8_t lawful_chaotic = 50;
    uint8_t lootable = 0;
    uint8_t pc = 0;
    uint8_t perception_range = 0;
    uint8_t plot = 0;
    uint8_t race = 0;
    uint8_t starting_package = 0;
};

} // namespace nw
