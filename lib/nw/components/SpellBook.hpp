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
    Spell spell = Spell::invalid;
    SpellMetaMagic meta = SpellMetaMagic::none;
    SpellFlags flags = SpellFlags::none;
};

void from_json(const nlohmann::json& j, SpellEntry& spell);
void to_json(nlohmann::json& j, const SpellEntry& spell);

struct SpellBook {
    SpellBook();

    bool from_gff(const GffInputArchiveStruct& archive);
    bool from_json(const nlohmann::json& archive);
    bool to_gff(GffOutputArchiveStruct& archive) const;
    nlohmann::json to_json() const;

    std::vector<std::vector<SpellEntry>> known;
    std::vector<std::vector<SpellEntry>> memorized;
};

} // namespace nw
