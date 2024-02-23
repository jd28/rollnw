#include "casting.hpp"

#include "../../kernel/Rules.hpp"
#include "functions.hpp"

namespace nwn1 {

int get_caster_level(nw::Creature* obj, nw::Class class_)
{
    auto main_class_info = nw::kernel::rules().classes.get(class_);
    if (!obj || !main_class_info || !main_class_info->spellcaster) { return 0; }

    int result = obj->levels.level_by_class(class_);

    for (const auto& cls : obj->levels.entries) {
        if (cls.id == nw::Class::invalid()) { break; }
        if (cls.id == class_) { continue; }

        auto class_info = nw::kernel::rules().classes.get(cls.id);
        if (!class_info) { continue; }

        if (main_class_info->arcane && class_info->arcane_spellgain_mod > 0) {
            result += cls.level / class_info->arcane_spellgain_mod;
        } else if (!main_class_info->arcane && class_info->divine_spellgain_mod > 0) {
            result += cls.level / class_info->divine_spellgain_mod;
        }
    }

    return result;
}

int get_spell_dc(nw::Creature* obj, nw::Class class_, nw::Spell spell)
{
    auto class_info = nw::kernel::rules().classes.get(class_);
    auto spell_info = nw::kernel::rules().spells.get(spell);
    if (!obj || !class_info || !spell_info) { return 0; }

    int result = 10 + spell_info->innate_level;
    auto adder = [&result](int value) { result += value; };

    result += get_ability_modifier(obj, class_info->caster_ability);

    nw::kernel::resolve_master_feats<int>(obj, spell_info->school, adder,
        mfeat_spell_focus, mfeat_spell_focus_greater, mfeat_spell_focus_epic);

    return result;
}

} // namespace nwn1
