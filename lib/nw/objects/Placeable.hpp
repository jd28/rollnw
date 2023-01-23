#pragma once

#include "Common.hpp"
#include "Inventory.hpp"
#include "Item.hpp"
#include "Lock.hpp"
#include "ObjectBase.hpp"
#include "Saves.hpp"
#include "Trap.hpp"

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
    bool from_json(const nlohmann::json& archive);
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

struct Placeable : public ObjectBase {
    Placeable();
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::placeable;
    static constexpr ResourceType::type restype = ResourceType::utp;

    virtual Common* as_common() override { return &common; }
    virtual const Common* as_common() const override { return &common; }
    virtual Placeable* as_placeable() override { return this; }
    virtual const Placeable* as_placeable() const override { return this; }
    virtual bool instantiate() override;

    // Serialization
    static bool deserialize(Placeable* obj, const nlohmann::json& archive, SerializationProfile profile);
    static bool serialize(const Placeable* obj, nlohmann::json& archive, SerializationProfile profile);

    Common common;
    PlaceableScripts scripts;
    Inventory inventory;
    Lock lock;
    Trap trap;

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

    bool instantiated_ = false;
};

// == Placeable - Serialization - Gff =========================================
// ============================================================================

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(Placeable* obj, const GffStruct& archive, SerializationProfile profile);
GffBuilder serialize(const Placeable* obj, SerializationProfile profile);
bool serialize(const Placeable* obj, GffBuilderStruct& archive, SerializationProfile profile);

bool deserialize(PlaceableScripts& self, const GffStruct& archive);
bool serialize(const PlaceableScripts& self, GffBuilderStruct& archive);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
