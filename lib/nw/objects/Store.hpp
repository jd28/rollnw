#pragma once

#include "../formats/Gff.hpp"
#include "Object.hpp"

namespace nw {

struct Store : public Object {
    Store(const GffStruct gff, SerializationProfile profile);
};

} // namespace nw
