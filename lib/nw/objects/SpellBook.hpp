#pragma once

#include "../rules/Spell.hpp"
#include "../serialization/Serialization.hpp"
#include "../util/FixedVector.hpp"

namespace nw {

struct SpellEntry {
    Spell spell = Spell::invalid();
    SpellMetaMagic meta = SpellMetaMagic::none;
    SpellFlags flags = SpellFlags::none;

    bool operator==(const SpellEntry&) const = default;
    auto operator<=>(const SpellEntry&) const = default;
};

struct SpellBook {
    SpellBook();
    SpellBook(nw::MemoryResource* allocator);

    bool from_json(const nlohmann::json& archive);
    nlohmann::json to_json() const;

    /// Adds a known spell at level
    bool add_known_spell(size_t level, Spell spell);

    /// Adds a memorized spell at level
    bool add_memorized_spell(size_t level, SpellEntry entry);

    /// Counts all known spells
    size_t all_known_spell_count() const noexcept;

    /// Counts all memorized spells
    size_t all_memorized_spell_count() const noexcept;

    /// Gets available slots
    int available_slots(size_t level);

    /// Clears all memorized slots matching ``spell``, regardless of metamagic or flags
    void clear_memorized_spell(size_t level, Spell spell);

    /// Clears all memorized slots matching ``spell``, regardless of metamagic or flags
    void clear_memorized_spell_slot(size_t level, int slot);

    /// Gets the number of known at a given level
    size_t get_known_spell_count(size_t level) const;

    /// Gets the number of memorized at a given level
    size_t get_memorized_spell_count(size_t level) const;

    /// Gets a known spell entry
    Spell get_known_spell(size_t level, int slot) const;

    /// Gets a memorized spell entry
    SpellEntry get_memorized_spell(size_t level, int slot) const;

    /// Gets the number of times spell is memorized
    int has_memorized_spell(Spell spell, SpellMetaMagic meta = SpellMetaMagic::none) const;

    /// Determines if spell is known, note that NPC wizards do not 'know' any spells.
    bool knows_spell(Spell spell) const;

    /// Removes a known spell entry
    void remove_known_spell(size_t level, Spell spell);

    /// Finds a spell slot
    int find_memorized_slot(size_t level, Spell spell, SpellMetaMagic meta = SpellMetaMagic::none) const;

    /// Finds first empty slot, else -1
    int first_empty_slot(size_t level) const noexcept;

    /// Sets the available number of memorized slots
    void set_available_slots(size_t level, size_t slots);

    FixedVector<Vector<Spell>> known_;
    FixedVector<Vector<SpellEntry>> memorized_;
};

bool deserialize(SpellBook& self, const GffStruct& archive);
bool serialize(const SpellBook& self, GffBuilderStruct& archive);

} // namespace nw
