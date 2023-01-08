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

nw::AttackType equip_index_to_attack_type(nw::EquipIndex equip)
{
    switch (equip) {
    default:
        return attack_type_any;
    case nw::EquipIndex::righthand:
        return attack_type_onhand;
    case nw::EquipIndex::lefthand:
        return attack_type_offhand;
    case nw::EquipIndex::arms:
        return attack_type_unarmed;
    case nw::EquipIndex::creature_bite:
        return attack_type_cweapon1;
    case nw::EquipIndex::creature_left:
        return attack_type_cweapon2;
    case nw::EquipIndex::creature_right:
        return attack_type_cweapon3;
    }
}

nw::Item* get_weapon_by_attack_type(const nw::Creature* obj, nw::AttackType type)
{
    switch (*type) {
    default:
        return nullptr;
    case *attack_type_onhand:
        return get_equipped_item(obj, nw::EquipIndex::righthand);
    case *attack_type_offhand:
        return get_equipped_item(obj, nw::EquipIndex::lefthand);
    case *attack_type_unarmed:
        return get_equipped_item(obj, nw::EquipIndex::arms);
    case *attack_type_cweapon1:
        return get_equipped_item(obj, nw::EquipIndex::creature_bite);
    case *attack_type_cweapon2:
        return get_equipped_item(obj, nw::EquipIndex::creature_left);
    case *attack_type_cweapon3:
        return get_equipped_item(obj, nw::EquipIndex::creature_right);
    }
}

bool is_flanked(const nw::Creature* target, const nw::Creature* attacker)
{
    if (!target || !attacker) { return false; }
    if (target->combat_info.target == attacker) { return false; }
    if (attacker->combat_info.target_distance_sq > 10.0f * 10.0f) { return false; }
    if (target->stats.has_feat(feat_prestige_defensive_awareness_2)) { return false; }
    return true;
}

int resolve_attack_bonus(const nw::Creature* obj, nw::AttackType type, nw::ObjectBase* versus)
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

    // Size
    int modifier = obj->combat_info.size_ab_modifier;

    // Modifiers - abilities will be handled by the first modifier search.
    auto mod_adder = [&modifier](int value) { modifier += value; };
    nw::kernel::resolve_modifier(obj, mod_type_attack_bonus, type, versus, mod_adder);
    if (type != attack_type_any) {
        // General modifiers, i.e. Epic Prowess
        nw::kernel::resolve_modifier(obj, mod_type_attack_bonus, attack_type_any, versus, mod_adder);
    }
    // Weapon feats, weapon master type stuff occurs below.
    nw::kernel::resolve_modifier(obj, mod_type_attack_bonus_item, baseitem, versus, mod_adder);
    // Combat Modes
    nw::kernel::resolve_modifier(obj, mod_type_attack_bonus_mode, obj->combat_info.combat_mode, versus, mod_adder);

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

nw::AttackResult resolve_attack_roll(const nw::Creature* obj, nw::AttackType type, nw::ObjectBase* vs)
{
    // [TODO], iteration, lots..

    static constexpr nw::DiceRoll d20{1, 20, 0};
    const auto roll = nw::roll_dice(d20);
    if (roll == 1) { return nw::AttackResult::miss_by_auto_fail; }

    auto weapon = get_weapon_by_attack_type(obj, type);
    auto attack_result = nw::AttackResult::miss_by_roll;

    const auto iter = weapon_iteration(obj, weapon);
    const auto ab = resolve_attack_bonus(obj, type, vs);
    const auto ac = calculate_ac_versus(obj, vs, false);

    if (roll == 20) {
        attack_result = nw::AttackResult::hit_by_auto_success;
    } else {
        if (ab + roll >= ac) {
            attack_result = nw::AttackResult::hit_by_roll;
        }
    }

    if (nw::is_attack_type_hit(attack_result)) {
        int crit_threat = resolve_critical_threat(obj, type);
        if (21 - roll <= crit_threat && ab + nw::roll_dice(d20) >= ac) {
            attack_result = nw::AttackResult::hit_by_critical;
        }

        auto [conceal, source] = resolve_concealment(obj, vs);
        if (conceal > 0) {
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
    if (obj->combat_info.attack_current < obj->combat_info.attacks_onhand + obj->combat_info.attacks_offhand) {
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

int resolve_critical_multiplier(const nw::Creature* obj, nw::AttackType type, nw::ObjectBase*)
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

std::pair<int, int> resolve_number_of_attacks(const nw::Creature* obj)
{
    int onhand = 1, offhand = 0;
    int ab = base_attack_bonus(obj);

    auto item_on = get_equipped_item(obj, nw::EquipIndex::lefthand);
    int iter = weapon_iteration(obj, item_on);
    onhand = iter > 0 ? ab / iter : 0;
    if (iter == 5) {
        onhand = std::max(4, onhand);
    } else if (iter == 3) {
        onhand = std::max(6, onhand);
    }

    auto item_off = get_equipped_item(obj, nw::EquipIndex::righthand);
    iter = weapon_iteration(obj, item_off);
    if (iter > 0) {
        offhand = 1;
        if (obj->stats.has_feat(feat_improved_two_weapon_fighting)) {
            ++offhand;
        }
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

    bool is_monk_or_null = !!weapon;
    if (weapon) {
        auto baseitem = nw::kernel::rules().baseitems.get(weapon->baseitem);
        is_monk_or_null = baseitem->is_monk_weapon;
    }

    auto [yes, level] = can_use_monk_abilities(obj);
    if (is_monk_or_null && yes) {
        return 3;
    }

    return 5;
}

} // namespace nwn1
