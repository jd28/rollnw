#pragma once

#include "ObjectBase.hpp"

#include "../rules/attributes.hpp"
#include "../rules/combat.hpp"
#include "Appearance.hpp"
#include "CombatInfo.hpp"
#include "Common.hpp"
#include "CreatureStats.hpp"
#include "Equips.hpp"
#include "Inventory.hpp"
#include "Item.hpp"
#include "LevelHistory.hpp"
#include "LevelStats.hpp"
#include "Location.hpp"

namespace nw {

struct CreatureScripts {
    CreatureScripts() = default;

    bool deserialize(const GffStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool serialize(GffBuilderStruct& archive) const;
    nlohmann::json to_json() const;

    Resref on_attacked;
    Resref on_blocked;
    Resref on_conversation;
    Resref on_damaged;
    Resref on_death;
    Resref on_disturbed;
    Resref on_endround;
    Resref on_heartbeat;
    Resref on_perceived;
    Resref on_rested;
    Resref on_spawn;
    Resref on_spell_cast_at;
    Resref on_user_defined;
};

struct Creature : public ObjectBase {
    Creature();
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::creature;
    static constexpr ResourceType::type restype = ResourceType::utc;

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    virtual Creature* as_creature() override { return this; }
    virtual const Creature* as_creature() const override { return this; }
    virtual bool instantiate() override;
    virtual Versus versus_me() const override;

    static bool deserialize(Creature* obj, const nlohmann::json& archive, SerializationProfile profile);
    static bool serialize(const Creature* obj, nlohmann::json& archive, SerializationProfile profile);

    Common common;
    Appearance appearance;
    CombatInfo combat_info;
    Equips equipment;
    Inventory inventory;
    LevelStats levels;
    LevelHistory history;
    CreatureScripts scripts;
    CreatureStats stats;

    Resref conversation;
    std::string deity;
    LocString description;
    LocString name_first;
    LocString name_last;
    std::string subrace;

    float cr = 0.0;
    int32_t cr_adjust = 0;
    uint32_t decay_time;
    Race race = Race::invalid();
    int32_t walkrate = 0;

    uint16_t faction_id = 0;
    int16_t hp = 0;
    int16_t hp_current = 0;
    int16_t hp_max = 0;
    int16_t hp_temp = 0;
    uint16_t soundset;

    // Transient
    int32_t hasted = 0;
    int32_t size = 0;

    // Serialized
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
    bool plot = false;
    uint8_t starting_package = 0;

    bool instantiated_ = false;
};

// == Creature - Serialization - Gff ==========================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY

bool deserialize(Creature* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Creature* obj, SerializationProfile profile);
bool serialize(const Creature* obj, GffBuilderStruct& archive, SerializationProfile profile);

#endif // ROLLNW_ENABLE_LEGACY
} // namespace nw
