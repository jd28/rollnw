#include "Spell.hpp"

#include "../formats/TwoDA.hpp"
#include "../kernel/Rules.hpp"

#include "nlohmann/json.hpp"

namespace nw {

DEFINE_RULE_TYPE(SpellSchool);
DEFINE_RULE_TYPE(Spell);

SpellSchoolInfo::SpellSchoolInfo(const TwoDARowView& tda)
{
    String temp_string;
    int temp_int;
    if (tda.get_to("Label", temp_string)) {
        tda.get_to("Letter", letter);
        tda.get_to("StringRef", name);
        if (tda.get_to("Opposition", temp_int)) {
            opposition = nw::SpellSchool::make(temp_int);
        }
        tda.get_to("Description", description);
    }
}

SpellInfo::SpellInfo(const TwoDARowView& tda)
{
    String temp_string;
    int temp_int;
    const auto& spellschools = nw::kernel::rules().spellschools;

    if (tda.get_to("label", temp_string)) {
        tda.get_to("Name", name);
        if (tda.get_to("IconResRef", temp_string)) {
            icon = {temp_string, nw::ResourceType::texture};
        }
        if (tda.get_to("School", temp_string)) {
            for (size_t i = 0; i < spellschools.entries.size(); ++i) {
                if (string::icmp(temp_string, spellschools.entries[i].letter)) {
                    school = nw::SpellSchool::make(int32_t(i));
                    break;
                }
            }
        }
        if (tda.get_to("MetaMagic", temp_int)) {
            metamagic = static_cast<SpellMetaMagic>(temp_int);
        }

        tda.get_to("Innate", innate_level);
    }
}

} // namespace nw
