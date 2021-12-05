#pragma once

#include "ObjectBase.hpp"
#include "components/Common.hpp"
#include "components/Situated.hpp"

namespace nw {

struct Door : public ObjectBase {
    enum struct AnimationState {
        closed = 0,
        opened1 = 1,
        opened2 = 2
    };

    Door(const GffInputArchiveStruct gff, SerializationProfile profile);
    virtual Common* common() override { return &common_; }
    virtual Situated* situated() override { return &situated_; }

    Common common_;
    Situated situated_;
    AnimationState anim_state = AnimationState::closed;
    std::string linked_to;
    Resref on_click;
    Resref on_open_failure;
    uint16_t loadscreen = 0;
    uint8_t generic_type = 0;
    uint8_t linked_to_flags = 0;
};

} // namespace nw
