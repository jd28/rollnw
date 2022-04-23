#pragma once

#include "../../serialization/Archives.hpp"
#include "SpellBook.hpp"

#include <vector>

namespace nw {

struct ClassEntry {
    int32_t id;
    int16_t level;
    SpellBook spells;
};

struct LevelStats {
    LevelStats() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    std::vector<ClassEntry> classes;
};

} // namespace nw
