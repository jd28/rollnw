#pragma once

#include "../formats/Gff.hpp"
#include "Object.hpp"
#include "Serialization.hpp"
#include "components/Trap.hpp"

namespace nw {

struct Trigger : public Object {
    Trigger(const GffStruct gff, SerializationProfile profile);

    Trap trap;
};

} // namespace nw
