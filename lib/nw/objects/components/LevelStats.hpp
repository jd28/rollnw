#pragma once

#include "../../rules/Class.hpp"
#include "../../serialization/Archives.hpp"
#include "SpellBook.hpp"

#include <vector>

namespace nw {

struct ClassEntry {
    Class id;
    int16_t level;
    SpellBook spells;
};

struct LevelStats {
    LevelStats() = default;

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    int level() const noexcept;
    int level_by_class(Class id) const noexcept;

    std::vector<ClassEntry> entries;
};

} // namespace nw
