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
};

} // namespace nw
