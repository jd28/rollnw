#include "SpellBook.hpp"

#include "../kernel/Rules.hpp"
#include "../log.hpp"
#include "../serialization/Gff.hpp"
#include "../serialization/GffBuilder.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

inline void from_json(const nlohmann::json& j, SpellEntry& spell)
{
    j.at("spell").get_to(spell.spell);
    j.at("metamagic").get_to(spell.meta);
    j.at("flags").get_to(spell.flags);
}

inline void to_json(nlohmann::json& j, const SpellEntry& spell)
{
    j["spell"] = spell.spell;
    j["metamagic"] = spell.meta;
    j["flags"] = spell.flags;
}

SpellBook::SpellBook()
    : SpellBook{nw::kernel::global_allocator()}
{
}

SpellBook::SpellBook(nw::MemoryResource* allocator)
    : known_{nw::kernel::rules().maximum_spell_levels(), allocator}
    , memorized_{nw::kernel::rules().maximum_spell_levels(), allocator}
{
    known_.resize(nw::kernel::rules().maximum_spell_levels());
    memorized_.resize(nw::kernel::rules().maximum_spell_levels());
}

bool SpellBook::from_json(const nlohmann::json& archive)
{
    try {
        auto it = archive.find("known");
        if (it != archive.end()) {
            for (auto& val : *it) {
                auto level = val.at("level").get<size_t>();
                val.at("spells").get_to(known_[level]);
            }
        }

        it = archive.find("memorized");
        if (it != archive.end()) {
            for (auto& val : *it) {
                auto level = val.at("level").get<size_t>();
                val.at("spells").get_to(memorized_[level]);
            }
        }
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "SpellBook: json exception: {}", e.what());
        return false;
    }
    return true;
}

nlohmann::json SpellBook::to_json() const
{
    nlohmann::json j;

    j["known"] = nlohmann::json::array();
    for (size_t i = 0; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        if (known_[i].empty()) { continue; }
        j["known"].push_back({
            {"level", i},
            {"spells", known_[i]},
        });
    }

    j["memorized"] = nlohmann::json::array();
    for (size_t i = 0; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        if (memorized_[i].empty()) { continue; }

        auto spells = nlohmann::json::array();
        for (const auto& it : memorized_[i]) {
            // Don't save any invalid slots
            if (it.spell == Spell::invalid()) { continue; }
            spells.push_back(it);
        }

        j["memorized"].push_back({
            {"level", i},
            {"spells", spells},
        });
    }

    return j;
}

bool SpellBook::add_known_spell(size_t level, Spell spell)
{
    if (spell == Spell::invalid() || level >= known_.size()) { return false; }
    auto it = std::find(std::begin(known_[level]), std::end(known_[level]), spell);
    if (it == std::end(known_[level])) {
        known_[level].push_back(spell);
        return true;
    }
    return false;
}

bool SpellBook::add_memorized_spell(size_t level, SpellEntry entry)
{
    int slot = first_empty_slot(level);
    if (slot == -1) { return false; }
    memorized_[level][slot] = entry;
    return true;
}

size_t SpellBook::all_known_spell_count() const noexcept
{
    size_t result = 0;
    for (const auto& it : known_) {
        result += it.size();
    }
    return result;
}

size_t SpellBook::all_memorized_spell_count() const noexcept
{
    size_t result = 0;
    for (const auto& it : memorized_) {
        result += it.size();
    }
    return result;
}

int SpellBook::available_slots(size_t level) const noexcept
{
    int result = 0;
    for (const auto& it : memorized_[level]) {
        if (it == SpellEntry{}) { ++result; }
    }
    return result;
}

void SpellBook::clear_memorized_spell(size_t level, Spell spell)
{
    // Got to look in higher levels for metamagic usages
    for (size_t i = level; i < memorized_.size(); ++i) {
        for (auto& it : memorized_[i]) {
            if (it.spell == spell) {
                it = SpellEntry{};
            }
        }
    }
}

void SpellBook::clear_memorized_spell_slot(size_t level, int slot)
{
    memorized_[level][slot] = SpellEntry{};
}

int SpellBook::find_memorized_slot(size_t level, Spell spell, MetaMagicFlag meta) const
{
    int i = 0;
    for (const auto& it : memorized_[level]) {
        if (it.spell == spell && it.meta == meta) {
            return i;
        }
        ++i;
    }

    return -1;
}

