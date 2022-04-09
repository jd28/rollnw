#pragma once

#include "../serialization/Archives.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Trap.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace nw {

struct TriggerScripts {
    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    Resref on_click;
    Resref on_disarm;
    Resref on_enter;
    Resref on_exit;
    Resref on_heartbeat;
    Resref on_trap_triggered;
    Resref on_user_defined;
};

struct Trigger : public ObjectBase {
    Trigger();
    Trigger(const GffInputArchiveStruct& archive, SerializationProfile profile);
    Trigger(const nlohmann::json& archive, SerializationProfile profile);

    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::trigger;

    // Validity
    bool valid() { return valid_; }

    // ObjectBase overrides
    virtual Common* common() override { return &common_; }
    virtual const Common* common() const override { return &common_; }
    virtual Trigger* as_trigger() override { return this; }
    virtual const Trigger* as_trigger() const override { return this; }

    // Serialization
    bool from_gff(const GffInputArchiveStruct& archive, SerializationProfile profile);
    bool from_json(const nlohmann::json& archive, SerializationProfile profile);
    bool to_gff(GffOutputArchiveStruct& archive, SerializationProfile profile) const;
    GffOutputArchive to_gff(SerializationProfile profile) const;
    nlohmann::json to_json(SerializationProfile profile) const;

    Common common_;
    std::vector<glm::vec3> geometry;
    std::string linked_to;
    TriggerScripts scripts;
    Trap trap;

    uint32_t faction = 0;
    float highlight_height = 0.0f;
    int32_t type = 0;

    uint16_t loadscreen = 0;
    uint16_t portrait = 0;

    uint8_t cursor = 0;
    uint8_t linked_to_flags = 0;

private:
    bool valid_ = false;
};

} // namespace nw
