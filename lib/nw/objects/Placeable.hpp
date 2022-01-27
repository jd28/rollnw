#pragma once

#include "Item.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Inventory.hpp"
#include "components/Lock.hpp"
#include "components/Saves.hpp"
#include "components/Trap.hpp"

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

struct Placeable : public ObjectBase {
    Placeable();
    Placeable(const GffInputArchiveStruct& archive, SerializationProfile profile);
    Placeable(const nlohmann::json& archive, SerializationProfile profile);

    static constexpr int json_archive_version = 1;

    bool valid() const noexcept { return valid_; }

    // ObjectBase overrides
    virtual Common* common() override { return &common_; }
    virtual const Common* common() const override { return &common_; }
    virtual Placeable* as_placeable() override { return this; }
    virtual const Placeable* as_placeable() const override { return this; }

    // Serialization
    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    GffOutputArchive to_gff(SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    Common common_;
    PlaceableScripts scripts;

    Inventory inventory;
    PlaceableAnimationState animation_state;

    uint32_t faction = 0;
    uint8_t bodybag = 0;
    bool has_inventory = false;
    bool useable = false;
    bool static_ = false;

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

private:
    bool valid_ = false;
};

} // namespace nw
