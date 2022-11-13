#include "Skill.hpp"

#include "../formats/TwoDA.hpp"
#include "../kernel/Kernel.hpp"

namespace nw {

SkillInfo::SkillInfo(const TwoDARowView& tda)
{
    std::string temp_string;
    if (tda.get_to("label", temp_string)) {
        tda.get_to("Name", name);
        tda.get_to("Description", description);
        if (tda.get_to("Icon", temp_string)) {
            icon = {temp_string, nw::ResourceType::texture};
        }
        tda.get_to("Untrained", untrained);

        tda.get_to("ArmorCheckPenalty", armor_check_penalty);
        tda.get_to("AllClassesCanUse", all_can_use);
        if (tda.get_to("Constant", temp_string)) {
            constant = nw::kernel::strings().intern(temp_string);
        }
        tda.get_to("HostileSkill", hostile);
    }
}

} // namespace nw
