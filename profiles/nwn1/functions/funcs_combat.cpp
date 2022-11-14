#include "funcs_combat.hpp"

#include "../constants.hpp"
#include "../functions.hpp"
#include "nw/components/Equips.hpp"
#include "nw/rules/Attack.hpp"
#include "nw/rules/BaseItem.hpp"

#include <nw/components/Creature.hpp>
#include <nw/components/Item.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/Class.hpp>
#include <nw/util/macros.hpp>

namespace nwn1 {

int attack_bonus(const nw::Creature* obj, nw::AttackType type, bool base)
{
    ROLLNW_UNUSED(base);
    int result = 0;
    if (!obj) { return result; }

    auto& classes = nw::kernel::rules().classes;

    // [TODO] This is obviously pretty wrong.
    for (const auto& cl : obj->levels.entries) {
        if (!classes.entries[cl.id.idx()].attack_bonus_table) {
            continue;
        }
        result += (*classes.entries[cl.id.idx()].attack_bonus_table)[cl.level];
    }

    // Size
    base += obj->size_ab_modifier;

    if (base) { return result; }

    auto weapon = get_weapon_by_attack_type(obj, type);
    auto baseitem = nw::BaseItem::invalid();
    if (!weapon) {
        // If there is no weapon, proceed as an unarmed attack.
        type = attack_type_unarmed;
    } else {
        baseitem = nw::BaseItem::make(weapon->baseitem);
    }

    // Modifiers
    nw::kernel::rules().calculate(obj, mod_type_attack_bonus, baseitem,
        [&result](int value) { result += value; });

    // Master Feats
    auto& mfr = nw::kernel::rules().master_feats;
    for (auto b : mfr.resolve<int>(obj, baseitem, mfeat_weapon_focus, mfeat_weapon_focus_epic)) {
        result += b;
    }

    // int wm = obj->levels.level_by_class(class_type_weapon_master);
    // auto s = mfr.resolve<int>(obj, baseitem, mfeat_weapon_of_choice);

    // Abilities
    int modifier = get_ability_modifier(obj, ability_strength);
    if (obj->stats.has_feat(feat_weapon_finesse) && weapon_is_finessable(obj, weapon)) {
        modifier = std::max(modifier, get_ability_modifier(obj, ability_dexterity));
    }
    result += modifier;

    // Effects attack increase/decrease is a little more complicated due to needing to support
    // an 'any' subtype.
    auto end = std::end(obj->effects());
    int value = 0;
    auto callback = [&value](int mod) { value += mod; };

    auto it = find_first_effect_of(std::begin(obj->effects()), end,
        effect_type_attack_increase, *attack_type_any);
    resolve_effects_of<int>(it, end, effect_type_attack_increase, *attack_type_any,
        callback, &effect_extract_int0);

    it = find_first_effect_of(std::begin(obj->effects()), end,
        effect_type_attack_increase, *type);
    resolve_effects_of<int>(it, end, effect_type_attack_increase, *type,
        callback, &effect_extract_int0);

    int bonus = value;
    value = 0;

    it = find_first_effect_of(it, end, effect_type_attack_decrease, *attack_type_any);
    resolve_effects_of<int>(it, end, effect_type_attack_decrease, *attack_type_any,
        callback, &effect_extract_int0);

    it = find_first_effect_of(it, end, effect_type_attack_decrease, *type);
    resolve_effects_of<int>(it, end, effect_type_attack_decrease, *type,
        callback, &effect_extract_int0);
    int decrease = value;

    return result + std::clamp(bonus - decrease, -12, 12);

    return result;
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

int number_of_attacks(const nw::Creature* obj, bool offhand)
{
    int ab = attack_bonus(obj, offhand ? attack_type_offhand : attack_type_onhand, true);
    // int iter = weapon_iteration(ent, )
    return ab / 5;
}

bool weapon_is_finessable(const nw::Creature* obj, nw::Item* weapon)
{
    if (!obj) { return false; }
    if (!weapon) { return true; }
    auto& bia = nw::kernel::rules().baseitems;
    auto baseitem = bia.get(nw::BaseItem::make(weapon->baseitem));
    if (!baseitem) { return false; }
    return baseitem->finesse_size >= obj->size;
}

int weapon_iteration(const nw::Creature* obj, nw::Item* weapon)
{
    if (!obj) { return 0; }

    auto& bia = nw::kernel::rules().baseitems;

    auto baseitem = bia.get(nw::BaseItem::make(weapon->baseitem));
    if (!baseitem) { return 0; }

    auto [yes, level] = can_use_monk_abilities(obj);
    if (baseitem->is_monk_weapon && yes) {
        return 3;
    }

    return 5;
}

} // namespace nwn1