int SpellBook::first_empty_slot(size_t level) const noexcept
{
    int i = 0;
    for (const auto& it : memorized_[level]) {
        if (it == SpellEntry{}) {
            return i;
        }
        ++i;
    }

    return -1;
}

size_t SpellBook::get_known_spell_count(size_t level) const
{
    if (level < known_.size()) {
        return known_[level].size();
    }
    return 0;
}

size_t SpellBook::get_memorized_spell_count(size_t level) const
{
    if (level < memorized_.size()) {
        return memorized_[level].size();
    }
    return 0;
}

Spell SpellBook::get_known_spell(size_t level, int slot) const
{
    if (level < known_.size() && slot < known_[level].size()) {
        return known_[level][slot];
    }
    return {};
}

SpellEntry SpellBook::get_memorized_spell(size_t level, int slot) const
{
    if (level < memorized_.size() && slot < memorized_[level].size()) {
        return memorized_[level][slot];
    }
    return {};
}

int SpellBook::has_memorized_spell(Spell spell, MetaMagicFlag meta) const
{
    int result = 0;
    for (auto& level : memorized_) {
        for (auto& entry : level) {
            if (entry.spell == spell && (meta == metamagic_none || entry.meta == meta)) {
                ++result;
            }
        }
    }
    return result;
}

bool SpellBook::knows_spell(Spell spell) const
{
    for (auto& level : known_) {
        for (auto& entry : level) {
            if (entry == spell) { return true; }
        }
    }
    return false;
}

void SpellBook::remove_known_spell(size_t level, Spell spell)
{
    if (level < known_.size()) {
        auto it = std::remove(std::begin(known_[level]), std::end(known_[level]), spell);
        known_[level].erase(it, std::end(known_[level]));
    }
}

void SpellBook::set_available_slots(size_t level, size_t slots)
{
    memorized_[level].resize(slots);
}

bool deserialize(SpellBook& self, const GffStruct& archive)
{
    for (size_t i = 0; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        auto k = fmt::format("KnownList{}", i);
        auto kfield = archive[k];
        if (kfield.valid()) {
            size_t sz = kfield.size();
            for (size_t j = 0; j < sz; ++j) {
                Spell s;
                uint16_t temp;
                if (kfield[j].get_to("Spell", temp)) {
                    s = Spell::make(temp);
                }
                // These fields are useless, I think.
                // kfield[j].get_to("SpellFlags", s.flags, false);
                // kfield[j].get_to("SpellMetaMagic", s.meta, false);
                self.known_[i].push_back(s);
            }
        }

        auto m = fmt::format("MemorizedList{}", i);
        auto mfield = archive[m];
        if (mfield.valid()) {
            size_t sz = mfield.size();
            for (size_t j = 0; j < sz; ++j) {
                SpellEntry s;
                uint16_t temp_u16;
                uint8_t temp_u8;
                if (mfield[j].get_to("Spell", temp_u16)) {
                    s.spell = Spell::make(temp_u16);
                }
                mfield[j].get_to("SpellFlags", s.flags);
                mfield[j].get_to("SpellMetaMagic", temp_u8);
                s.meta = MetaMagicFlag::make(temp_u8);
                self.memorized_[i].push_back(s);
            }
        }
    }
    return true;
}

bool serialize(const SpellBook& self, GffBuilderStruct& archive)
{
    for (size_t i = 0; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        if (self.known_[i].empty()) {
            continue;
        }
        auto k = fmt::format("KnownList{}", i);
        auto& klist = archive.add_list(k);
        for (const auto& sp : self.known_[i]) {
            klist.push_back(3)
                .add_field("Spell", uint16_t(*sp))
                .add_field("SpellFlags", static_cast<uint8_t>(1))
                .add_field("SpellMetaMagic", static_cast<uint8_t>(0));
        }
    }

    for (size_t i = 0; i < nw::kernel::rules().maximum_spell_levels(); ++i) {
        if (self.memorized_[i].empty()) {
            continue;
        }
        auto m = fmt::format("MemorizedList{}", i);
        auto& mlist = archive.add_list(m);
        for (const auto& sp : self.memorized_[i]) {
            if (sp.spell == Spell::invalid()) { continue; } // Don't save any invalid slots
            mlist.push_back(3)
                .add_field("Spell", uint16_t(*sp.spell))
                .add_field("SpellFlags", static_cast<uint8_t>(sp.flags))
                .add_field("SpellMetaMagic", static_cast<uint8_t>(*sp.meta));
        }
    }

    return true;
}

} // namespace nw
