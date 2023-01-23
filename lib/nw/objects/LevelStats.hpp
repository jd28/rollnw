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

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    /// Determines total level
    int level() const noexcept;

    /// Determines level by class
    int level_by_class(Class id) const noexcept;

    /// Returns the position of the class, or ``npos``.
    size_t position(Class id) const noexcept;

    std::array<ClassEntry, max_classes> entries;
};

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(LevelStats& self, const GffStruct& archive);
bool serialize(const LevelStats& self, GffBuilderStruct& archive);
#endif
} // namespace nw
