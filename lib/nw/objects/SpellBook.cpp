#include "SpellBook.hpp"

#include "../log.hpp"
#include "../util/templates.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace nw {

void from_json(const nlohmann::json& j, SpellEntry& spell)
{
    j.at("spell").get_to(spell.spell);
    j.at("metamagic").get_to(spell.meta);
    j.at("flags").get_to(spell.flags);
}

void to_json(nlohmann::json& j, const SpellEntry& spell)
{
    j["spell"] = spell.spell;
    j["metamagic"] = spell.meta;
    j["flags"] = spell.flags;
}

SpellBook::SpellBook()
{
    known_.resize(10);
    memorized_.resize(10);
}

bool SpellBook::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("known").get_to(known_);
        archive.at("memorized").get_to(memorized_);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "SpellBook: json exception: {}", e.what());
        return false;
    }
    return true;
}

nlohmann::json SpellBook::to_json() const
{
    nlohmann::json j;
    j["known"] = known_;
    j["memorized"] = memorized_;
    return j;
}

bool SpellBook::add_known_spell(size_t level, SpellEntry entry)
{
    if (level > known_.size()) {
        known_.resize(level);
        known_[level].push_back(entry);
        return true;
    }
    auto it = std::find(std::begin(known_[level]), std::end(known_[level]), entry);
    if (it == std::end(known_[level])) {
        known_[level].push_back(entry);
        return true;
    }
    return false;
}

bool SpellBook::add_memorized_spell(size_t level, SpellEntry entry)
{
    if (level > memorized_.size()) {
        memorized_.resize(level);
        memorized_[level].push_back(entry);
        return true;
    }
    auto it = std::find(std::begin(memorized_[level]), std::end(memorized_[level]), entry);
    if (it == std::end(memorized_[level])) {
        memorized_[level].push_back(entry);
        return true;
    }
    return false;
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

SpellEntry SpellBook::get_known_spell(size_t level, size_t index) const
{
    if (level < known_.size() && index < known_[level].size()) {
        return known_[level][index];
    }
    return {};
}

SpellEntry SpellBook::get_memorized_spell(size_t level, size_t index) const
{
    if (level < memorized_.size() && index < memorized_[level].size()) {
        return memorized_[level][index];
    }
    return {};
}

void SpellBook::remove_known_spell(size_t level, SpellEntry entry)
{
    if (level < known_.size()) {
        known_[level].erase(std::remove(std::begin(known_[level]), std::end(known_[level]), entry),
            std::end(known_[level]));
    }
}

void SpellBook::remove_memorized_spell(size_t level, SpellEntry entry)
{
    if (level < memorized_.size()) {
        memorized_[level].erase(std::remove(std::begin(memorized_[level]), std::end(memorized_[level]), entry),
            std::end(memorized_[level]));
    }
}

#ifdef ROLLNW_ENABLE_LEGACY
bool deserialize(SpellBook& self, const GffStruct& archive)
{
    for (size_t i = 0; i < 10; ++i) {
        auto k = fmt::format("KnownList{}", i);
        auto kfield = archive[k];
        if (kfield.valid()) {
            size_t sz = kfield.size();
            for (size_t j = 0; j < sz; ++j) {
                SpellEntry s;
                uint16_t temp;
                if (kfield[j].get_to("Spell", temp)) {
                    s.spell = Spell::make(temp);
                }
                kfield[j].get_to("SpellFlags", s.flags, false);
                kfield[j].get_to("SpellMetaMagic", s.meta, false);
                self.known_[i].push_back(s);
            }
        }

        auto m = fmt::format("MemorizedList{}", i);
        auto mfield = archive[m];
        if (mfield.valid()) {
            size_t sz = mfield.size();
            for (size_t j = 0; j < sz; ++j) {
                SpellEntry s;
                uint16_t temp;
                if (mfield[j].get_to("Spell", temp)) {
                    s.spell = Spell::make(temp);
                }
                mfield[j].get_to("SpellFlags", s.flags);
                mfield[j].get_to("SpellMetaMagic", s.meta);
                self.memorized_[i].push_back(s);
            }
        }
    }
    return true;
}

bool serialize(const SpellBook& self, GffBuilderStruct& archive)
{
    for (size_t i = 0; i < 10; ++i) {
        if (self.known_[i].empty()) {
            continue;
        }
        auto k = fmt::format("KnownList{}", i);
        auto& klist = archive.add_list(k);
        for (const auto& sp : self.known_[i]) {
            klist.push_back(3)
                .add_field("Spell", uint16_t(*sp.spell))
                .add_field("SpellFlags", static_cast<uint8_t>(sp.flags))
                .add_field("SpellMetaMagic", static_cast<uint8_t>(sp.meta));
        }
    }

    for (size_t i = 0; i < 10; ++i) {
        if (self.memorized_[i].empty()) {
            continue;
        }
        auto m = fmt::format("MemorizedList{}", i);
        auto& mlist = archive.add_list(m);
        for (const auto& sp : self.memorized_[i]) {
            mlist.push_back(3)
                .add_field("Spell", uint16_t(*sp.spell))
                .add_field("SpellFlags", static_cast<uint8_t>(sp.flags))
                .add_field("SpellMetaMagic", static_cast<uint8_t>(sp.meta));
        }
    }

    return true;
}

#endif // ROLLNW_ENABLE_LEGACY

} // namespace nw
