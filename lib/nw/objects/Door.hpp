#pragma once

#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Lock.hpp"
#include "components/Saves.hpp"
#include "components/Trap.hpp"

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
    Resref on_open_failure;
    Resref on_open;
    Resref on_spell_cast_at;
    Resref on_trap_triggered;
    Resref on_unlock;
    Resref on_user_defined;
};

struct Door : public ObjectBase {
    Door();
    Door(const GffInputArchiveStruct& archive, SerializationProfile profile);
    Door(const nlohmann::json& archive, SerializationProfile profile);

    static constexpr int json_archive_version = 1;

    // Validity
    bool valid() { return valid_; }

    // ObjectBase overrides
    virtual Common* common() override { return &common_; }
    virtual const Common* common() const override { return &common_; }
    virtual Door* as_door() override { return this; }
    virtual const Door* as_door() const override { return this; }

    // Serialization
    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    Common common_;
    DoorScripts scripts;

    DoorAnimationState animation_state = DoorAnimationState::closed;
    std::string linked_to;
    uint32_t faction = 0;
    uint16_t loadscreen = 0;
    uint32_t generic_type = 0;
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

private:
    bool valid_ = false;
};

} // namespace nw
