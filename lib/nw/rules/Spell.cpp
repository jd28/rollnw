#include "Spell.hpp"

#include "../formats/TwoDA.hpp"
#include "../kernel/Kernel.hpp"

namespace nw {

SpellInfo::SpellInfo(const TwoDARowView& tda)
{
    std::string temp_string;

    if (tda.get_to("label", temp_string)) {
        tda.get_to("Name", name);
        if (tda.get_to("IconResRef", temp_string)) {
            icon = {temp_string, nw::ResourceType::texture};
        }
    }
}

} // namespace nw
