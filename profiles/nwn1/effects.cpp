#include "effects.hpp"

#include "constants.hpp"
#include "functions.hpp"

#include <algorithm>
#include <nw/components/Creature.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/items.hpp>

#include <limits>

namespace nwn1 {

bool effect_apply_is_creature(nw::ObjectBase* obj, const nw::Effect*)
{
    return !!obj->as_creature();
}

bool effect_remove_is_creature(nw::ObjectBase* obj, const nw::Effect*)
{
    return !!obj->as_creature();
}

nw::Effect* effect_ability_modifier(nw::Ability ability, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto type = modifier > 0 ? effect_type_ability_increase : effect_type_ability_decrease;
    int value = modifier > 0 ? modifier : -modifier;

    auto eff = nw::kernel::effects().create(type);
    eff->subtype = *ability;
    eff->set_int(0, value);
    return eff;
}

nw::Effect* effect_attack_modifier(nw::AttackType attack, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(effect_type_haste);
    eff->type = modifier > 0 ? effect_type_attack_increase : effect_type_attack_decrease;
    eff->subtype = *attack;
    eff->set_int(0, modifier);
    return eff;
}

nw::Effect* effect_haste()
{
    return nw::kernel::effects().create(effect_type_haste);
}

nw::Effect* effect_skill_modifier(nw::Skill skill, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto type = modifier > 0 ? effect_type_skill_increase : effect_type_skill_decrease;
    int value = modifier > 0 ? modifier : -modifier;

    auto eff = nw::kernel::effects().create(type);
    eff->subtype = *skill;
    eff->set_int(0, value);
    return eff;
}

bool effect_haste_apply(nw::ObjectBase* obj, const nw::Effect*)
{
    if (auto cre = obj->as_creature()) {
        ++cre->hasted;
        return true;
    }
    return false;
}

bool effect_haste_remove(nw::ObjectBase* obj, const nw::Effect*)
{
    if (auto cre = obj->as_creature()) {
        if (cre->hasted) { --cre->hasted; }
        return true;
    }
    return false;
}

// -- Item Property Generators ------------------------------------------------
// ----------------------------------------------------------------------------

nw::ItemProperty itemprop_ability_modifier(nw::Ability ability, int modifier)
{
    nw::ItemProperty result;
    if (modifier == 0) { return result; }
    result.type = modifier > 0 ? *ip_ability_bonus : *ip_decreased_ability_score;
    result.subtype = uint16_t(*ability);
    result.cost_value = uint8_t(modifier);
    return result;
}

nw::Effect* ip_gen_ability_modifier(const nw::ItemProperty& ip, nw::EquipIndex)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    auto abil = nw::Ability::make(ip.subtype);
    const auto def = nw::kernel::rules().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_ability_bonus || type == ip_decreased_ability_score) && def->cost_table) {
        // Note: value will already be negative for decreased ability score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_ability_modifier(abil, *value);
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_attack_modifier(int value)
{
    nw::ItemProperty result;
    if (value == 0) { return result; }
    auto type = value > 0 ? ip_attack_bonus : ip_attack_penalty;

    const auto def = nw::kernel::rules().ip_definition(type);
    if (!def || !def->cost_table) { return result; }
    value = std::clamp(value, 0, int(def->cost_table->rows()));

    result.type = uint16_t(*type);
    result.cost_value = uint16_t(value);
    return result;
}

nw::Effect* ip_gen_attack_modifier(const nw::ItemProperty& ip, nw::EquipIndex equip)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    const auto def = nw::kernel::rules().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_attack_bonus || type == ip_attack_penalty) && def->cost_table) {
        // Note: value will already be negative for decreased ability score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_attack_modifier(equip_index_to_attack_type(equip), *value);
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_enhancement_modifier(int value)
{
    nw::ItemProperty result;
    if (value == 0) { return result; }
    auto type = value > 0 ? ip_enhancement_bonus : ip_enhancement_penalty;

    const auto def = nw::kernel::rules().ip_definition(type);
    if (!def || !def->cost_table) { return result; }
    value = std::clamp(value, 0, int(def->cost_table->rows()));

    result.type = uint16_t(*type);
    result.cost_value = uint16_t(value);
    return result;
}

nw::Effect* ip_gen_enhancement_modifier(const nw::ItemProperty& ip, nw::EquipIndex equip)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    const auto def = nw::kernel::rules().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_enhancement_bonus || type == ip_enhancement_penalty) && def->cost_table) {
        // Note: value will already be negative for decreased ability score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_attack_modifier(equip_index_to_attack_type(equip), *value);
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_haste()
{
    nw::ItemProperty result;
    result.type = *ip_haste;
    return result;
}

nw::Effect* ip_gen_haste(const nw::ItemProperty&, nw::EquipIndex)
{
    return effect_haste();
}

nw::ItemProperty itemprop_skill_modifier(nw::Skill skill, int modifier)
{
    nw::ItemProperty result;
    if (modifier == 0) { return result; }
    result.type = modifier > 0 ? *ip_skill_bonus : *ip_decreased_skill_modifier;
    result.subtype = uint16_t(*skill);
    result.cost_value = uint8_t(modifier);
    return result;
}

nw::Effect* ip_gen_skill_modifier(const nw::ItemProperty& ip, nw::EquipIndex)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    auto sk = nw::Skill::make(ip.subtype);
    const auto def = nw::kernel::rules().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_skill_bonus || type == ip_decreased_skill_modifier) && def->cost_table) {
        // Note: value will already be negative for decreased skill score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_skill_modifier(sk, *value);
        }
    }
    return nullptr;
}

} // namespace nwn1
