#pragma once

#include "../serialization/Archives.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Trap.hpp"

#include <flecs/flecs.h>
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

struct Trigger {
    static constexpr int json_archive_version = 1;
    static constexpr ObjectType object_type = ObjectType::trigger;

    // Serialization
    static bool deserialize(flecs::entity ent, const GffInputArchiveStruct& archive, SerializationProfile profile);
    static bool deserialize(flecs::entity ent, const nlohmann::json& archive, SerializationProfile profile);
    static GffOutputArchive serialize(const flecs::entity ent, SerializationProfile profile);
    static bool serialize(const flecs::entity ent, GffOutputArchiveStruct& archive, SerializationProfile profile);
    static bool serialize(const flecs::entity ent, nlohmann::json& archive, SerializationProfile profile);

    std::vector<glm::vec3> geometry;
    std::string linked_to;

    uint32_t faction = 0;
    float highlight_height = 0.0f;
    int32_t type = 0;

    uint16_t loadscreen = 0;
    uint16_t portrait = 0;

    uint8_t cursor = 0;
    uint8_t linked_to_flags = 0;
};

} // namespace nw
