#include "legacy_spellbook_gff.hpp"

#include "../../kernel/Kernel.hpp"
#include "../../kernel/Rules.hpp"
#include "../../serialization/Gff.hpp"
#include "../../serialization/GffBuilder.hpp"

#include <fmt/format.h>

namespace nwn1 {

LegacySpellBook::LegacySpellBook()
    : LegacySpellBook{nw::kernel::global_allocator()}
{
}

LegacySpellBook::LegacySpellBook(nw::MemoryResource* allocator)
    : known{nw::kernel::rules().maximum_spell_levels(), allocator}
    , memorized{nw::kernel::rules().maximum_spell_levels(), allocator}
{
    known.resize(nw::kernel::rules().maximum_spell_levels());
    memorized.resize(nw::kernel::rules().maximum_spell_levels());
}

bool deserialize_legacy_spellbook(LegacySpellBook& out, const nw::GffStruct& archive)
{
    for (size_t i = 0; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        auto known_label = fmt::format("KnownList{}", i);
        auto known_field = archive[known_label];
        if (known_field.valid()) {
            const size_t count = known_field.size();
            for (size_t j = 0; j < count; ++j) {
                uint16_t spell = 0;
                if (known_field[j].get_to("Spell", spell)) {
                    out.known[i].push_back(nw::Spell::make(spell));
                }
            }
        }

        auto memorized_label = fmt::format("MemorizedList{}", i);
        auto memorized_field = archive[memorized_label];
        if (memorized_field.valid()) {
            const size_t count = memorized_field.size();
            for (size_t j = 0; j < count; ++j) {
                LegacySpellBookEntry entry;
                uint16_t spell = 0;
                uint8_t metamagic = 0;
                if (memorized_field[j].get_to("Spell", spell)) {
                    entry.spell = nw::Spell::make(spell);
                }
                memorized_field[j].get_to("SpellFlags", entry.flags);
                memorized_field[j].get_to("SpellMetaMagic", metamagic);
                entry.meta = nw::MetaMagicCode::make(metamagic);
                out.memorized[i].push_back(entry);
            }
        }
    }
    return true;
}

void serialize_legacy_spellbook(const LegacySpellBook& book, nw::GffBuilderStruct& archive)
{
    for (size_t i = 0; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        if (book.known[i].empty()) { continue; }

        auto label = fmt::format("KnownList{}", i);
        auto& list = archive.add_list(label);
        for (const nw::Spell spell : book.known[i]) {
            list.push_back(3)
                .add_field("Spell", uint16_t(*spell))
                .add_field("SpellFlags", static_cast<uint8_t>(1))
                .add_field("SpellMetaMagic", static_cast<uint8_t>(0));
        }
    }

    for (size_t i = 0; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        if (book.memorized[i].empty()) { continue; }

        auto label = fmt::format("MemorizedList{}", i);
        auto& list = archive.add_list(label);
        for (const LegacySpellBookEntry& spell : book.memorized[i]) {
            if (spell.spell == nw::Spell::invalid()) { continue; }
            list.push_back(3)
                .add_field("Spell", uint16_t(*spell.spell))
                .add_field("SpellFlags", static_cast<uint8_t>(spell.flags))
                .add_field("SpellMetaMagic", static_cast<uint8_t>(*spell.meta));
        }
    }
}

} // namespace nwn1
