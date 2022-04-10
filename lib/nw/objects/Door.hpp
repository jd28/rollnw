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
    Resref on_open;
    Resref on_open_failure;
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
    static constexpr ObjectType object_type = ObjectType::door;

    // ObjectBase overrides
    virtual bool valid() const noexcept override;
    virtual Common* common() override;
    virtual const Common* common() const override;
    virtual Door* as_door() override;
    virtual const Door* as_door() const override;

    // Serialization
    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    GffOutputArchive to_gff(SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    Common common_;
    Resref conversation;
    LocString description;
    std::string linked_to;
    Lock lock;
    DoorScripts scripts;
    Saves saves;
    Trap trap;

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

private:
    bool valid_ = false;
};

} // namespace nw
