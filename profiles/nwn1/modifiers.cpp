#include "modifiers.hpp"

#include "combat.hpp"
#include "constants.hpp"
#include "constants/const_feat.hpp"
#include "functions.hpp"

#include <nw/functions.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/log.hpp>
#include <nw/objects/Common.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/feats.hpp>
#include <nw/rules/items.hpp>

namespace nwn1 {

// == Modifiers ===============================================================
// ============================================================================

void load_modifiers()
{
    LOG_F(INFO, "[nwn1] loading modifiers...");

    auto& rules = nw::kernel::rules();

    // == Ability =============================================================
    // ========================================================================
    rules.modifiers.add(mod::ability(
        class_stat_gain,
        "nwn-ee-class-stat-gain",
        nw::ModifierSource::class_));

    rules.modifiers.add(mod::ability(
        epic_great_ability,
        "dnd-3.0-epic-great-ability",
        nw::ModifierSource::feat));

    // == Armor Class =========================================================
    // ========================================================================
    rules.modifiers.add(mod::armor_class(
        dragon_disciple_ac,
        "dnd-3.0-dragon-disciple-ac",
        nw::ModifierSource::class_));

    rules.modifiers.add(mod::armor_class(
        pale_master_ac,
        "dnd-3.0-palemaster-ac",
        nw::ModifierSource::class_));

    rules.modifiers.add(mod::armor_class(
        simple_feat_mod(feat_epic_armor_skin, 2),
        "dnd-3.0-epic-armor-skin",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::armor_class(
        training_versus_ac,
        "dnd-3.0-training-vs-ac",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::armor_class(
        ac_dodge,
        tumble_ac,
        "dnd-3.0-tumble-ac",
        nw::ModifierSource::skill));

    // == Attack Bonus ========================================================
    // ========================================================================
    rules.modifiers.add(mod::attack_bonus(
        ability_attack_bonus,
        "dnd-3.0-ability-attack-bonus",
        nw::ModifierSource::ability));

    rules.modifiers.add(mod::attack_bonus(
        weapon_feat_ab,
        "dnd-3.0-weapon-feats",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::attack_bonus(
        attack_type_onhand,
        enchant_arrow_ab,
        "dnd-3.0-enchant-arrow",
        nw::ModifierSource::class_));

    rules.modifiers.add(mod::attack_bonus(
        attack_type_any,
        simple_feat_mod(feat_epic_prowess, 1),
        "dnd-3.0-epic-prowess",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::attack_bonus(
        attack_type_any,
        favored_enemy_ab,
        "dnd-3.0-favored-enemy",
        nw::ModifierSource::class_));

    rules.modifiers.add(mod::attack_bonus(
        attack_type_any,
        combat_mode_ab,
        "dnd-3.0-combat-mode",
        nw::ModifierSource::combat_mode));

    rules.modifiers.add(mod::attack_bonus(
        attack_type_onhand,
        good_aim,
        "dnd-3.0-good-aim",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::attack_bonus(
        attack_type_any,
        target_state_ab,
        "dnd-3.0-target-state",
        nw::ModifierSource::unknown));

    rules.modifiers.add(mod::attack_bonus(
        attack_type_any,
        training_versus_ab,
        "dnd-3.0-training-vs-ab",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::attack_bonus(
        weapon_master_ab,
        "dnd-3.0-weaponmaster-ab",
        nw::ModifierSource::class_));

    // == Concealment =========================================================
    // ========================================================================
    rules.modifiers.add(mod::concealment(
        epic_self_concealment,
        "dnd-3.0-self-concealment",
        nw::ModifierSource::feat));

    // == Damage Bonus ========================================================
    // ========================================================================

    // Ability Damage
    rules.modifiers.add(mod::damage_bonus(
        ability_damage,
        "dnd-3.0-ability-dmg",
        nw::ModifierSource::ability));

    // Combat Mode
    rules.modifiers.add(mod::damage_bonus(
        attack_type_any,
        combat_mode_dmg,
        "dnd-3.0-combat-mode-dmg",
        nw::ModifierSource::combat_mode));

    // Favored Enemy
    rules.modifiers.add(mod::damage_bonus(
        attack_type_any,
        favored_enemy_dmg,
        "dnd-3.0-favored-enemy-dmg",
        nw::ModifierSource::feat));

    // Overwhelming Critical
    rules.modifiers.add(mod::damage_bonus(
        overwhelming_crit_dmg,
        "dnd-3.0-overwhelming-crit-dmg",
        nw::ModifierSource::feat));

    // == Damage Immunity =====================================================
    // ========================================================================
    rules.modifiers.add(mod::damage_immunity(
        dragon_disciple_immunity,
        "dnd-3.0-self-concealment",
        nw::ModifierSource::class_));

    // == Damage Reduction ====================================================
    // ========================================================================
    rules.modifiers.add(mod::damage_reduction(
        barbarian_dmg_reduction,
        "dnd-3.0-barbarian-reduction",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::damage_reduction(
        dwarven_defender_dmg_reduction,
        "dnd-3.0-dwarven-defender-reduction",
        nw::ModifierSource::class_));

    rules.modifiers.add(mod::damage_reduction(
        epic_dmg_reduction,
        "dnd-3.0-epic-damage-reduction",
        nw::ModifierSource::feat));

    // == Damage Resist =======================================================
    // ========================================================================
    rules.modifiers.add(mod::damage_resist(
        energy_resistance,
        "dnd-3.0-energy-resist-acid",
        nw::ModifierSource::feat));

    // == Hitpoints ===========================================================
    // ========================================================================
    rules.modifiers.add(mod::hitpoints(
        toughness,
        "dnd-3.0-toughness",
        nw::ModifierSource::feat));

    rules.modifiers.add(mod::hitpoints(
        epic_toughness,
        "dnd-3.0-epic-toughness",
        nw::ModifierSource::feat));

    // == Skills =============================================================
    // ========================================================================
    rules.modifiers.add(mod::skill(
        skill_search,
        simple_feat_mod(feat_stonecunning, 2),
        "dnd-3.0-stone-cunning",
        nw::ModifierSource::feat));

    LOG_F(INFO, "  ... {} modifiers loaded", nw::kernel::rules().modifiers.size());
}

nw::ModifierFunction simple_feat_mod(nw::Feat feat, int value)
{
    return [feat, value](const nw::ObjectBase* obj) {
        auto cre = obj->as_creature();
        if (cre && cre->stats.has_feat(feat)) {
            return value;
        }
        return 0;
    };
}

nw::ModifierResult class_stat_gain(const nw::ObjectBase* obj, int32_t subtype)
{
    auto cre = obj->as_creature();
    if (!cre) { return 0; }
    if (subtype < 0 || subtype > 5) { return 0; }
    nw::Ability abil = nw::Ability::make(subtype);
    int result = 0;
    for (const auto& cl : cre->levels.entries) {
        if (cl.id == nw::Class::invalid()) { break; }
        result += nw::kernel::rules().classes.get_stat_gain(cl.id, abil, cl.level);
    }
    return result;
}

nw::ModifierResult epic_great_ability(const nw::ObjectBase* obj, int32_t subtype)
{
    if (subtype < 0 || subtype > 5) return 0;
    nw::Ability abil = nw::Ability::make(subtype);
    nw::Feat start, stop;
    switch (*abil) {
    default:
        return 0;
    case *ability_strength:
        start = feat_epic_great_strength_1;
        stop = feat_epic_great_strength_10;
        break;
    case *ability_dexterity:
        start = feat_epic_great_dexterity_1;
        stop = feat_epic_great_dexterity_10;
        break;
    case *ability_constitution:
        start = feat_epic_great_constitution_1;
        stop = feat_epic_great_constitution_10;
        break;
    case *ability_intelligence:
        start = feat_epic_great_intelligence_1;
        stop = feat_epic_great_intelligence_10;
        break;
    case *ability_wisdom:
        start = feat_epic_great_wisdom_1;
        stop = feat_epic_great_wisdom_10;
        break;
    case *ability_charisma:
        start = feat_epic_great_charisma_1;
        stop = feat_epic_great_charisma_10;
        break;
    }

    auto nth = nw::highest_feat_in_range(obj->as_creature(), start, stop);
    if (nth == nw::Feat::invalid()) {
        return 0;
    }
    return *nth - *start + 1;
}

nw::ModifierResult dragon_disciple_ac(const nw::ObjectBase* obj)
{
    if (!obj) { return 0; }
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    auto level = cre->levels.level_by_class(nwn1::class_type_dragon_disciple);

    if (level >= 10) {
        return (level / 5) + 2;
    } else if (level >= 1 && level <= 4) {
        return 1;
    } else if (level >= 5 && level <= 7) {
        return 2;
    } else if (level >= 8) {
        return 3;
    }
    return 0;
}

nw::ModifierResult pale_master_ac(const nw::ObjectBase* obj)
{
    if (!obj) { return 0; }
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    auto pm_level = cre->levels.level_by_class(nwn1::class_type_pale_master);

    return pm_level > 0 ? ((pm_level / 4) + 1) * 2 : 0;
}

nw::ModifierResult training_versus_ac(const nw::ObjectBase* obj, const nw::ObjectBase* target)
{
    if (!obj || !obj->as_creature() || !target || !target->as_creature()) { return 0; }
    auto cre = obj->as_creature();
    auto vs = target->as_creature();

    if (vs->race == racial_type_giant
        && cre->stats.has_feat(feat_battle_training_versus_giants)) {
        return 4;
    }

    return 0;
}

nw::ModifierResult tumble_ac(const nw::ObjectBase* obj)
{
    if (!obj) { return 0; }
    auto cre = obj->as_creature();
    if (!cre) { return 0; }
    return get_skill_rank(cre, skill_tumble) / 5;
}

// Attack Bonus
nw::ModifierResult ability_attack_bonus(const nw::ObjectBase* obj, int32_t subtype)
{
    if (!obj) { return 0; }
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    auto type = nw::AttackType::make(subtype);
    if (type == nw::AttackType::invalid() || type == attack_type_any) {
        return 0;
    }
    int modifier = 0;
    auto weapon = get_weapon_by_attack_type(cre, type);
    bool is_ranged = is_ranged_weapon(weapon);
    if (is_ranged) {
        modifier = get_ability_modifier(cre, ability_dexterity);
        if (cre->stats.has_feat(feat_zen_archery)) {
            modifier = std::max(modifier, get_ability_modifier(cre, ability_wisdom));
        }
    } else {
        modifier = get_ability_modifier(cre, ability_strength);
        if (cre->stats.has_feat(feat_weapon_finesse) && weapon_is_finessable(cre, weapon)) {
            modifier = std::max(modifier, get_ability_modifier(cre, ability_dexterity));
        }
    }
    return modifier;
}

nw::ModifierResult combat_mode_ab(const nw::ObjectBase* obj)
{
    if (!obj) { return 0; }
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    switch (*cre->combat_info.combat_mode) {
    default:
        return 0;
    case *combat_mode_expertise:
        return -5;
    case *combat_mode_improved_expertise:
        return -10;
    case *combat_mode_flurry_of_blows:
        return -2;
    case *combat_mode_power_attack:
        return -5;
    case *combat_mode_improved_power_attack:
        return -10;
    case *combat_mode_rapid_shot:
        return -2;
    }
}

nw::ModifierResult enchant_arrow_ab(const nw::ObjectBase* obj, int32_t subtype)
{
    auto attack_type = nw::AttackType::make(subtype);
    int result = 0;
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    auto baseitem = nw::BaseItem::invalid();
    if (auto weapon = get_weapon_by_attack_type(cre, attack_type)) {
        baseitem = weapon->baseitem;
    }

    if (baseitem == base_item_longbow || baseitem == base_item_shortbow) {
        auto feat = nw::highest_feat_in_range(cre, feat_prestige_enchant_arrow_6,
            feat_prestige_enchant_arrow_20);
        if (feat != nw::Feat::invalid()) {
            result = (*feat - *feat_prestige_enchant_arrow_6) + 6;
        } else {
            feat = nw::highest_feat_in_range(cre, feat_prestige_enchant_arrow_1,
                feat_prestige_enchant_arrow_5);
            if (feat != nw::Feat::invalid()) {
                result = (*feat - *feat_prestige_enchant_arrow_1) + 1;
            }
        }
    }
    return result;
}

nw::ModifierResult favored_enemy_ab(const nw::ObjectBase* obj, const nw::ObjectBase* vs, int32_t subtype)
{
    if (!obj) { return 0; }
    auto cre = obj->as_creature();
    if (!cre || !vs) { return 0; }
    auto vs_cre = vs->as_creature();
    if (!vs_cre) { return 0; }

    if (nw::AttackType::make(subtype) != attack_type_any) { return 0; }
    if (cre->levels.level_by_class(class_type_ranger) == 0) { return 0; }

    if (!!nw::kernel::resolve_master_feat<int>(cre, vs_cre->race, mfeat_favored_enemy)) {
        if (cre->stats.has_feat(feat_epic_bane_of_enemies)) {
            return 2;
        }
    }
    return 0;
}

nw::ModifierResult good_aim(const nw::ObjectBase* obj, int32_t subtype)
{
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    auto attack_type = nw::AttackType::make(subtype);
    auto baseitem = nw::BaseItem::invalid();
    if (auto weapon = get_weapon_by_attack_type(cre, attack_type)) {
        baseitem = weapon->baseitem;
    }

    if (cre->race == racial_type_halfling && baseitem == base_item_sling) {
        return 1;
    }

    auto bi = nw::kernel::rules().baseitems.get(baseitem);
    if (bi && (bi->weapon_wield == 10 || bi->weapon_wield == 11)
        && cre->stats.has_feat(feat_good_aim)) {
        return 1;
    }

    return 0;
}

nw::ModifierResult target_state_ab(const nw::ObjectBase* obj, const nw::ObjectBase* target)
{
    int result = 0;
    if (!obj || !target) { return result; }
    auto cre = obj->as_creature();
    auto vs = target->as_creature();
    if (!cre || !vs) { return result; }

    if (to_bool(cre->combat_info.target_state & nw::TargetState::attacker_invis)
        || to_bool(cre->combat_info.target_state & nw::TargetState::blind)) {
        if (!vs->stats.has_feat(feat_blind_fight)) {
            result += 2;
        }
    }

    if (to_bool(cre->combat_info.target_state & nw::TargetState::stunned)) {
        result += 2;
    }

    if (to_bool(cre->combat_info.target_state & nw::TargetState::flanked)) {
        if (!vs->stats.has_feat(feat_prestige_defensive_awareness_2)) {
            result += 2;
        }
    }

    if (to_bool(cre->combat_info.target_state & nw::TargetState::unseen)
        || to_bool(cre->combat_info.target_state & nw::TargetState::invis)) {
        result -= 4;
    }

    // Others - will need to wait
    //
    // Melee only
    // Target is prone. +4AB
    // Ranged only
    // Attacker is mounted. -4AB (or -2AB with mounted archery)
    // Target is moving. -2AB
    // Target is prone. -4AB
    // Target is within 3.5 meters of the attacker. -4AB (unless negated by point blank shot)

    return result;
}

nw::ModifierResult training_versus_ab(const nw::ObjectBase* obj, const nw::ObjectBase* target)
{
    if (!obj || !obj->as_creature() || !target || !target->as_creature()) { return 0; }
    auto cre = obj->as_creature();
    auto vs = target->as_creature();
    int result = 0;

    if (vs->race == racial_type_humanoid_goblinoid
        && cre->stats.has_feat(feat_battle_training_versus_goblins)) {
        result = 1;
    } else if (vs->race == racial_type_humanoid_orc
        && cre->stats.has_feat(feat_battle_training_versus_orcs)) {
        result = 1;
    } else if (vs->race == racial_type_humanoid_reptilian
        && cre->stats.has_feat(feat_battle_training_versus_reptilians)) {
        result = 1;
    }

    return result;
}

nw::ModifierResult weapon_feat_ab(const nw::ObjectBase* obj, int32_t subtype)
{
    auto attack_type = nw::AttackType::make(subtype);
    if (attack_type == attack_type_any) { return 0; }
    auto cre = obj->as_creature();
    if (!cre) { return 0; }
    int result = 0;

    auto baseitem = nw::BaseItem::invalid();
    if (auto weapon = get_weapon_by_attack_type(cre, attack_type)) {
        baseitem = weapon->baseitem;
    }

    nw::kernel::resolve_master_feats<int>(
        cre, baseitem,
        [&result](int val) { result += val; },
        mfeat_weapon_focus, mfeat_weapon_focus_epic);
    return result;
}

nw::ModifierResult weapon_master_ab(const nw::ObjectBase* obj, int32_t subtype)
{
    auto attack_type = nw::AttackType::make(subtype);
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    auto baseitem = nw::BaseItem::invalid();
    if (auto weapon = get_weapon_by_attack_type(cre, attack_type)) {
        baseitem = weapon->baseitem;
    }

    auto wm = cre->levels.level_by_class(nwn1::class_type_weapon_master);
    if (wm < 5) { return 0; }

    bool has_feat = !!nw::kernel::resolve_master_feat<int>(cre, baseitem, mfeat_weapon_of_choice);
    if (!has_feat) { return 0; }

    int result = 1;

    if (wm >= 13) {
        result += (wm - 10) / 3;
    }

    return result;
}

// Concealment
nw::ModifierResult epic_self_concealment(const nw::ObjectBase* obj)
{
    if (!obj || !obj->as_creature()) { return 0; }
    auto cre = obj->as_creature();
    auto nth = highest_feat_in_range(cre, feat_epic_self_concealment_10, feat_epic_self_concealment_50);
    if (nth != nw::Feat::invalid()) {
        return (*nth - *feat_epic_self_concealment_10 + 1) * 10;
    }
    return 0;
}

// Damage bonus
nw::ModifierResult ability_damage(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
{
    nw::DamageRoll roll;
    roll.type = damage_type_base_weapon;
    if (!obj) { return roll; }
    auto cre = obj->as_creature();
    if (!cre) { return roll; }

    auto attack_type = nw::AttackType::make(subtype);
    if (attack_type != attack_type_any) {
        auto weapon = get_weapon_by_attack_type(cre, attack_type);
        if (weapon && is_ranged_weapon(weapon)) {
            for (const auto& ip : weapon->properties) {
                if (ip.type == *ip_mighty) {
                    roll.roll.bonus = std::min(int(ip.cost_value),
                        get_ability_modifier(cre, ability_strength));
                }
            }
        } else {
            roll.roll.bonus = get_ability_modifier(cre, ability_strength);
            if (attack_type == attack_type_offhand) {
                roll.roll.bonus = int(roll.roll.bonus * 0.5);
            } else if (weapon && get_relative_weapon_size(cre, weapon) > 0) { // I think this is right
                roll.roll.bonus = int(roll.roll.bonus * 1.5);
            }
        }
    }
    if (roll.roll.bonus < 0) { roll.roll.bonus = 0; }
    return roll;
};

nw::ModifierResult combat_mode_dmg(const nw::ObjectBase* obj)
{
    if (!obj || !obj->as_creature()) { return 0; }
    auto cre = obj->as_creature();

    nw::DamageRoll result;
    result.type = damage_type_base_weapon;

    switch (*cre->combat_info.combat_mode) {
    default:
        return result;
    case *combat_mode_power_attack:
        result.roll.bonus = 5;
        break;
    case *combat_mode_improved_power_attack:
        result.roll.bonus = 10;
        break;
    }

    return result;
}

nw::ModifierResult favored_enemy_dmg(const nw::ObjectBase* obj, const nw::ObjectBase* vs, int32_t subtype)
{
    nw::DamageRoll result;
    if (!obj) { return result; }
    auto cre = obj->as_creature();
    if (!cre || !vs) { return result; }
    auto vs_cre = vs->as_creature();
    if (!vs_cre) { return result; }

    if (nw::AttackType::make(subtype) != attack_type_any) { return result; }
    int ranger = cre->levels.level_by_class(class_type_ranger);
    if (ranger == 0) { return result; }

    if (!!nw::kernel::resolve_master_feat<int>(cre, vs_cre->race, mfeat_favored_enemy)) {
        result.type = damage_type_base_weapon;
        result.flags = nw::DamageCategory::critical;
        result.roll.bonus = 1 + (ranger / 5);
        if (cre->stats.has_feat(feat_epic_bane_of_enemies)) {
            result.roll.dice = 2;
            result.roll.sides = 6;
        }
    }
    return result;
}

nw::ModifierResult overwhelming_crit_dmg(const nw::ObjectBase* obj, const nw::ObjectBase* vs, int32_t subtype)
{
    nw::DamageRoll result;
    result.flags = nw::DamageCategory::critical;
    if (!obj || !obj->as_creature()) { return result; }
    auto cre = obj->as_creature();
    auto attack_type = nw::AttackType::make(subtype);
    if (attack_type == attack_type_any) { return result; }
    auto weapon = get_weapon_by_attack_type(cre, attack_type);
    auto base = nw::BaseItem::invalid();
    if (weapon) { base = weapon->baseitem; }
    if (!!nw::kernel::resolve_master_feat<int>(cre, base, mfeat_overwhelming_crit)) {
        auto mult = resolve_critical_multiplier(cre, attack_type, vs);

        result.roll.dice = 1;
        result.roll.sides = 6;
        if (mult > 1) {
            // Not technically how NWN does it, it might only factor base crit mult as well
            result.roll.dice = mult - 1;
        }
    }
    return result;
}

// Damage Immunity
nw::ModifierResult dragon_disciple_immunity(const nw::ObjectBase* obj, int32_t subtype)
{
    auto dmg_type = nw::Damage::make(subtype);
    if (!obj || !obj->as_creature() || dmg_type != damage_type_fire) { return 0; }
    auto cre = obj->as_creature();
    if (cre->levels.level_by_class(class_type_dragon_disciple) >= 10) {
        return 100;
    }

    return 0;
}

// Damage Reduction
nw::ModifierResult barbarian_dmg_reduction(const nw::ObjectBase* obj)
{
    if (!obj || !obj->as_creature()) { return 0; }

    auto cre = obj->as_creature();

    int barb = cre->levels.level_by_class(class_type_barbarian);
    if (barb > 10) {
        return 1 + (barb - 10) / 3;
    }

    return 0;
}

nw::ModifierResult dwarven_defender_dmg_reduction(const nw::ObjectBase* obj)
{
    if (!obj || !obj->as_creature()) { return 0; }

    auto cre = obj->as_creature();

    int dd = cre->levels.level_by_class(class_type_dwarven_defender);
    if (dd > 0) {
        return 3 + ((dd - 6) / 4 * 3);
    }

    return 0;
}

nw::ModifierResult epic_dmg_reduction(const nw::ObjectBase* obj)
{
    if (!obj || !obj->as_creature()) { return 0; }

    auto cre = obj->as_creature();
    int result = 0;

    if (cre->stats.has_feat(feat_epic_damage_reduction_9)) {
        result = 9;
    } else if (cre->stats.has_feat(feat_epic_damage_reduction_6)) {
        result = 6;
    } else if (cre->stats.has_feat(feat_epic_damage_reduction_3)) {
        result = 3;
    }

    return result;
}

// Damage Resist
nw::ModifierResult energy_resistance(const nw::ObjectBase* obj, int32_t subtype)
{
    if (!obj || !obj->as_creature()) { return {}; }
    auto cre = obj->as_creature();
    auto dmg_type = nw::Damage::make(subtype);
    nw::Feat feat_start, feat_end, resist;
    if (dmg_type == damage_type_acid) {
        resist = feat_resist_energy_acid;
        feat_start = feat_epic_energy_resistance_acid_1;
        feat_end = feat_epic_energy_resistance_acid_10;
    } else if (dmg_type == damage_type_cold) {
        resist = feat_resist_energy_cold;
        feat_start = feat_epic_energy_resistance_cold_1;
        feat_end = feat_epic_energy_resistance_cold_10;
    } else if (dmg_type == damage_type_electrical) {
        resist = feat_resist_energy_electrical;
        feat_start = feat_epic_energy_resistance_electrical_1;
        feat_end = feat_epic_energy_resistance_electrical_10;
    } else if (dmg_type == damage_type_fire) {
        resist = feat_resist_energy_fire;
        feat_start = feat_epic_energy_resistance_fire_1;
        feat_end = feat_epic_energy_resistance_fire_10;
    } else if (dmg_type == damage_type_sonic) {
        resist = feat_resist_energy_sonic;
        feat_start = feat_epic_energy_resistance_sonic_1;
        feat_end = feat_epic_energy_resistance_sonic_10;
    } else {
        return 0;
    }

    auto nth = highest_feat_in_range(cre, feat_start, feat_end);
    if (nth == nw::Feat::invalid()) {
        if (cre->stats.has_feat(resist)) {
            return 5;
        } else {
            return 0;
        }
    }

    return (*nth - *feat_start + 1) * 10;
}

// Hitpoints
nw::ModifierResult toughness(const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();
    if (!cre) {
        return 0;
    }

    if (cre->stats.has_feat(feat_toughness)) {
        return cre->levels.level();
    }

    return 0;
}

nw::ModifierResult epic_toughness(const nw::ObjectBase* obj)
{
    auto nth = highest_feat_in_range(obj->as_creature(), feat_epic_toughness_1, feat_epic_toughness_10);
    if (nth == nw::Feat::invalid()) {
        return 0;
    }
    return (*nth - *feat_epic_toughness_1 + 1) * 20;
}

}
