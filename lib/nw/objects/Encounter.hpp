#pragma once

#include "../formats/Gff.hpp"
#include "Object.hpp"
#include "Serialization.hpp"

namespace nw {

struct Encounter : public Object {
    Encounter(const GffStruct gff, SerializationProfile profile);
};

} // namespace nw
