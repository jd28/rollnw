#pragma once

#include "Object.hpp"

#include "../formats/Gff.hpp"
#include "components/Location.hpp"
#include "components/Stats.hpp"

#include <nlohmann/json.hpp>

namespace nw {

struct Creature : public Object {
    Creature(const GffStruct gff, SerializationProfile profile);

    Location loc;
};

} // namespace nw
