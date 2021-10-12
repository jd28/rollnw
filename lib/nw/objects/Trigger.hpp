#pragma once

#include "../formats/Gff.hpp"
#include "Object.hpp"
#include "Serialization.hpp"
#include "components/Trap.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace nw {

struct Trigger : public Object {
    Trigger(const GffStruct gff, SerializationProfile profile);

    Trap trap;
    std::vector<glm::vec3> geometery;
    std::string linked_to;
    Resref on_click;
    Resref on_disarm;
    Resref on_enter;
    Resref on_exit;
    Resref on_heartbeat;
    Resref on_trap_triggered;
    Resref on_user_defined;
    float highlight_height = 0.0f;

    uint16_t loadscreen = 0;
    uint16_t portrait = 0;
    uint8_t cursor = 0;
    uint8_t linked_to_flags = 0;
};

} // namespace nw
