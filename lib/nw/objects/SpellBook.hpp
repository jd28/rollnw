#pragma once

#include "../rules/Spell.hpp"
#include "../serialization/Archives.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace nw {

enum struct SpellFlags : uint8_t {
    none = 0x0,
    readied = 0x01,
    spontaneous = 0x02,
    unlimited = 0x04,
};

enum struct SpellMetaMagic : uint8_t {
    none = 0x00,
    empower = 0x01,
    extend = 0x02,
    maximize = 0x04,
    quicken = 0x08,
    silent = 0x10,
    still = 0x20,
};

struct SpellEntry {
    Spell spell = Spell::invalid();
    SpellMetaMagic meta = SpellMetaMagic::none;
    SpellFlags flags = SpellFlags::none;

    bool operator==(const SpellEntry&) const = default;
    auto operator<=>(const SpellEntry&) const = default;
};

void from_json(const nlohmann::json& j, SpellEntry& spell);
void to_json(nlohmann::json& j, const SpellEntry& spell);

struct SpellBook {
    SpellBook();

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    /// Adds a known spell at level
    bool add_known_spell(size_t level, SpellEntry entry);

    /// Adds a memorized spell at level
    bool add_memorized_spell(size_t level, SpellEntry entry);

    /// Gets the number of known at a given level
    size_t get_known_spell_count(size_t level) const;
    /// Gets the number of memorized at a given level
    size_t get_memorized_spell_count(size_t level) const;

    /// Gets a known spell entry
    SpellEntry get_known_spell(size_t level, size_t index) const;
    /// Gets a memorized spell entry
    SpellEntry get_memorized_spell(size_t level, size_t index) const;

    /// Removes a known spell entry
    void remove_known_spell(size_t level, SpellEntry entry);
    /// Removes a memorized spell entry
    void remove_memorized_spell(size_t level, SpellEntry entry);

    std::vector<std::vector<SpellEntry>> known_;
    std::vector<std::vector<SpellEntry>> memorized_;
};

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(SpellBook& self, const GffStruct& archive);
bool serialize(const SpellBook& self, GffBuilderStruct& archive);
#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
