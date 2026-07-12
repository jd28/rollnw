#pragma once

#include "../../rules/Spell.hpp"
#include "../../util/FixedVector.hpp"

namespace nw {
struct GffBuilderStruct;
struct GffStruct;
} // namespace nw

namespace nwn1 {

struct LegacySpellBookEntry {
    nw::Spell spell = nw::Spell::invalid();
    nw::MetaMagicCode meta = nw::metamagic_none;
    nw::SpellFlags flags = nw::SpellFlags::none;
};

struct LegacySpellBook {
    LegacySpellBook();
    LegacySpellBook(nw::MemoryResource* allocator);

    nw::FixedVector<nw::Vector<nw::Spell>> known;
    nw::FixedVector<nw::Vector<LegacySpellBookEntry>> memorized;
};

[[nodiscard]] bool deserialize_legacy_spellbook(LegacySpellBook& out, const nw::GffStruct& archive);
void serialize_legacy_spellbook(const LegacySpellBook& book, nw::GffBuilderStruct& archive);

} // namespace nwn1
