#pragma once

#include "SituatedObject.hpp"

namespace nw {

struct Placeable : public SituatedObject {
    enum struct AnimationState {
        none = 0, // Technically "default"
        open = 1,
        closed = 2,
        destroyed = 3,
        activated = 4,
        deactivated = 5
    };

    Placeable(const GffStruct gff, SerializationProfile profile);
};

} // namespace nw
