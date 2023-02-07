#include "effects.hpp"

#include "combat.hpp"
#include "constants.hpp"
#include "functions.hpp"

#include <algorithm>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/Effect.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/items.hpp>

#include <limits>

namespace nwn1 {

// == Loaders =================================================================
// ============================================================================

void load_effects()
{
    LOG_F(INFO, "[nwn1] Loading effect appliers");

#define ADD_IS_CREATURE_EFFECT(type) \
    nw::kernel::effects().add(type, effect_apply_is_creature, effect_remove_is_creature)

#define ADD_IS_VALID_EFFECT(type) \
    nw::kernel::effects().add(type, effect_apply_is_valid, effect_remove_is_valid)

    ADD_IS_CREATURE_EFFECT(effect_type_ability_increase);
    ADD_IS_CREATURE_EFFECT(effect_type_ability_decrease);

    ADD_IS_CREATURE_EFFECT(effect_type_ac_increase);
    ADD_IS_CREATURE_EFFECT(effect_type_ac_decrease);

    ADD_IS_CREATURE_EFFECT(effect_type_attack_increase);
    ADD_IS_CREATURE_EFFECT(effect_type_attack_decrease);

    ADD_IS_CREATURE_EFFECT(effect_type_concealment);

    ADD_IS_CREATURE_EFFECT(effect_type_damage_increase);
    ADD_IS_CREATURE_EFFECT(effect_type_damage_decrease);

    ADD_IS_VALID_EFFECT(effect_type_damage_immunity_increase);
    ADD_IS_VALID_EFFECT(effect_type_damage_immunity_decrease);
    ADD_IS_VALID_EFFECT(effect_type_damage_reduction);
    ADD_IS_VALID_EFFECT(effect_type_damage_resistance);

    nw::kernel::effects().add(effect_type_haste, effect_haste_apply, effect_haste_remove);
    nw::kernel::effects().add(effect_type_temporary_hitpoints,
        effect_hitpoints_temp_apply, effect_hitpoints_temp_remove);

    ADD_IS_CREATURE_EFFECT(effect_type_miss_chance);

    ADD_IS_CREATURE_EFFECT(effect_type_saving_throw_increase);
    ADD_IS_CREATURE_EFFECT(effect_type_saving_throw_decrease);

    ADD_IS_CREATURE_EFFECT(effect_type_skill_increase);
    ADD_IS_CREATURE_EFFECT(effect_type_skill_decrease);

#undef ADD_IS_CREATURE_EFFECT
#undef ADD_IS_VALID_EFFECT
}

void load_itemprop_generators()
{
    LOG_F(INFO, "[nwn1] Loading item property generators");
    nw::kernel::effects().add(ip_ability_bonus, ip_gen_ability_modifier);
    nw::kernel::effects().add(ip_attack_bonus, ip_gen_attack_modifier);
    nw::kernel::effects().add(ip_attack_penalty, ip_gen_attack_modifier);
    nw::kernel::effects().add(ip_decreased_ability_score, ip_gen_ability_modifier);
    nw::kernel::effects().add(ip_damage_bonus, ip_gen_damage_bonus);
    nw::kernel::effects().add(ip_enhancement_bonus, ip_gen_enhancement_modifier);
    nw::kernel::effects().add(ip_enhancement_penalty, ip_gen_enhancement_modifier);
    nw::kernel::effects().add(ip_haste, ip_gen_haste);
    nw::kernel::effects().add(ip_saving_throw_bonus, ip_gen_save_modifier);
    nw::kernel::effects().add(ip_decreased_saving_throws, ip_gen_save_modifier);
    nw::kernel::effects().add(ip_saving_throw_bonus_specific, ip_gen_save_vs_modifier);
    nw::kernel::effects().add(ip_decreased_saving_throws_specific, ip_gen_save_vs_modifier);
    nw::kernel::effects().add(ip_skill_bonus, ip_gen_skill_modifier);
    nw::kernel::effects().add(ip_decreased_skill_modifier, ip_gen_skill_modifier);
}

// == Effect Apply/Remove =====================================================
// ============================================================================

bool effect_apply_is_creature(nw::ObjectBase* obj, const nw::Effect*)
{
    return !!obj->as_creature();
}

bool effect_remove_is_creature(nw::ObjectBase* obj, const nw::Effect*)
{
    return !!obj->as_creature();
}

bool effect_apply_is_valid(nw::ObjectBase* obj, const nw::Effect*)
{
    return !!obj;
}

bool effect_remove_is_valid(nw::ObjectBase* obj, const nw::Effect*)
{
    return !!obj;
}

bool effect_haste_apply(nw::ObjectBase* obj, const nw::Effect*)
{
    if (auto cre = obj->as_creature()) {
        if (!cre->hasted) { ++cre->combat_info.attacks_extra; }
        ++cre->hasted;
        return true;
    }
    return false;
}

bool effect_haste_remove(nw::ObjectBase* obj, const nw::Effect*)
{
    if (auto cre = obj->as_creature()) {
        if (cre->hasted) { --cre->hasted; }
        if (!cre->hasted) { --cre->combat_info.attacks_extra; }
        return true;
    }
    return false;
}

bool effect_hitpoints_temp_apply(nw::ObjectBase* obj, const nw::Effect* effect)
{
    if (!obj || !obj->as_creature()) { return false; }
    auto cre = obj->as_creature();
    cre->hp_current += int16_t(effect->get_int(0));
    cre->hp_temp += int16_t(effect->get_int(0));
    return true;
}

bool effect_hitpoints_temp_remove(nw::ObjectBase* obj, const nw::Effect* effect)
{
    if (!obj || !obj->as_creature()) { return false; }
    auto cre = obj->as_creature();
    if (effect->get_int(0) > 0) {
        cre->hp_current -= int16_t(effect->get_int(0));
        cre->hp_temp -= int16_t(effect->get_int(0));
    }

    // Death has to be considered here.. but not yet.

    return true;
}

// == Effect Creation =========================================================
// ============================================================================

nw::Effect* effect_ability_modifier(nw::Ability ability, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto type = modifier > 0 ? effect_type_ability_increase : effect_type_ability_decrease;
    int value = modifier > 0 ? modifier : -modifier;

    auto eff = nw::kernel::effects().create(type);
    eff->subtype = *ability;
    eff->set_int(0, std::abs(value));
    return eff;
}

nw::Effect* effect_armor_class_modifier(nw::ArmorClass type, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto efftype = modifier > 0 ? effect_type_ac_increase : effect_type_ac_decrease;
    int value = modifier > 0 ? modifier : -modifier;

    auto eff = nw::kernel::effects().create(efftype);
    eff->subtype = *type;
    eff->set_int(0, std::abs(value));
    return eff;
}

nw::Effect* effect_attack_modifier(nw::AttackType attack, int modifier)
{
    if (modifier == 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(effect_type_attack_increase);
    eff->type = modifier > 0 ? effect_type_attack_increase : effect_type_attack_decrease;
    eff->subtype = *attack;
    eff->set_int(0, std::abs(modifier));
    return eff;
}

nw::Effect* effect_concealment(int value, nw::MissChanceType type)
{
    if (value <= 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(effect_type_concealment);
    eff->subtype = *type;
    eff->set_int(0, value);
    return eff;
}

nw::Effect* effect_damage_bonus(nw::Damage type, nw::DiceRoll dice, nw::DamageCategory cat)
{
    if (!dice) { return nullptr; }
    auto eff = nw::kernel::effects().create(effect_type_damage_increase);
    eff->subtype = *type;
    eff->set_int(0, dice.dice);
    eff->set_int(1, dice.sides);
    eff->set_int(2, dice.bonus);
    eff->set_int(3, static_cast<int32_t>(cat));
    return eff;
}

nw::Effect* effect_damage_immunity(nw::Damage type, int value)
{
    if (value == 0) { return nullptr; }
    value = std::clamp(value, -100, 100);
    auto eff = nw::kernel::effects().create(effect_type_damage_immunity_increase);
    eff->type = value > 0 ? effect_type_damage_immunity_increase : effect_type_damage_immunity_decrease;
    eff->subtype = *type;
    eff->set_int(0, std::abs(value));
    return eff;
}

nw::Effect* effect_damage_penalty(nw::Damage type, nw::DiceRoll dice)
{
    if (!dice) { return nullptr; }
    auto eff = nw::kernel::effects().create(effect_type_damage_decrease);
    eff->subtype = *type;
    eff->set_int(0, dice.dice);
    eff->set_int(1, dice.sides);
    eff->set_int(2, dice.bonus);
    return eff;
}

nw::Effect* effect_damage_reduction(int value, int power, int max)
{
    if (value == 0 || power <= 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(effect_type_damage_reduction);
    eff->set_int(0, value);
    eff->set_int(1, power);
    eff->set_int(2, max);
    return eff;
}

nw::Effect* effect_damage_resistance(nw::Damage type, int value, int max)
{
    if (value <= 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(effect_type_damage_resistance);
    eff->subtype = *type;
    eff->set_int(0, value);
    eff->set_int(1, max);
    return eff;
}

nw::Effect* effect_haste()
{
    return nw::kernel::effects().create(effect_type_haste);
}

nw::Effect* effect_hitpoints_temporary(int amount)
{
    if (amount <= 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(effect_type_temporary_hitpoints);
    eff->set_int(0, amount);
    return eff;
}

nw::Effect* effect_miss_chance(int value, nw::MissChanceType type)
{
    if (value <= 0) { return nullptr; }
    auto eff = nw::kernel::effects().create(effect_type_miss_chance);
    eff->subtype = *type;
    eff->set_int(0, value);
    return eff;
}

nw::Effect* effect_save_modifier(nw::Save save, int modifier, nw::SaveVersus vs)
{
    if (modifier == 0) { return nullptr; }
    auto type = modifier > 0 ? effect_type_saving_throw_increase : effect_type_saving_throw_decrease;
    auto eff = nw::kernel::effects().create(type);
    eff->subtype = *save;
    eff->set_int(0, std::abs(modifier));
    eff->set_int(1, *vs);
    return eff;
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

// == Item Properties =========================================================
// ============================================================================

nw::ItemProperty itemprop_ability_modifier(nw::Ability ability, int modifier)
{
    nw::ItemProperty result;
    if (modifier == 0) { return result; }
    result.type = modifier > 0 ? *ip_ability_bonus : *ip_decreased_ability_score;
    result.subtype = uint16_t(*ability);
    result.cost_value = uint16_t(std::abs(modifier));
    return result;
}

nw::Effect* ip_gen_ability_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    auto abil = nw::Ability::make(ip.subtype);
    const auto def = nw::kernel::effects().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_ability_bonus || type == ip_decreased_ability_score) && def->cost_table) {
        // Note: value will already be negative for decreased ability score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_ability_modifier(abil, *value);
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_armor_class_modifier(int value)
{
    nw::ItemProperty result;
    if (value == 0) { return result; }
    auto type = value > 0 ? ip_ac_bonus : ip_decreased_ac;
    const auto def = nw::kernel::effects().ip_definition(type);
    if (!def || !def->cost_table) { return result; }
    value = std::clamp(value, 0, int(def->cost_table->rows()));

    result.type = uint16_t(*type);
    result.cost_value = uint16_t(value);
    result.cost_value = uint16_t(std::abs(value));
    return result;
}

nw::Effect* ip_gen_ac_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem baseitem)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    const auto def = nw::kernel::effects().ip_definition(type);
    if (!def) { return nullptr; }

    auto base = nw::kernel::rules().baseitems.get(baseitem);
    if (!base) { return nullptr; }
    auto ac_type = base->ac_type;

    if ((type == ip_ac_bonus || type == ip_decreased_ac) && def->cost_table) {
        // Note: value will already be negative for decreased ability score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_armor_class_modifier(ac_type, *value);
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_attack_modifier(int value)
{
    nw::ItemProperty result;
    if (value == 0) { return result; }
    auto type = value > 0 ? ip_attack_bonus : ip_attack_penalty;

    const auto def = nw::kernel::effects().ip_definition(type);
    if (!def || !def->cost_table) { return result; }
    value = std::clamp(value, 0, int(def->cost_table->rows()));

    result.type = uint16_t(*type);
    result.cost_value = uint16_t(std::abs(value));
    return result;
}

nw::Effect* ip_gen_attack_modifier(const nw::ItemProperty& ip, nw::EquipIndex equip, nw::BaseItem)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    const auto def = nw::kernel::effects().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_attack_bonus || type == ip_attack_penalty) && def->cost_table) {
        // Note: value will already be negative for decreased ability score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_attack_modifier(equip_index_to_attack_type(equip), *value);
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_damage_bonus(nw::Damage type, int value)
{
    nw::ItemProperty result;
    result.type = uint16_t(*ip_damage_bonus);
    result.subtype = uint16_t(*type);
    result.cost_value = uint16_t(value);
    return result;
}

nw::Effect* ip_gen_damage_bonus(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    auto dmgtype = nw::Damage::make(ip.subtype);
    const auto def = nw::kernel::effects().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_damage_bonus) && def->cost_table) {
        // Note: value will already be negative for decreased ability score.
        auto dice = def->cost_table->get<int>(ip.cost_value, "NumDice");
        auto sides = def->cost_table->get<int>(ip.cost_value, "Die");
        if (dice && sides) {
            if (*dice > 0) {
                return effect_damage_bonus(dmgtype, {*dice, *sides, 0});
            } else if (*dice == 0) {
                return effect_damage_bonus(dmgtype, {0, 0, *sides});
            }
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_enhancement_modifier(int value)
{
    nw::ItemProperty result;
    if (value == 0) { return result; }
    auto type = value > 0 ? ip_enhancement_bonus : ip_enhancement_penalty;

    const auto def = nw::kernel::effects().ip_definition(type);
    if (!def || !def->cost_table) { return result; }
    value = std::clamp(value, 0, int(def->cost_table->rows()));

    result.type = uint16_t(*type);
    result.cost_value = uint16_t(std::abs(value));
    return result;
}

nw::Effect* ip_gen_enhancement_modifier(const nw::ItemProperty& ip, nw::EquipIndex equip, nw::BaseItem)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    const auto def = nw::kernel::effects().ip_definition(type);
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

nw::Effect* ip_gen_haste(const nw::ItemProperty&, nw::EquipIndex, nw::BaseItem)
{
    return effect_haste();
}

nw::ItemProperty itemprop_keen()
{
    nw::ItemProperty result;
    result.type = *ip_keen;
    return result;
}

nw::ItemProperty itemprop_save_modifier(nw::Save type, int modifier)
{
    nw::ItemProperty result;
    if (modifier == 0) { return result; }
    result.type = modifier > 0 ? *ip_saving_throw_bonus : *ip_decreased_saving_throws;
    result.subtype = uint16_t(*type);
    result.cost_value = uint16_t(std::abs(modifier));
    return result;
}

nw::Effect* ip_gen_save_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    auto save = nw::Save::make(ip.subtype);
    const auto def = nw::kernel::effects().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_saving_throw_bonus || type == ip_decreased_saving_throws) && def->cost_table) {
        // Note: value will already be negative for decreased skill score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_save_modifier(save, *value);
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_save_vs_modifier(nw::SaveVersus type, int modifier)
{
    nw::ItemProperty result;
    if (modifier == 0) { return result; }
    result.type = modifier > 0 ? *ip_saving_throw_bonus_specific : *ip_decreased_saving_throws_specific;
    result.subtype = uint16_t(*type);
    result.cost_value = uint16_t(std::abs(modifier));
    return result;
}

nw::Effect* ip_gen_save_vs_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    auto save = nw::SaveVersus::make(ip.subtype);
    const auto def = nw::kernel::effects().ip_definition(type);
    if (!def) { return nullptr; }

    if ((type == ip_saving_throw_bonus_specific || type == ip_decreased_saving_throws_specific) && def->cost_table) {
        // Note: value will already be negative for decreased skill score.
        if (auto value = def->cost_table->get<int>(ip.cost_value, "Value")) {
            return effect_save_modifier(nw::Save::invalid(), *value, save);
        }
    }
    return nullptr;
}

nw::ItemProperty itemprop_skill_modifier(nw::Skill skill, int modifier)
{
    nw::ItemProperty result;
    if (modifier == 0) { return result; }
    result.type = modifier > 0 ? *ip_skill_bonus : *ip_decreased_skill_modifier;
    result.subtype = uint16_t(*skill);
    result.cost_value = uint16_t(std::abs(modifier));
    return result;
}

nw::Effect* ip_gen_skill_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem)
{
    auto type = nw::ItemPropertyType::make(ip.type);
    auto sk = nw::Skill::make(ip.subtype);
    const auto def = nw::kernel::effects().ip_definition(type);
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
