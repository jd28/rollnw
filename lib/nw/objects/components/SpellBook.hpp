#pragma once

#include "../../serialization/Archives.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace nw {

enum struct SpellFlags : uint8_t {
    none = 0x0,
    readied = 0x01,
    spontaneouss = 0x02,
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

struct Spell {
    uint16_t spell;
    SpellMetaMagic meta = SpellMetaMagic::none;
    SpellFlags flags = SpellFlags::none;
};

struct SpellBook {
    SpellBook() = default;
    SpellBook(const GffInputArchiveStruct gff);

    std::array<std::vector<Spell>, 9> known;
    std::array<std::vector<Spell>, 9> memorized;
};

} // namespace nw
