#pragma once

#include "Item.hpp"
#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Inventory.hpp"
#include "components/Situated.hpp"

namespace nw {

struct Placeable : public ObjectBase {
    enum struct AnimationState {
        none = 0, // Technically "default"
        open = 1,
        closed = 2,
        destroyed = 3,
        activated = 4,
        deactivated = 5
    };

    Placeable(const GffInputArchiveStruct gff, SerializationProfile profile);
    virtual Common* common() override { return &common_; }
    virtual Situated* situated() override { return &situated_; }

    Common common_;
    Situated situated_;

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
