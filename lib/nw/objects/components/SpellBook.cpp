#include "SpellBook.hpp"

#include <nlohmann/json.hpp>

namespace nw {

bool SpellBook::from_gff(const GffInputArchiveStruct& gff)
{
    for (size_t i = 0; i < 10; ++i) {
        auto m = fmt::format("MemorizedList{}", i);
        size_t sz = gff[m].size();
        for (size_t j = 0; j < sz; ++j) {
            Spell s;
            gff[m][j].get_to("Spell", s.spell);
            gff[m][j].get_to("SpellFlags", s.flags);
            gff[m][j].get_to("SpellMetaMagic", s.meta);
            memorized[i].push_back(s);
        }

        auto k = fmt::format("KnownList{}", i);
        sz = gff[k].size();
        for (size_t j = 0; j < sz; ++j) {
            Spell s;
            gff[k][j].get_to("Spell", s.spell);
            gff[k][j].get_to("SpellFlags", s.flags);
            gff[k][j].get_to("SpellMetaMagic", s.meta);
            known[i].push_back(s);
        }
    }
    return true;
}

bool SpellBook::to_gff(GffOutputArchiveStruct& archive) const
{
    // for (size_t i = 0; i < 10; ++i) {
    //     auto m = fmt::format("MemorizedList{}", i);
    //     size_t sz = gff[m].size();
    //     for (size_t j = 0; j < sz; ++j) {
    //         Spell s;
    //         gff[m][j].get_to("Spell", s.spell);
    //         gff[m][j].get_to("SpellFlags", s.flags);
    //         gff[m][j].get_to("SpellMetaMagic", s.meta);
    //         memorized[i].push_back(s);
    //     }

    //     auto k = fmt::format("KnownList{}", i);
    //     sz = gff[k].size();
    //     for (size_t j = 0; j < sz; ++j) {
    //         Spell s;
    //         gff[k][j].get_to("Spell", s.spell);
    //         gff[k][j].get_to("SpellFlags", s.flags);
    //         gff[k][j].get_to("SpellMetaMagic", s.meta);
    //         known[i].push_back(s);
    //     }
    // }

    return true;
}

nlohmann::json SpellBook::to_json() const
{
    nlohmann::json j;
    // j["known"] = known;
    // j["memorized"] = memorized;
    return j;
}

} // namespace nw
