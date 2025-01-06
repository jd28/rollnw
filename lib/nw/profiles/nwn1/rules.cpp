#include "rules.hpp"

#include "constants.hpp"
#include "effects.hpp"
#include "functions.hpp"

#include "../../functions.hpp"
#include "../../kernel/Rules.hpp"
#include "../../objects/Creature.hpp"

namespace nwk = nw::kernel;

namespace nwn1 {

// == Combat Modes ============================================================
// ============================================================================

bool combat_mode_can_always_use(nw::CombatMode, const nw::Creature*)
{
    return true;
}

nw::ModifierResult combat_mode_expertise_mod(nw::CombatMode mode, nw::ModifierType type, const nw::Creature* obj)
{
    if (!obj) { return 0; }
    int value = 5;
    if (mode == combat_mode_improved_expertise) {
        value *= 2;
    }

    if (type == mod_type_attack_bonus) {
        return -value;
    } else if (type == mod_type_armor_class) {
        return value;
    }
    return {};
}

nw::ModifierResult combat_mode_power_attach_mod(nw::CombatMode mode, nw::ModifierType type, const nw::Creature* obj)
{
    if (!obj) { return 0; }
    int value = 5;
    if (mode == combat_mode_improved_power_attack) {
        value *= 2;
    }

    if (type == mod_type_attack_bonus) {
        return -value;
    } else if (type == mod_type_damage) {
        nw::DamageRoll dr;
        dr.type = damage_type_base_weapon;
        dr.roll.bonus = value;
        return dr;
    }
    return {};
}

nw::ModifierResult combat_mode_flurry_mod(nw::CombatMode mode, nw::ModifierType type, const nw::Creature* obj)
{
    if (!obj) { return 0; }

    if (type == mod_type_attack_bonus) {
        return -2;
    }
    return {};
}

bool combat_mode_flurry_use(nw::CombatMode, const nw::Creature* obj)
{
    auto rh = get_equipped_item(obj, nw::EquipIndex::righthand);
    auto lh = get_equipped_item(obj, nw::EquipIndex::lefthand);
    auto result = is_monk_weapon(rh) && is_monk_weapon(lh);
    // [TODO] - Some message to player.
    return result;
}

nw::ModifierResult combat_mode_rapid_shot_mod(nw::CombatMode mode, nw::ModifierType type, const nw::Creature* obj)
{
    if (!obj) { return 0; }

    if (type == mod_type_attack_bonus) {
        return -2;
    }
    return {};
}

bool combat_mode_rapid_shot_use(nw::CombatMode, const nw::Creature* obj)
{
    if (!is_ranged_weapon(get_equipped_item(obj, nw::EquipIndex::righthand))) {
        // [TODO] - Some message to player
    }
    return true;
}

void load_combat_modes()
{
    nwk::rules().register_combat_mode({combat_mode_expertise_mod},
        {combat_mode_expertise, combat_mode_improved_expertise});

    nwk::rules().register_combat_mode({combat_mode_power_attach_mod},
        {combat_mode_power_attack, combat_mode_improved_power_attack});

    nwk::rules().register_combat_mode({combat_mode_flurry_mod, combat_mode_flurry_use},
        {combat_mode_flurry_of_blows});

    nwk::rules().register_combat_mode({combat_mode_rapid_shot_mod, combat_mode_rapid_shot_use},
        {combat_mode_rapid_shot});
}

// == Master Feats ============================================================
// ============================================================================

bool load_master_feats()
{
    LOG_F(INFO, "[nwn1] Loading master feats");

    auto& mfr = nw::kernel::rules().master_feats;
    mfr.set_bonus(mfeat_skill_focus, 3);
    mfr.set_bonus(mfeat_skill_focus_epic, 10);

#define ADD_SKILL(name)                                                \
    mfr.add(skill_##name, mfeat_skill_focus, feat_skill_focus_##name); \
    mfr.add(skill_##name, mfeat_skill_focus_epic, feat_epic_skill_focus_##name)

    ADD_SKILL(animal_empathy);
    ADD_SKILL(concentration);
    ADD_SKILL(disable_trap);
    ADD_SKILL(discipline);
    ADD_SKILL(heal);
    ADD_SKILL(hide);
    ADD_SKILL(listen);
    ADD_SKILL(lore);
    ADD_SKILL(move_silently);
    ADD_SKILL(open_lock);
    ADD_SKILL(parry);
    ADD_SKILL(perform);
    ADD_SKILL(persuade);
    ADD_SKILL(pick_pocket);
    ADD_SKILL(search);
    ADD_SKILL(set_trap);
    ADD_SKILL(spellcraft);
    ADD_SKILL(spot);
    ADD_SKILL(taunt);
    ADD_SKILL(use_magic_device);
    ADD_SKILL(appraise);
    ADD_SKILL(tumble);
    ADD_SKILL(craft_trap);
    ADD_SKILL(bluff);
    ADD_SKILL(intimidate);
    ADD_SKILL(craft_armor);
    ADD_SKILL(craft_weapon);
    // ADD_SKILL(ride); -- No feats
#undef ADD_SKILL

    // Weapons
    mfr.set_bonus(mfeat_weapon_focus, 1);
    mfr.set_bonus(mfeat_weapon_focus_epic, 2);
    mfr.set_bonus(mfeat_weapon_spec, 2);
    mfr.set_bonus(mfeat_weapon_spec_epic, 4);
    // [TODO] One here is just an indicator that char has feat.. for now.  Ultimately, dice rolls
    // and damage rolls will need to be included in ModifierResult
    mfr.set_bonus(mfeat_devastating_crit, 1);
    mfr.set_bonus(mfeat_improved_crit, 1);
    mfr.set_bonus(mfeat_overwhelming_crit, 1);
    mfr.set_bonus(mfeat_weapon_of_choice, 1);

    // Special case baseitem invalid - the rest will be loaded during the baseitem loading below
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_focus, feat_weapon_focus_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_focus_epic, feat_epic_weapon_focus_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_spec, feat_weapon_specialization_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_weapon_spec_epic, feat_epic_devastating_critical_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_devastating_crit, feat_epic_devastating_critical_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_improved_crit, feat_improved_critical_unarmed);
    mfr.add(nw::BaseItem::invalid(), mfeat_overwhelming_crit, feat_epic_overwhelming_critical_unarmed);

    // Favored Enemy - Bonus is just a check for having the feat.
    mfr.set_bonus(mfeat_favored_enemy, 1);
    mfr.add(racial_type_dwarf, mfeat_favored_enemy, feat_favored_enemy_dwarf);
    mfr.add(racial_type_elf, mfeat_favored_enemy, feat_favored_enemy_elf);
    mfr.add(racial_type_gnome, mfeat_favored_enemy, feat_favored_enemy_gnome);
    mfr.add(racial_type_halfling, mfeat_favored_enemy, feat_favored_enemy_halfling);
    mfr.add(racial_type_halfelf, mfeat_favored_enemy, feat_favored_enemy_halfelf);
    mfr.add(racial_type_halforc, mfeat_favored_enemy, feat_favored_enemy_halforc);
    mfr.add(racial_type_human, mfeat_favored_enemy, feat_favored_enemy_human);
    mfr.add(racial_type_aberration, mfeat_favored_enemy, feat_favored_enemy_aberration);
    mfr.add(racial_type_animal, mfeat_favored_enemy, feat_favored_enemy_animal);
    mfr.add(racial_type_beast, mfeat_favored_enemy, feat_favored_enemy_beast);
    mfr.add(racial_type_construct, mfeat_favored_enemy, feat_favored_enemy_construct);
    mfr.add(racial_type_dragon, mfeat_favored_enemy, feat_favored_enemy_dragon);
    mfr.add(racial_type_humanoid_goblinoid, mfeat_favored_enemy, feat_favored_enemy_goblinoid);
    mfr.add(racial_type_humanoid_monstrous, mfeat_favored_enemy, feat_favored_enemy_monstrous);
    mfr.add(racial_type_humanoid_orc, mfeat_favored_enemy, feat_favored_enemy_orc);
    mfr.add(racial_type_humanoid_reptilian, mfeat_favored_enemy, feat_favored_enemy_reptilian);
    mfr.add(racial_type_elemental, mfeat_favored_enemy, feat_favored_enemy_elemental);
    mfr.add(racial_type_fey, mfeat_favored_enemy, feat_favored_enemy_fey);
    mfr.add(racial_type_giant, mfeat_favored_enemy, feat_favored_enemy_giant);
    mfr.add(racial_type_magical_beast, mfeat_favored_enemy, feat_favored_enemy_magical_beast);
    mfr.add(racial_type_outsider, mfeat_favored_enemy, feat_favored_enemy_outsider);
    mfr.add(racial_type_shapechanger, mfeat_favored_enemy, feat_favored_enemy_shapechanger);
    mfr.add(racial_type_undead, mfeat_favored_enemy, feat_favored_enemy_undead);
    mfr.add(racial_type_vermin, mfeat_favored_enemy, feat_favored_enemy_vermin);

    mfr.set_bonus(mfeat_spell_focus, 2);
    mfr.set_bonus(mfeat_spell_focus_greater, 2);
    mfr.set_bonus(mfeat_spell_focus_epic, 2);

    mfr.add(spell_school_abjuration, mfeat_spell_focus, feat_spell_focus_abjuration);
    mfr.add(spell_school_abjuration, mfeat_spell_focus_greater, feat_greater_spell_focus_abjuration);
    mfr.add(spell_school_abjuration, mfeat_spell_focus_epic, feat_epic_spell_focus_abjuration);
    mfr.add(spell_school_conjuration, mfeat_spell_focus, feat_spell_focus_conjuration);
    mfr.add(spell_school_conjuration, mfeat_spell_focus_greater, feat_greater_spell_focus_conjuration);
    mfr.add(spell_school_conjuration, mfeat_spell_focus_epic, feat_epic_spell_focus_conjuration);
    mfr.add(spell_school_divination, mfeat_spell_focus, feat_spell_focus_divination);
    mfr.add(spell_school_divination, mfeat_spell_focus_greater, feat_greater_spell_focus_divination);
    mfr.add(spell_school_divination, mfeat_spell_focus_epic, feat_epic_spell_focus_divination);
    mfr.add(spell_school_enchantment, mfeat_spell_focus, feat_spell_focus_enchantment);
    mfr.add(spell_school_enchantment, mfeat_spell_focus_greater, feat_greater_spell_focus_enchantment);
    mfr.add(spell_school_enchantment, mfeat_spell_focus_epic, feat_epic_spell_focus_enchantment);
    mfr.add(spell_school_evocation, mfeat_spell_focus, feat_spell_focus_evocation);
    mfr.add(spell_school_evocation, mfeat_spell_focus_greater, feat_greater_spell_focus_evocation);
    mfr.add(spell_school_evocation, mfeat_spell_focus_epic, feat_epic_spell_focus_evocation);
    mfr.add(spell_school_illusion, mfeat_spell_focus, feat_spell_focus_illusion);
    mfr.add(spell_school_illusion, mfeat_spell_focus_greater, feat_greater_spell_focus_illusion);
    mfr.add(spell_school_illusion, mfeat_spell_focus_epic, feat_epic_spell_focus_illusion);
    mfr.add(spell_school_necromancy, mfeat_spell_focus, feat_spell_focus_necromancy);
    mfr.add(spell_school_necromancy, mfeat_spell_focus_greater, feat_greater_spell_focus_necromancy);
    mfr.add(spell_school_necromancy, mfeat_spell_focus_epic, feat_epic_spell_focus_necromancy);
    mfr.add(spell_school_transmutation, mfeat_spell_focus, feat_spell_focus_transmutation);
    mfr.add(spell_school_transmutation, mfeat_spell_focus_greater, feat_greater_spell_focus_transmutation);
    mfr.add(spell_school_transmutation, mfeat_spell_focus_epic, feat_epic_spell_focus_transmutation);

    LOG_F(INFO, "  ... {} master feat specializations loaded", mfr.entries().size());
    return true;
}

// == Modifiers ===============================================================
// ============================================================================

nw::ModifierFunction simple_feat_mod(nw::Feat feat, int value)
{
    return [feat, value](const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t) {
        auto cre = obj->as_creature();
        if (cre && cre->stats.has_feat(feat)) {
            return value;
        }
        return 0;
    };
}

nw::ModifierResult class_stat_gain(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
{
    nw::Ability abil = nw::Ability::make(subtype);
    if (abil == nw::Ability::invalid()) { return 0; }

    auto cre = obj->as_creature();
    if (!cre) { return 0; }
    if (subtype < 0 || subtype > 5) { return 0; }
    int result = 0;
    for (const auto& cl : cre->levels.entries) {
        if (cl.id == nw::Class::invalid()) { break; }
        result += nw::kernel::rules().classes.get_stat_gain(cl.id, abil, cl.level);
    }
    return result;
}

nw::ModifierResult epic_great_ability(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
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

nw::ModifierResult dragon_disciple_ac(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
{
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

nw::ModifierResult pale_master_ac(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
{
    auto cre = obj->as_creature();
    if (!cre) { return 0; }

    auto pm_level = cre->levels.level_by_class(nwn1::class_type_pale_master);

    return pm_level > 0 ? ((pm_level / 4) + 1) * 2 : 0;
}

nw::ModifierResult training_versus_ac(const nw::ObjectBase* obj, const nw::ObjectBase* target, int32_t)
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

nw::ModifierResult tumble_ac(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
{
    auto cre = obj->as_creature();
    if (!cre) { return 0; }
    return get_skill_rank(cre, skill_tumble, nullptr, true) / 5;
}

// Attack Bonus
nw::ModifierResult ability_attack_bonus(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
{
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

nw::ModifierResult combat_mode_ab(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
{
    auto cre = obj->as_creature();
    if (!cre || cre->combat_info.combat_mode == nw::CombatMode::invalid()) { return 0; }

    auto cbs = nw::kernel::rules().combat_mode(cre->combat_info.combat_mode);
    return cbs.modifier(cre->combat_info.combat_mode, mod_type_attack_bonus, cre);
}

nw::ModifierResult enchant_arrow_ab(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
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

nw::ModifierResult good_aim(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
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

nw::ModifierResult target_state_ab(const nw::ObjectBase* obj, const nw::ObjectBase* target, int32_t)
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

nw::ModifierResult training_versus_ab(const nw::ObjectBase* obj, const nw::ObjectBase* target, int32_t)
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

nw::ModifierResult weapon_feat_ab(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
{
    auto attack_type = nw::AttackType::make(subtype);
    if (attack_type == attack_type_any || attack_type == nw::AttackType::invalid()) { return 0; }
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

nw::ModifierResult weapon_master_ab(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
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
nw::ModifierResult epic_self_concealment(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
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

nw::ModifierResult combat_mode_dmg(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
{
    auto cre = obj->as_creature();
    if (!cre || cre->combat_info.combat_mode == nw::CombatMode::invalid()) { return nw::DamageRoll{}; }

    auto cbs = nw::kernel::rules().combat_mode(cre->combat_info.combat_mode);
    return cbs.modifier(cre->combat_info.combat_mode, mod_type_damage, cre);
}

nw::ModifierResult favored_enemy_dmg(const nw::ObjectBase* obj, const nw::ObjectBase* vs, int32_t subtype)
{
    nw::DamageRoll result;
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
nw::ModifierResult dragon_disciple_immunity(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
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
nw::ModifierResult barbarian_dmg_reduction(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
{
    if (!obj || !obj->as_creature()) { return 0; }

    auto cre = obj->as_creature();

    int barb = cre->levels.level_by_class(class_type_barbarian);
    if (barb > 10) {
        return 1 + (barb - 10) / 3;
    }

    return 0;
}

nw::ModifierResult dwarven_defender_dmg_reduction(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
{
    if (!obj || !obj->as_creature()) { return 0; }

    auto cre = obj->as_creature();

    int dd = cre->levels.level_by_class(class_type_dwarven_defender);
    if (dd > 0) {
        return 3 + ((dd - 6) / 4 * 3);
    }

    return 0;
}

nw::ModifierResult epic_dmg_reduction(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
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
nw::ModifierResult energy_resistance(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t subtype)
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
nw::ModifierResult toughness(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
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

nw::ModifierResult epic_toughness(const nw::ObjectBase* obj, const nw::ObjectBase*, int32_t)
{
    auto nth = highest_feat_in_range(obj->as_creature(), feat_epic_toughness_1, feat_epic_toughness_10);
    if (nth == nw::Feat::invalid()) {
        return 0;
    }
    return (*nth - *feat_epic_toughness_1 + 1) * 20;
}

// == Modifiers ===============================================================
// ============================================================================

void load_modifiers()
{
    LOG_F(INFO, "[nwn1] loading modifiers...");

    auto& rules = nw::kernel::rules();

    // == Ability =============================================================
    // ========================================================================
    rules.modifiers.add(nw::make_modifier(mod_type_ability, class_stat_gain, "nwn-ee-class-stat-gain", nw::ModifierSource::class_));
    rules.modifiers.add(nw::make_modifier(mod_type_ability, epic_great_ability, "dnd-3.0-epic-great-ability", nw::ModifierSource::feat));

    // == Armor Class =========================================================
    // ========================================================================
    rules.modifiers.add(nw::make_modifier(mod_type_armor_class, ac_natural, dragon_disciple_ac, "dnd-3.0-dragon-disciple-ac", nw::ModifierSource::class_));
    rules.modifiers.add(nw::make_modifier(mod_type_armor_class, ac_natural, pale_master_ac, "dnd-3.0-palemaster-ac", nw::ModifierSource::class_));
    rules.modifiers.add(nw::make_modifier(mod_type_armor_class, ac_natural, simple_feat_mod(feat_epic_armor_skin, 2), "dnd-3.0-epic-armor-skin", nw::ModifierSource::feat));
    rules.modifiers.add(nw::make_modifier(mod_type_armor_class, ac_natural, training_versus_ac, "dnd-3.0-training-vs-ac", nw::ModifierSource::feat));
    rules.modifiers.add(nw::make_modifier(mod_type_armor_class, ac_dodge, tumble_ac, "dnd-3.0-tumble-ac", nw::ModifierSource::skill));

    // == Attack Bonus ========================================================
    // ========================================================================
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, ability_attack_bonus, "dnd-3.0-ability-attack-bonus", nw::ModifierSource::ability));
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, weapon_feat_ab, "dnd-3.0-weapon-feats", nw::ModifierSource::feat));
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, attack_type_onhand, enchant_arrow_ab, "dnd-3.0-enchant-arrow", nw::ModifierSource::class_));
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, attack_type_any, simple_feat_mod(feat_epic_prowess, 1), "dnd-3.0-epic-prowess", nw::ModifierSource::feat));
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, attack_type_any, favored_enemy_ab, "dnd-3.0-favored-enemy", nw::ModifierSource::class_));
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, attack_type_any, combat_mode_ab, "dnd-3.0-combat-mode", nw::ModifierSource::combat_mode));
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, attack_type_onhand, good_aim, "dnd-3.0-good-aim", nw::ModifierSource::feat));
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, attack_type_any, target_state_ab, "dnd-3.0-target-state", nw::ModifierSource::unknown));
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, attack_type_any, training_versus_ab, "dnd-3.0-training-vs-ab", nw::ModifierSource::feat));
    rules.modifiers.add(nw::make_modifier(mod_type_attack_bonus, weapon_master_ab, "dnd-3.0-weaponmaster-ab", nw::ModifierSource::class_));

    // == Concealment =========================================================
    // ========================================================================
    rules.modifiers.add(nw::make_modifier(mod_type_concealment, epic_self_concealment, "dnd-3.0-self-concealment", nw::ModifierSource::feat));

    // == Damage Bonus ========================================================
    // ========================================================================

    rules.modifiers.add(nw::make_modifier(mod_type_damage, ability_damage, "dnd-3.0-ability-dmg", nw::ModifierSource::ability));
    rules.modifiers.add(nw::make_modifier(mod_type_damage, attack_type_any, combat_mode_dmg, "dnd-3.0-combat-mode-dmg", nw::ModifierSource::combat_mode));
    rules.modifiers.add(nw::make_modifier(mod_type_damage, attack_type_any, favored_enemy_dmg, "dnd-3.0-favored-enemy-dmg", nw::ModifierSource::feat));
    rules.modifiers.add(nw::make_modifier(mod_type_damage, overwhelming_crit_dmg, "dnd-3.0-overwhelming-crit-dmg", nw::ModifierSource::feat));

    // == Damage Immunity =====================================================
    // ========================================================================
    rules.modifiers.add(nw::make_modifier(mod_type_dmg_immunity, dragon_disciple_immunity, "dnd-3.0-self-concealment", nw::ModifierSource::class_));

    // == Damage Reduction ====================================================
    // ========================================================================
    rules.modifiers.add(nw::make_modifier(mod_type_dmg_reduction, barbarian_dmg_reduction, "dnd-3.0-barbarian-reduction", nw::ModifierSource::feat));
    rules.modifiers.add(nw::make_modifier(mod_type_dmg_reduction, dwarven_defender_dmg_reduction, "dnd-3.0-dwarven-defender-reduction", nw::ModifierSource::class_));
    rules.modifiers.add(nw::make_modifier(mod_type_dmg_reduction, epic_dmg_reduction, "dnd-3.0-epic-damage-reduction", nw::ModifierSource::feat));

    // == Damage Resist =======================================================
    // ========================================================================
    rules.modifiers.add(nw::make_modifier(mod_type_dmg_resistance, energy_resistance, "dnd-3.0-energy-resist-acid", nw::ModifierSource::feat));

    // == Hitpoints ===========================================================
    // ========================================================================
    rules.modifiers.add(nw::make_modifier(mod_type_hitpoints, toughness, "dnd-3.0-toughness", nw::ModifierSource::feat));
    rules.modifiers.add(nw::make_modifier(mod_type_hitpoints, epic_toughness, "dnd-3.0-epic-toughness", nw::ModifierSource::feat));

    // == Skills =============================================================
    // ========================================================================
    // This is way wrong, but ok for now.
    rules.modifiers.add(nw::make_modifier(mod_type_skill, skill_search, simple_feat_mod(feat_stonecunning, 2), "dnd-3.0-stone-cunning", nw::ModifierSource::feat));

    LOG_F(INFO, "  ... {} modifiers loaded", nw::kernel::rules().modifiers.size());
}

// == Qualifiers ==============================================================
// ============================================================================

bool qualify_ability(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    if (!qual.subtype.is<int32_t>()) {
        LOG_F(ERROR, "qualifier - ability: invalid subtype");
        return {};
    }
    auto val = get_ability_score(obj->as_creature(), nw::Ability::make(qual.subtype.as<int32_t>()));
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();

    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

bool qualify_alignment(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    if (!qual.subtype.is<int32_t>()) {
        LOG_F(ERROR, "qualifier - alignment: invalid subtype");
        return false;
    }

    auto cre = obj->as_creature();
    if (!cre) { return false; }

    auto target_axis = static_cast<nw::AlignmentAxis>(qual.subtype.as<int32_t>());
    auto flags = static_cast<nw::AlignmentFlags>(qual.params[0].as<int32_t>());
    auto ge = cre->good_evil;
    auto lc = cre->lawful_chaotic;

    if (!!(flags & nw::AlignmentFlags::good) && !!(target_axis | nw::AlignmentAxis::good_evil)) {
        if (ge > 50) {
            return true;
        }
    }

    if (!!(flags & nw::AlignmentFlags::evil) && !!(target_axis | nw::AlignmentAxis::good_evil)) {
        if (ge < 50) {
            return true;
        }
    }

    if (!!(flags & nw::AlignmentFlags::lawful) && !!(target_axis | nw::AlignmentAxis::law_chaos)) {
        if (lc > 50) {
            return true;
        }
    }

    if (!!(flags & nw::AlignmentFlags::chaotic) && !!(target_axis | nw::AlignmentAxis::law_chaos)) {
        if (lc < 50) {
            return true;
        }
    }

    if (!!(flags & nw::AlignmentFlags::neutral)) {
        if (target_axis == nw::AlignmentAxis::both) {
            return ge == 50 && lc == 50;
        }
        if (target_axis == nw::AlignmentAxis::good_evil) {
            return ge == 50;
        }
        if (target_axis == nw::AlignmentAxis::law_chaos) {
            return lc == 50;
        }
    }

    return false;
}

bool qualify_bab(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    auto val = base_attack_bonus(obj->as_creature());
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();

    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

bool qualify_feat(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();
    return cre && cre->stats.has_feat(nw::Feat::make(qual.subtype.as<int32_t>()));
}

bool qualify_class_level(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    if (!qual.subtype.is<int32_t>()) {
        LOG_F(ERROR, "qualifier - ability: invalid subtype");
        return false;
    }

    auto cre = obj->as_creature();
    if (!cre) { return false; }
    auto val = cre->levels.level_by_class(nw::Class::make(qual.subtype.as<int32_t>()));
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();

    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

bool qualify_level(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();
    if (!cre) { return false; }
    auto val = cre->levels.level();
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();

    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

bool qualify_race(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    auto cre = obj->as_creature();
    return cre && *cre->race == qual.params[0].as<int32_t>();
}

bool qualify_skill(const nw::Qualifier& qual, const nw::ObjectBase* obj)
{
    if (!qual.subtype.is<int32_t>()) {
        LOG_F(ERROR, "qualifier - skill: invalid subtype");
        return {};
    }
    auto val = get_skill_rank(obj->as_creature(), nw::Skill::make(qual.subtype.as<int32_t>()));
    auto min = qual.params[0].as<int32_t>();
    auto max = qual.params[1].as<int32_t>();
    if (val < min) { return false; }
    if (max != 0 && val > max) { return false; }
    return true;
}

void load_qualifiers()
{
    nw::kernel::rules().set_qualifier(nw::req_type_ability, qualify_ability);
    nw::kernel::rules().set_qualifier(nw::req_type_alignment, qualify_alignment);
    nw::kernel::rules().set_qualifier(nw::req_type_bab, qualify_bab);
    nw::kernel::rules().set_qualifier(nw::req_type_class_level, qualify_class_level);
    nw::kernel::rules().set_qualifier(nw::req_type_feat, qualify_feat);
    nw::kernel::rules().set_qualifier(nw::req_type_level, qualify_level);
    nw::kernel::rules().set_qualifier(nw::req_type_race, qualify_race);
    nw::kernel::rules().set_qualifier(nw::req_type_skill, qualify_skill);
}

// == Special Attacks =========================================================
// ============================================================================

nw::ModifierResult smite_modifier(nw::SpecialAttack type, nw::ModifierType modtype, nw::Creature* attacker, const nw::ObjectBase* target)
{
    int level = 0;

    if (modtype == mod_type_damage) {
        nw::DamageRoll result;
        auto feat = nw::highest_feat_in_range(attacker, feat_epic_great_smiting_1, feat_epic_great_smiting_10);
        int bonus = 1;

        if (feat != nw::Feat::invalid()) {
            bonus = 1 + *feat - *feat_epic_great_smiting_1 + 1;
        }

        if (type == special_attack_smite_good) {
            result.type = damage_type_positive;
            level = attacker->levels.level_by_class(class_type_blackguard);
        } else {
            result.type = damage_type_divine;
            level = attacker->levels.level_by_class(class_type_paladin)
                + attacker->levels.level_by_class(class_type_divinechampion);
        }

        result.roll.bonus = level * bonus;
        return result;
    } else if (modtype == mod_type_attack_bonus) {
        if (type == special_attack_smite_good) {
            level = attacker->levels.level_by_class(class_type_blackguard);
        } else {
            level = attacker->levels.level_by_class(class_type_paladin)
                + attacker->levels.level_by_class(class_type_divinechampion);
        }
        return level > 0 ? get_ability_score(attacker, ability_charisma) : 0;
    }

    return {};
}

bool smite_use(nw::SpecialAttack type, nw::Creature* attacker, const nw::ObjectBase* target)
{
    nw::Versus vs = target->versus_me();
    if (type == special_attack_smite_good) {
        return to_bool(vs.align_flags & nw::AlignmentFlags::good);
    } else {
        return to_bool(vs.align_flags & nw::AlignmentFlags::evil);
    }
}

void load_special_attacks()
{
    nwk::rules().register_special_attack(special_attack_smite_evil, {smite_modifier, smite_use, {}});
    nwk::rules().register_special_attack(special_attack_smite_good, {smite_modifier, smite_use, {}});
}

} // namespace nwn1
