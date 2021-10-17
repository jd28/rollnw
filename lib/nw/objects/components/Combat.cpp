#include "Combat.hpp"

namespace nw {

Combat::Combat(const GffStruct gff)
{
    gff.get_to("NaturalAC", ac_natural);

    size_t sz = gff["SpecAbilityList"].size();
    special_abilities.reserve(sz);
    for (size_t i = 0; i < sz; ++i) {
        SpecialAbility sa;
        gff["SpecAbilityList"][i].get_to("Spell", sa.spell);
        gff["SpecAbilityList"][i].get_to("SpellCasterLevel", sa.level);
        gff["SpecAbilityList"][i].get_to("SpellFlags", sa.flags);
        special_abilities.push_back(sa);
    }
}

} // namespace nw
