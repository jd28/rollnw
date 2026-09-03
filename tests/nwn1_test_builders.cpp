#include "nwn1_test_builders.hpp"

#include "nw/kernel/Kernel.hpp"
#include "nw/kernel/Rules.hpp"
#include "nw/rules/effects.hpp"
#include "nw/rules/items.hpp"

#include <algorithm>
#include <cmath>

namespace nwn1 {

nw::Effect* effect_ability_modifier(nw::Ability ability, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto type = modifier > 0 ? nw::EffectType::make(36) : nw::EffectType::make(37);
    int value = modifier > 0 ? modifier : -modifier;

    auto eff = nw::kernel::effects().create(type);
    eff->handle().subtype = *ability;
    eff->set_int(0, std::abs(value));
    return eff;
}

nw::Effect* effect_armor_class_modifier(nw::ArmorClass type, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto efftype = modifier > 0 ? nw::EffectType::make(48) : nw::EffectType::make(49);
    int value = modifier > 0 ? modifier : -modifier;

    auto eff = nw::kernel::effects().create(efftype);
    eff->handle().subtype = *type;
    eff->set_int(0, std::abs(value));
    return eff;
}

nw::Effect* effect_attack_modifier(nw::AttackType attack, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(nw::EffectType::make(10));
    eff->handle().type = modifier > 0 ? nw::EffectType::make(10) : nw::EffectType::make(11);
    eff->handle().subtype = *attack;
    eff->set_int(0, std::abs(modifier));
    return eff;
}

nw::Effect* effect_bonus_spell_slot(nw::Class class_, int spell_level)
{
    if (spell_level < 0 || static_cast<size_t>(spell_level) >= nw::kernel::rules().maximum_spell_levels()) {
        return nullptr;
    }

    auto eff = nw::kernel::effects().create(nw::EffectType::make(78));
    eff->handle().subtype = *class_;
    eff->set_int(0, spell_level);
    return eff;
}

nw::Effect* effect_damage_immunity(nw::Damage type, int value)
{
    if (value == 0) { return nullptr; }
    value = std::clamp(value, -100, 100);
    auto eff = nw::kernel::effects().create(nw::EffectType::make(16));
    eff->handle().type = value > 0 ? nw::EffectType::make(16) : nw::EffectType::make(17);
    eff->handle().subtype = *type;
    eff->set_int(0, std::abs(value));
    return eff;
}

nw::Effect* effect_damage_reduction(int value, int power, int max)
{
    if (value == 0 || power <= 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(nw::EffectType::make(12));
    eff->set_int(0, value);
    eff->set_int(1, power);
    eff->set_int(2, max);
    return eff;
}

nw::Effect* effect_damage_resistance(nw::Damage type, int value, int max)
{
    if (value <= 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(nw::EffectType::make(2));
    eff->handle().subtype = *type;
    eff->set_int(0, value);
    eff->set_int(1, max);
    return eff;
}

nw::Effect* effect_haste()
{
    return nw::kernel::effects().create(nw::EffectType::make(1));
}

nw::Effect* effect_hitpoints_temporary(int amount)
{
    if (amount <= 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(nw::EffectType::make(15));
    eff->set_int(0, amount);
    return eff;
}

nw::Effect* effect_save_modifier(nw::Save save, int modifier, nw::SaveVersus vs)
{
    if (modifier == 0) { return nullptr; }
    auto type = modifier > 0 ? nw::EffectType::make(26) : nw::EffectType::make(27);
    auto eff = nw::kernel::effects().create(type);
    eff->handle().subtype = *save;
    eff->set_int(0, std::abs(modifier));
    eff->set_int(1, *vs);
    return eff;
}

nw::Effect* effect_skill_modifier(nw::Skill skill, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto type = modifier > 0 ? nw::EffectType::make(55) : nw::EffectType::make(56);
    int value = modifier > 0 ? modifier : -modifier;

    auto eff = nw::kernel::effects().create(type);
    eff->handle().subtype = *skill;
    eff->set_int(0, value);
    return eff;
}

nw::ItemProperty itemprop_ability_modifier(nw::Ability ability, int modifier)
{
    nw::ItemProperty result;
    if (modifier == 0) { return result; }
    result.type = uint16_t(modifier > 0 ? *nw::ItemPropertyType::make(0) : *nw::ItemPropertyType::make(27));
    result.subtype = uint16_t(*ability);
    result.cost_value = uint16_t(std::abs(modifier));
    return result;
}

nw::ItemProperty itemprop_haste()
{
    nw::ItemProperty result;
    result.type = uint16_t(*nw::ItemPropertyType::make(35));
    return result;
}

nw::ItemProperty itemprop_keen()
{
    nw::ItemProperty result;
    result.type = uint16_t(*nw::ItemPropertyType::make(43));
    return result;
}

} // namespace nwn1
