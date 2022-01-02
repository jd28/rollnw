#pragma once

#include "../../serialization/Archives.hpp"
#include "SpellBook.hpp"

#include <vector>

namespace nw {

struct Class {
    int32_t id;
    int16_t level;
    SpellBook spells;
};

struct LevelStats {
    LevelStats() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;

    std::vector<Class> classes;
};

} // namespace nw
