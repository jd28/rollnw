#pragma once

#include "../rules/Class.hpp"
#include "../serialization/Archives.hpp"
#include "SpellBook.hpp"

#include <array>
#include <limits>

namespace nw {

struct ClassEntry {
    Class id = nw::Class::invalid();
    int16_t level = 0;
    SpellBook spells;
};

struct LevelStats {
    LevelStats() = default;
    static constexpr size_t max_classes = 8;
    static constexpr size_t npos = std::numeric_limits<size_t>::max();

    bool from_gff(const GffStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffBuilderStruct& archive) const;
    nlohmann::json to_json() const;

    /// Determines total level
    int level() const noexcept;

    /// Determines level by class
    int level_by_class(Class id) const noexcept;

    /// Returns the position of the class, or ``npos``.
    size_t position(Class id) const noexcept;

    std::array<ClassEntry, max_classes> entries;
};

} // namespace nw