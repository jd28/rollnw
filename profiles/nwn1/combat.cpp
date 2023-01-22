#include "combat.hpp"

#include "functions.hpp"

#include <nw/functions.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Equips.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/LevelStats.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/items.hpp>
#include <nw/rules/system.hpp>
#include <nw/util/macros.hpp>

namespace nwn1 {

int base_attack_bonus(const nw::Creature* obj)
{
    if (!obj) { return 0; }

    size_t result = 0;
    const auto& classes = nw::kernel::rules().classes;

    size_t levels = obj->levels.level();
    size_t epic = 0;
    if (levels >= 20) {
        // Not sure what account the game takes of the attack bonus tables when epic.
        // pretty sure it's none.
        epic = (levels - 20) / 2;
        levels = 20;
    }

    std::array<int, nw::LevelStats::max_classes> class_levels{};

    if (obj->pc) {
        for (size_t i = 0; i < levels; ++i) {
            ++class_levels[obj->levels.position(obj->history.entries[i].class_)];
        }

        for (size_t i = 0; i < nw::LevelStats::max_classes; ++i) {
            if (class_levels[i] == 0) { break; }
            auto cl = obj->levels.entries[i].id;
            result += classes.get_base_attack_bonus(cl, class_levels[i]);
        }
    } else {
        for (const auto& cl : obj->levels.entries) {
            if (levels == 0 || cl.id == nw::Class::invalid()) { break; }
            auto count = std::min(levels, size_t(cl.level));
            result += classes.get_base_attack_bonus(cl.id, count);
            levels -= count;
        }
    }
    return int(result + epic);
}

bool is_flanked(const nw::Creature* target, const nw::Creature* attacker)
{
    if (!target || !attacker) { return false; }
    if (target->combat_info.target == attacker) { return false; }
    if (attacker->combat_info.target_distance_sq > 10.0f * 10.0f) { return false; }
    if (target->stats.has_feat(feat_prestige_defensive_awareness_2)) { return false; }
    return true;
}

std::unique_ptr<nw::AttackData> resolve_attack(nw::Creature* attacker, nw::ObjectBase* target)
{
    if (!attacker || !target) { return {}; }
    auto target_cre = target->as_creature();

    // Every attack reset/update how many onhand and offhand attacks
    // the attacker has.
    auto [onhand, offhand] = resolve_number_of_attacks(attacker);
    attacker->combat_info.attacks_onhand = onhand;
    attacker->combat_info.attacks_offhand = offhand;

    int total_attacks = attacker->combat_info.attacks_onhand
        + attacker->combat_info.attacks_offhand
        + attacker->combat_info.attacks_extra;

    if (attacker->combat_info.attack_current >= total_attacks) {
        attacker->combat_info.attack_current = 0;
    }

    std::unique_ptr<nw::AttackData> data = std::make_unique<nw::AttackData>();

    data->attacker = attacker;
    data->target = target;
    data->type = resolve_attack_type(attacker);
    data->weapon = get_weapon_by_attack_type(attacker, data->type);
    data->target_state = resolve_target_state(attacker, target);
    data->target_is_creature = !!target_cre;
    data->is_ranged_attack = is_ranged_weapon(get_weapon_by_attack_type(attacker, data->type));
    data->nth_attack = attacker->combat_info.attack_current;
    data->result = resolve_attack_roll(attacker, data->type, target);

    if (nw::is_attack_type_hit(data->result)) {
        // Parry
        if (data->result != nw::AttackResult::hit_by_auto_success
            && target_cre
            && target_cre->combat_info.combat_mode == combat_mode_parry) {
            if (resolve_skill_check(target_cre, skill_parry, data->attack_roll + data->attack_bonus)) {
                // Do something .. but only the number of attacks per round target has
            }
        }

        // Deflect Arrows
        // - Not be flat footed
        // - No ranged weapons
        // - Have a free hand. [TODO] Two-sided, two-handed]
        // - Have a feat Deflect Arrows
        // - Successful reflex save vs 20.
        if (data->is_ranged_attack) {
            auto item = get_equipped_item(target_cre, nw::EquipIndex::righthand);
            if (!to_bool(data->target_state & nw::TargetState::flatfooted)
                && !is_ranged_weapon(item)
                && (!item || !get_equipped_item(target_cre, nw::EquipIndex::lefthand))
                && target_cre->stats.has_feat(feat_deflect_arrows)) {
                if (resolve_saving_throw(target_cre, saving_throw_reflex, 20,
                        nw::SaveVersus::invalid(), attacker)) {
                    // Do something .. but only once per round
                }
            }
        }

        // Check to make sure the hit hasn't be negated
        if (nw::is_attack_type_hit(data->result)) {
            if (data->result == nw::AttackResult::hit_by_critical) {
                data->multiplier = resolve_critical_multiplier(attacker, data->type);
            } else {
                data->multiplier = 1;
            }

            // Resolve Damage
            data->damage_total = resolve_attack_damage(attacker, target, data.get());

            // Epic Dodge
        }
    }

    ++attacker->combat_info.attack_current;

    return data;
}

int resolve_attack_bonus(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* versus)
{
    int result = 0;
    if (!obj) { return result; }

    nw::Versus vs;
    if (versus) {
        vs = versus->versus_me();
    }

    auto weapon = get_weapon_by_attack_type(obj, type);
    auto baseitem = nw::BaseItem::invalid();
    if (!weapon) {
        // If there is no weapon, proceed as an unarmed attack.
        type = attack_type_unarmed;
    } else {
        baseitem = weapon->baseitem;
    }

    // BAB
    result = base_attack_bonus(obj);

    // Resolve dual wield penalty
    auto [on, off] = resolve_dual_wield_penalty(obj);
    if (type == attack_type_onhand) {
        result += on;
    } else if (type == attack_type_offhand) {
        result += off;
    }

    // Size
    int modifier = obj->combat_info.size_ab_modifier;

    // Modifiers tied to a specific item/attack type
    // - abilities will be handled by the first modifier search.
    // - Weapon feats, weapon master stuff.
    auto mod_adder = [&modifier](int value) { modifier += value; };
    nw::kernel::resolve_modifier(obj, mod_type_attack_bonus, type, versus, mod_adder);
    if (type != attack_type_any) {
        // General modifiers applicable to any attack i.e. Epic Prowess, combat modes
        nw::kernel::resolve_modifier(obj, mod_type_attack_bonus, attack_type_any, versus, mod_adder);
    }

    // Effects attack increase/decrease is a little more complicated due to needing to support
    // an 'any' subtype.
    auto begin = std::begin(obj->effects());
    auto end = std::end(obj->effects());

    int bonus = 0;
    auto bonus_adder = [&bonus](int mod) { bonus += mod; };

    auto it = nw::resolve_effects_of<int>(begin, end, effect_type_attack_increase, *attack_type_any, vs,
        bonus_adder, &nw::effect_extract_int0);

    if (type != attack_type_any) {
        it = nw::resolve_effects_of<int>(it, end, effect_type_attack_increase, *type, vs,
            bonus_adder, &nw::effect_extract_int0);
    }

    int decrease = 0;
    auto decrease_adder = [&decrease](int mod) { decrease += mod; };

    it = nw::resolve_effects_of<int>(it, end, effect_type_attack_decrease, *attack_type_any, vs,
        decrease_adder, &nw::effect_extract_int0);

    if (type != attack_type_any) {
        nw::resolve_effects_of<int>(it, end, effect_type_attack_decrease, *type, vs,
            decrease_adder, &nw::effect_extract_int0);
    }

    auto [min, max] = nw::kernel::effects().effect_limits_attack();
    return result + modifier + std::clamp(bonus - decrease, min, max);
}

int resolve_attack_damage(const nw::Creature* obj, const nw::ObjectBase* versus, nw::AttackData* data)
{
    if (!obj || !versus || !data) { return 0; }
    int result = 0;

    nw::Versus vs;
    if (versus) {
        vs = versus->versus_me();
    }

    // Resolve base weapon damage
    nw::DiceRoll base_dice;
    if (is_unarmed_weapon(data->weapon)) {
        base_dice = resolve_unarmed_damage(obj);
    } else if (is_creature_weapon(data->weapon)) {
        base_dice = resolve_creature_damage(obj, data->weapon);
    } else {
        base_dice = resolve_weapon_damage(obj, data->weapon->baseitem);
    }

    data->damage_base.amount = nw::roll_dice(base_dice, data->multiplier);

    // Resolve damage from effects
    auto it = nw::find_first_effect_of(std::begin(obj->effects()), std::end(obj->effects()),
        effect_type_damage_increase);
    for (; it != std::end(obj->effects()) && it->type == effect_type_damage_increase; ++it) {
        if (it->effect->versus().match(vs)) { continue; }

        auto dmg = nw::Damage::make(it->subtype);
        auto dice = nw::DiceRoll{it->effect->get_int(0), it->effect->get_int(1), it->effect->get_int(2)};

        auto cat = static_cast<nw::DamageCategory>(it->effect->get_int(3));
        auto unblock = to_bool(nw::DamageCategory::unblockable & cat);

        if (data->result != nw::AttackResult::hit_by_critical && to_bool(nw::DamageCategory::critical & cat)) {
            continue;
        }

        auto amt = nw::roll_dice(dice, data->multiplier);
        if (to_bool(nw::DamageCategory::penalty & cat)) {
            amt = -amt;
        }

        data->add(dmg, amt, unblock);
    }

    // Resolve damage modifiers
    auto dmg_cb = [data](nw::DamageRoll roll) {
        if (roll.type == nw::Damage::invalid()) { return; }
        if (to_bool(nw::DamageCategory::critical & roll.flags) && data->multiplier <= 1) { return; }

        auto unblock = to_bool(nw::DamageCategory::unblockable & roll.flags);
        auto amt = nw::roll_dice(roll.roll, data->multiplier);
        if (to_bool(nw::DamageCategory::penalty & roll.flags)) {
            amt = -amt;
        }
        data->add(roll.type, amt, unblock);
    };

    // Damage modifiers applicable to any attack, i.e. favored enemy, combat modes, etc.
    nw::kernel::resolve_modifier(obj, mod_type_damage, attack_type_any, versus, dmg_cb);
    // Damage modifiers applicable to a specific weapon/attack type.  i.e. Strength/ability damage.
    nw::kernel::resolve_modifier(obj, mod_type_damage, data->type, versus, dmg_cb);
    // [TODO] Special Attacks

    // Massive Criticals - I don't think this is multiplied..?
    if (data->weapon && data->result == nw::AttackResult::hit_by_critical) {
        for (const auto& ip : data->weapon->properties) {
            if (ip.type == *ip_massive_criticals) {
                auto def = nw::kernel::effects().ip_definition(ip_massive_criticals);
                if (def) {
                    auto dice = def->cost_table->get<int>(ip.cost_value, "NumDice");
                    auto sides = def->cost_table->get<int>(ip.cost_value, "Die");
                    if (dice && sides) {
                        if (*dice > 0) {
                            data->add(damage_type_base_weapon, nw::roll_dice({*dice, *sides, 0}));
                        } else if (*dice == 0) {
                            data->add(damage_type_base_weapon, nw::roll_dice({0, 0, *sides}));
                        }
                    }
                }
                break;
            }
        }
    }

    // Compact all physical damages to base item type.
    for (auto& dmg : data->damages()) {
        if (dmg.type == damage_type_base_weapon
            || dmg.type == damage_type_bludgeoning
            || dmg.type == damage_type_piercing
            || dmg.type == damage_type_slashing) {
            data->damage_base.unblocked += dmg.unblocked;
            data->damage_base.amount += dmg.amount;
            dmg.unblocked = 0;
            dmg.amount = 0;
        }
    }

    // Resolve resist, reduction, immunity
    resolve_damage_modifiers(obj, versus, data);

    for (const auto& dmg : data->damages()) {
        result += dmg.amount + dmg.unblocked;
    }

    return result + data->damage_base.amount + data->damage_base.unblocked;
}

nw::AttackResult resolve_attack_roll(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* vs, nw::AttackData* data)
{
    static constexpr nw::DiceRoll d20{1, 20, 0};
    const auto roll = nw::roll_dice(d20);
    if (roll == 1) { return nw::AttackResult::miss_by_auto_fail; }

    auto attack_result = nw::AttackResult::miss_by_roll;

    const auto ab = resolve_attack_bonus(obj, type, vs);
    const auto ac = calculate_ac_versus(obj, vs, false);
    const auto iter = resolve_iteration_penalty(obj, type);

    if (data) {
        data->attack_bonus = ab;
        data->armor_class = ac;
        data->iteration_penalty = iter;
    }

    if (roll == 20) {
        attack_result = nw::AttackResult::hit_by_auto_success;
    } else {
        if (ab + roll - iter >= ac) {
            attack_result = nw::AttackResult::hit_by_roll;
        }
    }

    if (nw::is_attack_type_hit(attack_result)) {
        int crit_threat = resolve_critical_threat(obj, type);
        if (data) { data->threat_range = crit_threat; }
        if (21 - roll <= crit_threat && ab + nw::roll_dice(d20) - iter >= ac) {
            attack_result = nw::AttackResult::hit_by_critical;
        }

        auto [conceal, source] = resolve_concealment(obj, vs);
        if (conceal > 0) {
            if (data) { data->concealment = conceal; }

            nw::DiceRoll d100{1, 100};
            auto conceal_check = nw::roll_dice(d100);
            if (conceal_check <= conceal) {
                if (obj->stats.has_feat(feat_blind_fight)) {
                    conceal_check = nw::roll_dice(d100);
                    if (conceal_check <= conceal) {
                        attack_result = source ? nw::AttackResult::miss_by_miss_chance
                                               : nw::AttackResult::miss_by_concealment;
                    }
                } else {
                    attack_result = source ? nw::AttackResult::miss_by_miss_chance
                                           : nw::AttackResult::miss_by_concealment;
                }
            }
        }
    }

    return attack_result;
}

nw::AttackType resolve_attack_type(const nw::Creature* obj)
{
    static constexpr nw::DiceRoll d3{1, 3};
    nw::AttackType result = nw::AttackType::invalid();
    if (obj->combat_info.attack_current < obj->combat_info.attacks_onhand + obj->combat_info.attacks_extra) {
        auto weapon = get_weapon_by_attack_type(obj, attack_type_onhand);
        if (weapon) {
            result = attack_type_onhand;
        } else {
            result = attack_type_unarmed;
        }

        if (result == attack_type_unarmed) {
            weapon = get_weapon_by_attack_type(obj, attack_type_unarmed);
            if (!weapon) {
                // look for creature attacks, try random first.
                switch (nw::roll_dice(d3)) {
                default:
                    result = nw::AttackType::invalid();
                    break;
                case 1:
                    if (get_weapon_by_attack_type(obj, attack_type_cweapon1)) {
                        result = attack_type_cweapon1;
                        weapon = get_weapon_by_attack_type(obj, attack_type_cweapon1);
                    }
                    break;
                case 2:
                    if (get_weapon_by_attack_type(obj, attack_type_cweapon2)) {
                        result = attack_type_cweapon2;
                        weapon = get_weapon_by_attack_type(obj, attack_type_cweapon2);
                    }
                    break;
                case 3:
                    if (get_weapon_by_attack_type(obj, attack_type_cweapon3)) {
                        result = attack_type_cweapon3;
                        weapon = get_weapon_by_attack_type(obj, attack_type_cweapon3);
                    }
                    break;
                }
            }

            // If still nothing go in order until something is found.
            if (!weapon) {
                weapon = get_weapon_by_attack_type(obj, attack_type_cweapon1);
                if (weapon) { result = attack_type_cweapon3; }
            }

            if (!weapon) {
                weapon = get_weapon_by_attack_type(obj, attack_type_cweapon2);
                if (weapon) { result = attack_type_cweapon3; }
            }

            if (!weapon) {
                weapon = get_weapon_by_attack_type(obj, attack_type_cweapon3);
                if (weapon) { result = attack_type_cweapon3; }
            }
        }
    } else if (obj->combat_info.attacks_offhand > 0) {
        result = attack_type_offhand;
    }

    return result;
}

std::pair<int, bool> resolve_concealment(const nw::ObjectBase* obj, const nw::ObjectBase* target, bool vs_ranged)
{
    if (!obj || !target) { return {0, false}; }

    auto end = std::end(obj->effects());
    int miss_chance_eff = 0;

    auto it = nw::find_first_effect_of(std::begin(obj->effects()), end, effect_type_miss_chance,
        *miss_chance_type_normal);

    while (it != end && it->type == effect_type_miss_chance) {
        if (it->subtype == *miss_chance_type_normal
            || (it->subtype == *miss_chance_type_melee && !vs_ranged)
            || (it->subtype == *miss_chance_type_ranged && vs_ranged)) {
            miss_chance_eff = std::max(miss_chance_eff, it->effect->get_int(0));
        }
        // [TODO] Darkness
        ++it;
    }

    int conceal_mod = nw::kernel::sum_modifier<int>(target, mod_type_concealment);

    int conceal_eff = 0;
    auto end2 = std::end(target->effects());
    auto it2 = nw::find_first_effect_of(std::begin(target->effects()), end2, effect_type_concealment,
        *miss_chance_type_normal);

    while (it2 != end2 && it2->type == effect_type_concealment) {
        if (it2->subtype == *miss_chance_type_normal
            || (it2->subtype == *miss_chance_type_melee && !vs_ranged)
            || (it2->subtype == *miss_chance_type_ranged && vs_ranged)) {
            conceal_eff = std::max(conceal_eff, it2->effect->get_int(0));
            // [TODO] Darkness
        }
        ++it2;
    }

    auto conc = std::max(conceal_mod, conceal_eff);
    if (miss_chance_eff > conc) {
        return {miss_chance_eff, true};
    } else {
        return {conc, false};
    }
}

int resolve_critical_multiplier(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase*)
{
    int result = 2;
    auto weapon = get_weapon_by_attack_type(obj, type);

    if (!obj) { return result; }
    auto base = nw::BaseItem::invalid();
    if (weapon) {
        auto baseitem = nw::kernel::rules().baseitems.get(weapon->baseitem);
        if (!baseitem) { return result; }
        result = baseitem->crit_multiplier;
        base = weapon->baseitem;
    }

    if (obj->levels.level_by_class(class_type_weapon_master) >= 5) {
        ++result;
    }

    return result;
}

int resolve_critical_threat(const nw::Creature* obj, nw::AttackType type)
{
    int start = 1;
    auto weapon = get_weapon_by_attack_type(obj, type);

    if (!obj) { return start; }

    auto base = nw::BaseItem::invalid();
    if (weapon) {
        auto baseitem = nw::kernel::rules().baseitems.get(weapon->baseitem);
        if (!baseitem) { return start; }
        start = baseitem->crit_threat;
        base = weapon->baseitem;
    }

    int result = start;

    if (nw::item_has_property(weapon, ip_keen)) {
        result += start;
    }

    if (!!nw::kernel::resolve_master_feat<int>(obj, base, mfeat_improved_crit)) {
        result += start;
    }

    if (obj->levels.level_by_class(class_type_weapon_master) >= 7) {
        result += 2;
    }

    return result;
}

int resolve_damage_immunity(const nw::ObjectBase* obj, nw::Damage type, const nw::ObjectBase* versus)
{
    if (!obj) { return 0; }
    nw::Versus vs;
    if (versus) {
        vs = versus->versus_me();
    }

    int mod_bonus = nw::kernel::max_modifier<int>(obj, mod_type_dmg_immunity, type, versus);

    auto begin = std::begin(obj->effects());
    auto end = std::end(obj->effects());
    auto [eff_bonus, it] = nw::sum_effects_of<int>(begin, end, effect_type_damage_immunity_increase,
        *type, vs);

    auto [eff_penalty, _] = nw::sum_effects_of<int>(begin, end, effect_type_damage_immunity_decrease,
        *type, vs);

    // [TODO] Check if this is actually right..
    int result = std::max(mod_bonus, eff_bonus - eff_penalty);
    return result;
}

void resolve_damage_modifiers(const nw::Creature* obj, const nw::ObjectBase* versus, nw::AttackData* data)
{
    if (!obj || !versus || !data) { return; }

    auto do_damage_resistance = [=](nw::DamageResult& dmg, int resist, nw::Effect* resist_eff) {
        int resist_remaining = 0;
        if (resist_eff) { resist_remaining = resist_eff->get_int(1); }

        // The amount that can be resisted is the minimum of amount, the resistance, and the remaining
        // resist amount.
        int amount = std::min(dmg.amount, resist);
        if (resist_remaining > 0) {
            amount = std::min(amount, resist_remaining);
        }

        dmg.amount -= amount;
        dmg.resist = amount;

        if (resist_remaining > 0) {
            dmg.resist_remaining = resist_remaining - amount;
            if (dmg.resist_remaining == 0) {
                data->effects_to_remove.push_back(resist_eff->handle());
            } else {
                resist_eff->set_int(1, dmg.resist_remaining);
            }
        }
    };

    auto do_damage_immunity = [=](nw::DamageResult& dmg, int imm) {
        if (imm >= 100) {
            dmg.immunity = dmg.amount;
            dmg.amount = 0;
        } else {
            dmg.immunity = (imm * dmg.amount) / 100;
            dmg.amount -= dmg.immunity;
        }
    };

    // Do Resistance and Immunity for all non-physical weapon damage
    for (auto& dmg : data->damages()) {
        if (dmg.amount <= 0) { continue; }
        auto [resist, resist_eff] = resolve_damage_resistance(versus, dmg.type, obj);
        do_damage_resistance(dmg, resist, resist_eff);

        if (dmg.amount > 0) {
            auto imm = resolve_damage_immunity(versus, dmg.type, obj);
            do_damage_immunity(dmg, imm);
        }
    }

    // Do Resistance and Immunity for all non-physical weapon damage
    // Resistance favors attacker, Immunity favors defender
    const auto flags = resolve_weapon_damage_flags(data->weapon);

    int least_resist = std::numeric_limits<int>::max();
    nw::Effect* least_resist_eff = nullptr;
    for (auto type : {damage_type_bludgeoning, damage_type_piercing, damage_type_slashing}) {
        if (!flags.test(type)) { continue; }

        auto [resist, resist_eff] = resolve_damage_resistance(versus, type, obj);
        if (resist < least_resist) {
            least_resist = resist;
            least_resist_eff = resist_eff;
        }
    }
    do_damage_resistance(data->damage_base, least_resist, least_resist_eff);

    if (data->damage_base.amount > 0) {
        int best_imm = 0;
        for (auto type : {damage_type_bludgeoning, damage_type_piercing, damage_type_slashing}) {
            if (!flags.test(type)) { continue; }

            auto imm = resolve_damage_immunity(versus, type, obj);
            best_imm = std::max(best_imm, imm);
        }
        do_damage_immunity(data->damage_base, best_imm);
    }

    if (data->damage_base.amount > 0) {
        auto power = resolve_weapon_power(obj, data->weapon);
        auto [red, red_eff] = resolve_damage_reduction(versus, power, obj);
        int red_remaining = 0;
        if (red_eff) { red_remaining = red_eff->get_int(2); }

        int amount = std::min(data->damage_base.amount, red);
        if (red_remaining > 0) {
            amount = std::min(amount, red_remaining);
        }

        data->damage_base.amount -= amount;
        data->damage_base.resist = amount;

        if (red_remaining > 0) {
            data->damage_base.reduction_remaining = red_remaining - amount;
            if (data->damage_base.reduction_remaining == 0) {
                data->effects_to_remove.push_back(red_eff->handle());
            } else {
                red_eff->set_int(2, data->damage_base.reduction_remaining);
            }
        }
    }
}

std::pair<int, nw::Effect*> resolve_damage_reduction(const nw::ObjectBase* obj, int power, const nw::ObjectBase* versus)
{
    if (!obj || power <= 0) { return {0, nullptr}; }

    nw::Versus vs;
    if (versus) {
        vs = versus->versus_me();
    }

    int mod_bonus = nw::kernel::sum_modifier<int>(obj, mod_type_dmg_reduction, versus);

    auto begin = std::begin(obj->effects());
    auto end = std::end(obj->effects());
    auto it = nw::find_first_effect_of(begin, end, effect_type_damage_reduction);
    int best = 0, best_remain = 0;
    nw::Effect* best_eff = nullptr;
    while (it != end && it->type == effect_type_damage_reduction) {
        if (it->effect->get_int(1) > power) {
            if (it->effect->get_int(0) > best
                || (it->effect->get_int(0) == best && it->effect->get_int(2) > best_remain)) {
                best = it->effect->get_int(0);
                best_eff = it->effect;
                best_remain = it->effect->get_int(2);
            }
        }
        ++it;
    }

    if (mod_bonus >= best) {
        return {mod_bonus, nullptr};
    }
    return {best, best_eff};
}

std::pair<int, nw::Effect*> resolve_damage_resistance(const nw::ObjectBase* obj, nw::Damage type, const nw::ObjectBase* versus)
{
    if (!obj) { return {0, nullptr}; }

    nw::Versus vs;
    if (versus) { vs = versus->versus_me(); }

    int mod_bonus = nw::kernel::max_modifier<int>(obj, mod_type_dmg_resistance, type, versus);

    auto begin = std::begin(obj->effects());
    auto end = std::end(obj->effects());
    auto it = nw::find_first_effect_of(begin, end, effect_type_damage_resistance);
    int best = 0, best_remain = 0;
    nw::Effect* best_eff = nullptr;
    while (it != end && it->type == effect_type_damage_resistance) {
        if (it->effect->subtype == *type) {
            if (it->effect->get_int(0) > best
                || (it->effect->get_int(0) == best && it->effect->get_int(1) > best_remain)) {
                best = it->effect->get_int(0);
                best_eff = it->effect;
                best_remain = it->effect->get_int(1);
            }
        }
        ++it;
    }

    if (mod_bonus >= best) {
        return {mod_bonus, nullptr};
    }
    return {best, best_eff};
}

std::pair<int, int> resolve_dual_wield_penalty(const nw::Creature* obj)
{
    if (!obj) { return {0, 0}; }
    int on = 0;
    int off = 0;
    bool off_is_light = false;

    // Make sure their is an onhand weapon
    auto rh = get_equipped_item(obj, nw::EquipIndex::righthand);
    if (!rh) { return {on, off}; }

    auto rh_bi = nw::kernel::rules().baseitems.get(rh->baseitem);
    if (!rh_bi || !rh_bi->weapon_type) { return {on, off}; }

    if (!is_double_sided_weapon(rh)) {
        // Make sure their is an offhand weapon
        auto lh = get_equipped_item(obj, nw::EquipIndex::lefthand);
        if (!lh) { return {on, off}; }

        auto lh_bi = nw::kernel::rules().baseitems.get(lh->baseitem);
        if (!lh_bi || !lh_bi->weapon_type) { return {on, off}; }

        off_is_light = is_light_weapon(obj, lh);
    } else {
        off_is_light = true;
    }

    if (off_is_light) {
        on = -4;
        off = -8;
    } else {
        on = -6;
        off = -10;
    }

    if (obj->combat_info.ac_armor_base <= 3 && obj->levels.level_by_class(class_type_ranger) > 0) {
        on += 2;
        off += 6;
    } else {
        if (obj->stats.has_feat(feat_two_weapon_fighting)) {
            on += 2;
            off += 2;
        }

        if (obj->stats.has_feat(feat_ambidexterity)) {
            off += 4;
        }
    }

    return {on, off};
}

int resolve_iteration_penalty(const nw::Creature* attacker, nw::AttackType type)
{
    auto weapon = get_weapon_by_attack_type(attacker, type);
    int iter = weapon_iteration(attacker, weapon);

    if (type == attack_type_offhand) {
        iter = iter * (attacker->combat_info.attack_current - attacker->combat_info.attacks_onhand - attacker->combat_info.attacks_extra);
    } else {
        iter = iter * attacker->combat_info.attack_current;
    }

    return iter;
}

std::pair<int, int> resolve_number_of_attacks(const nw::Creature* obj)
{
    int onhand = 1, offhand = 0;
    int ab = base_attack_bonus(obj);

    auto item_on = get_equipped_item(obj, nw::EquipIndex::righthand);
    int iter = weapon_iteration(obj, item_on);
    onhand = iter > 0 ? ab / iter : 0;
    if (iter == 5) {
        onhand = std::min(4, onhand);
    } else if (iter == 3) {
        onhand = std::min(6, onhand);
    }

    auto item_off = get_equipped_item(obj, nw::EquipIndex::lefthand);
    if (!item_off) { return {onhand, offhand}; }

    auto item_off_bi = nw::kernel::rules().baseitems.get(item_off->baseitem);
    if (!item_off_bi || item_off_bi->weapon_type == 0) { return {onhand, offhand}; }

    offhand = 1;
    if (obj->stats.has_feat(feat_improved_two_weapon_fighting)) {
        ++offhand;
    } else if (obj->combat_info.ac_armor_base <= 3 && obj->levels.level_by_class(class_type_ranger) >= 9) {
        ++offhand;
    }

    return {onhand, offhand};
}

nw::TargetState resolve_target_state(const nw::Creature* attacker, const nw::ObjectBase* target)
{
    nw::TargetState result = nw::TargetState::none;
    if (!attacker || !target) { return result; }

    if (is_flanked(target->as_creature(), attacker)) {
        result |= nw::TargetState::flanked;
    }

    return result;
}

nw::DamageFlag resolve_weapon_damage_flags(const nw::Item* weapon)
{
    nw::DamageFlag result;
    if (!weapon) {
        result.set(damage_type_bludgeoning);
    } else {
        auto bi = nw::kernel::rules().baseitems.get(weapon->baseitem);
        if (!bi) { return result; }
        switch (bi->weapon_type) {
        default:
            break;
        case 1:
            result.set(damage_type_piercing);
            break;
        case 2:
            result.set(damage_type_bludgeoning);
            break;
        case 3:
            result.set(damage_type_slashing);
            break;
        case 4:
            result.set(damage_type_piercing);
            result.set(damage_type_slashing);
            break;
        case 5:
            result.set(damage_type_bludgeoning);
            result.set(damage_type_piercing);
            break;
        }
    }
    return result;
}

int resolve_weapon_power(const nw::Creature* obj, const nw::Item* weapon)
{
    if (!obj) { return 0; }
    int result = 0;

    bool is_monk_or_null = !weapon || is_monk_weapon(weapon);
    auto [yes, level] = can_use_monk_abilities(obj);
    if (yes && is_monk_or_null) {
        if (obj->stats.has_feat(feat_epic_improved_ki_strike_5)) {
            result = 5;
        } else if (obj->stats.has_feat(feat_epic_improved_ki_strike_4)) {
            result = 4;
        } else if (obj->stats.has_feat(feat_ki_strike_3)) {
            result = 3;
        } else if (obj->stats.has_feat(feat_ki_strike_2)) {
            result = 2;
        } else if (obj->stats.has_feat(feat_ki_strike)) {
            result = 1;
        }
    }

    // If there's no weapon return
    if (!weapon) { return result; }

    // Item properties
    for (const auto& ip : weapon->properties) {
        if (ip.type == *ip_attack_bonus || ip.type == *ip_enhancement_bonus) {
            result = std::max(result, int(ip.cost_value));
        }
    }

    // Enchant Arrow
    auto base = weapon->baseitem;
    if (base == base_item_longbow || base == base_item_shortbow) {
        int aa = 0;
        auto feat = nw::highest_feat_in_range(obj, feat_prestige_enchant_arrow_6,
            feat_prestige_enchant_arrow_20);
        if (feat != nw::Feat::invalid()) {
            aa = *feat - *feat_prestige_enchant_arrow_6 + 6;
        } else {
            feat = nw::highest_feat_in_range(obj, feat_prestige_enchant_arrow_1, feat_prestige_enchant_arrow_5);
            if (feat != nw::Feat::invalid()) {
                aa = *feat - *feat_prestige_enchant_arrow_1 + 1;
            }
        }
        result = std::max(result, aa);
    }

    return result;
}

bool weapon_is_finessable(const nw::Creature* obj, nw::Item* weapon)
{
    if (!obj) { return false; }
    if (!weapon) { return true; }
    auto baseitem = nw::kernel::rules().baseitems.get(weapon->baseitem);
    if (!baseitem) { return false; }
    return baseitem->finesse_size <= obj->size;
}

int weapon_iteration(const nw::Creature* obj, const nw::Item* weapon)
{
    if (!obj) { return 0; }

    bool is_monk_or_null = !weapon || is_monk_weapon(weapon);
    auto [yes, level] = can_use_monk_abilities(obj);
    if (is_monk_or_null && yes) {
        return 3;
    }

    return 5;
}

} // namespace nwn1
