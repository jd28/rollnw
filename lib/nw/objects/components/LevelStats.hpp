#pragma once

#include "../Serialization.hpp"
#include "SpellBook.hpp"

#include <vector>

namespace nw {

struct Class {
    int32_t id;
    int16_t level;
    SpellBook spells;
};

struct LevelStats {
    LevelStats(const GffStruct gff);

    std::vector<Class> classes;
};

} // namespace nw
