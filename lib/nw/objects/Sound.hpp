#pragma once

#include "../formats/Gff.hpp"
#include "Object.hpp"
#include "Serialization.hpp"

namespace nw {

struct Sound : public Object {
    Sound(const GffStruct gff, SerializationProfile profile);
};

} // namespace nw
