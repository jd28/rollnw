#include "funcs_combat.hpp"

#include "../constants.hpp"
#include "../functions.hpp"

#include <nw/components/Creature.hpp>
#include <nw/components/Equips.hpp>
#include <nw/components/Item.hpp>
#include <nw/components/LevelStats.hpp>
#include <nw/functions.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/items.hpp>
#include <nw/rules/system.hpp>
#include <nw/util/macros.hpp>

namespace nwn1 {

int attack_bonus(const nw::Creature* obj, nw::AttackType type, nw::ObjectBase* versus)
{
    int result = 0;
    if (!obj) { return result; }

    nw::Versus vs;
    if (versus) { vs = versus->versus_me(); }

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
    // General modifiers, i.e. Epic Prowess
    nw::kernel::resolve_modifier(obj, mod_type_attack_bonus, attack_type_any, versus, mod_adder);
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

    it = nw::resolve_effects_of<int>(it, end, effect_type_attack_increase, *type, vs,
        bonus_adder, &nw::effect_extract_int0);

    int decrease = 0;
    auto decrease_adder = [&decrease](int mod) { decrease += mod; };

    it = nw::resolve_effects_of<int>(it, end, effect_type_attack_decrease, *attack_type_any, vs,
        decrease_adder, &nw::effect_extract_int0);

    nw::resolve_effects_of<int>(it, end, effect_type_attack_decrease, *type, vs,
        decrease_adder, &nw::effect_extract_int0);

    auto [min, max] = nw::kernel::rules().attack_effect_limits();
    return result + modifier + std::clamp(bonus - decrease, min, max);
}

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

bool is_ranged_weapon(const nw::Item* item)
{
    if (!item) { return false; }
    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    return bi ? bi->ranged : false;
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

int number_of_attacks(const nw::Creature* obj, bool offhand)
{
    int ab = base_attack_bonus(obj);
    auto equip = offhand ? nw::EquipIndex::lefthand : nw::EquipIndex::lefthand;
    auto item = get_equipped_item(obj, equip);
    int iter = weapon_iteration(obj, item);
    return ab / iter;
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
