#pragma once

#include "SituatedObject.hpp"

namespace nw {

struct Door : public SituatedObject {
    enum struct AnimationState {
        closed = 0,
        opened1 = 1,
        opened2 = 2
    };

    Door(const GffStruct gff, SerializationProfile profile);

    AnimationState anim_state = AnimationState::closed;
    std::string linked_to;
    Resref on_click;
    Resref on_open_failure;
    uint16_t loadscreen = 0;
    uint8_t generic_type = 0;
    uint8_t linked_to_flags = 0;
};

} // namespace nw
