#pragma once

#include "Item.hpp"
#include "SituatedObject.hpp"
#include "components/Inventory.hpp"

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

    Inventory inventory;
    AnimationState anim_state;

    uint8_t bodybag = 0;
    uint8_t has_inventory = 0;
    uint8_t useable = 0;
    uint8_t static_ = 0;

    Resref on_inv_disturbed;
    Resref on_used;
};

} // namespace nw
