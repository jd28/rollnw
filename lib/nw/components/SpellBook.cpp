#include "SpellBook.hpp"

#include "../log.hpp"
#include "../util/templates.hpp"

#include <nlohmann/json.hpp>

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
    known.resize(10);
    memorized.resize(10);
}

bool SpellBook::from_gff(const GffStruct& gff)
{
    for (size_t i = 0; i < 10; ++i) {
        auto k = fmt::format("KnownList{}", i);
        size_t sz = gff[k].size();
        for (size_t j = 0; j < sz; ++j) {
            SpellEntry s;
            uint16_t temp;
            if (gff[k][j].get_to("Spell", temp)) {
                s.spell = Spell::make(temp);
            }
            gff[k][j].get_to("SpellFlags", s.flags);
            gff[k][j].get_to("SpellMetaMagic", s.meta);
            known[i].push_back(s);
        }

        auto m = fmt::format("MemorizedList{}", i);
        sz = gff[m].size();
        for (size_t j = 0; j < sz; ++j) {
            SpellEntry s;
            uint16_t temp;
            if (gff[k][j].get_to("Spell", temp)) {
                s.spell = Spell::make(temp);
            }
            gff[m][j].get_to("SpellFlags", s.flags);
            gff[m][j].get_to("SpellMetaMagic", s.meta);
            memorized[i].push_back(s);
        }
    }
    return true;
}

bool SpellBook::from_json(const nlohmann::json& archive)
{
    try {
        archive.at("known").get_to(known);
        archive.at("memorized").get_to(memorized);
    } catch (const nlohmann::json::exception& e) {
        LOG_F(ERROR, "SpellBook: json exception: {}", e.what());
        return false;
    }
    return true;
}

bool SpellBook::to_gff(GffBuilderStruct& archive) const
{
    uint8_t flags, meta;

    for (size_t i = 0; i < 10; ++i) {
        if (known[i].empty()) {
            continue;
        }
        auto k = fmt::format("KnownList{}", i);
        auto& klist = archive.add_list(k);
        for (const auto& sp : known[i]) {
            klist.push_back(3)
                .add_field("Spell", uint16_t(*sp.spell))
                .add_field("SpellFlags", static_cast<uint8_t>(sp.flags))
                .add_field("SpellMetaMagic", static_cast<uint8_t>(sp.meta));
        }
    }

    for (size_t i = 0; i < 10; ++i) {
        if (memorized[i].empty()) {
            continue;
        }
        auto m = fmt::format("MemorizedList{}", i);
        auto& mlist = archive.add_list(m);
        for (const auto& sp : memorized[i]) {
            mlist.push_back(3)
                .add_field("Spell", uint16_t(*sp.spell))
                .add_field("SpellFlags", static_cast<uint8_t>(sp.flags))
                .add_field("SpellMetaMagic", static_cast<uint8_t>(sp.meta));
        }
    }

    return true;
}

nlohmann::json SpellBook::to_json() const
{
    nlohmann::json j;
    j["known"] = known;
    j["memorized"] = memorized;
    return j;
}

} // namespace nw
