#include "functions.hpp"

#include "constants.hpp"

#include <nw/functions.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/EventSystem.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/kernel/TwoDACache.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/util/templates.hpp>

namespace nwn1 {

// == Abilities ===============================================================
// ============================================================================

int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base)
{
    if (!obj || ability == nw::Ability::invalid()) { return 0; }

    // Base
    int result = obj->stats.get_ability_score(ability);

    // Race
    auto race = nw::kernel::rules().races.get(obj->race);
    if (race) { result += race->ability_modifiers[ability.idx()]; }

    // Modifiers = Feats, etc
    nw::kernel::resolve_modifier(obj, mod_type_ability, ability,
        [&result](int value) { result += value; });

    if (base) { return result; }

    // Effects
    auto begin = std::begin(obj->effects());
    auto end = std::end(obj->effects());

    auto [bonus, it] = nw::sum_effects_of<int>(begin, end,
        effect_type_ability_increase, *ability);

    auto [decrease, _] = nw::sum_effects_of<int>(it, end,
        effect_type_ability_decrease, *ability);

    auto [min, max] = nw::kernel::effects().effect_limits_ability();
    return result + std::clamp(bonus - decrease, min, max);
}

int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base)
{
    return (get_ability_score(obj, ability, base) - 10) / 2;
}

int get_dex_modifier(const nw::Creature* obj)
{
    int base = get_ability_modifier(obj, ability_dexterity);
    auto item = get_equipped_item(obj, nw::EquipIndex::chest);
    if (item && item->baseitem == base_item_armor && item->armor_id != -1) {
        // [TODO] Optimize
        auto& tda = nw::kernel::twodas();
        if (auto armor = tda.get("armor")) {
            if (auto result = armor->get<int32_t>(item->armor_id, "DEXBONUS")) {
                return std::min(base, *result);
            }
        }
    }
    return base;
}

// == Armor Class =============================================================
// ============================================================================
int calculate_ac_versus(const nw::ObjectBase* obj, const nw::ObjectBase* versus, bool is_touch_attack)
{
    // Basing some of this off Pathfinder SRD.
    // Touch attack ignores Natural, Shield, Armor AC.
    if (!obj) { return 0; }
    auto cre = obj->as_creature();

    int result = 10;
    int natural = 0;
    int dex = cre ? get_dex_modifier(cre) : 0;
    bool dexed = true;
    nw::Versus vs;

    if (versus) {
        vs = versus->versus_me();
    }

    if (cre) {
        result += cre->combat_info.size_ac_modifier; // Size
    }

    if (!is_touch_attack) {
        if (cre) {
            result += cre->combat_info.ac_armor_base;    // Armor
            result += cre->combat_info.ac_natural_bonus; // Natural
            result += cre->combat_info.ac_shield_base;   // Shield
        }
        // Modifiers - only support AC natural for now..
        auto natural_adder = [&natural](int value) { natural += value; };
        nw::kernel::resolve_modifier(obj, mod_type_armor_class, ac_natural, versus, natural_adder);
    }

    std::array<int, 5> results{};

    // Effects
    auto begin = std::begin(obj->effects());
    auto end = std::end(obj->effects());

    auto [dodge_bon, it_bon] = nw::sum_effects_of<int>(begin, end,
        effect_type_ac_increase, *ac_dodge);
    auto [dodge_pen, it_pen] = nw::sum_effects_of<int>(begin, end,
        effect_type_ac_decrease, *ac_dodge);

    if (is_touch_attack) {
        auto [bonus, itb] = nw::max_effects_of<int>(it_bon, end,
            effect_type_ac_increase, *ac_deflection);
        auto [penalty, itp] = nw::max_effects_of<int>(it_pen, end,
            effect_type_ac_decrease, *ac_deflection);
        results[ac_deflection.idx()] = bonus - penalty;
    } else {
        for (auto type : {ac_natural, ac_armor, ac_shield, ac_deflection}) {
            auto [bonus, itb] = nw::max_effects_of<int>(it_bon, end,
                effect_type_ac_increase, *type);
            auto [penalty, itp] = nw::max_effects_of<int>(it_pen, end,
                effect_type_ac_decrease, *type);
            results[type.idx()] = bonus - penalty;
            it_bon = itb;
            it_pen = itp;
        }
    }
    auto [min, max] = nw::kernel::effects().effect_limits_armor_class();

    natural += std::clamp(results[ac_natural.idx()], min, max);
    int armor = std::clamp(results[ac_armor.idx()], min, max);
    int deflect = std::clamp(results[ac_deflection.idx()], min, max);
    int dodge = std::clamp(dodge_bon - dodge_pen, min, max);
    int shield = std::clamp(results[ac_shield.idx()], min, max);

    if (dexed) {
        if (cre && cre->hasted) { dodge += 4; }
        result += dex;
        auto dodge_adder = [&dodge](int value) { dodge += value; };
        nw::kernel::resolve_modifier(obj, mod_type_armor_class, ac_dodge, versus, dodge_adder);
    }

    return result + armor + deflect + dodge + natural + shield;
}

int calculate_item_ac(const nw::Item* obj)
{
    if (!obj) { return 0; }
    if (obj->baseitem == base_item_armor && obj->armor_id != -1) {
        // [TODO] Optimize
        auto& tda = nw::kernel::twodas();
        if (auto armor = tda.get("armor")) {
            if (auto result = armor->get<int32_t>(obj->armor_id, "ACBONUS")) {
                return *result;
            }
        }
    } else if (is_shield(obj->baseitem)) {
        // [TODO] Figure this out
        return 0;
    }

    return 0;
}

// == Classes =================================================================
// ============================================================================

std::pair<bool, int> can_use_monk_abilities(const nw::Creature* obj)
{
    if (class_type_monk == nw::Class::invalid()) {
        return {false, 0};
    }
    auto level = obj->levels.level_by_class(class_type_monk);
    if (level == 0) {
        return {false, 0};
    }
    return {true, level};
}

// == Effects =================================================================
// ============================================================================

bool has_effect_type_applied(nw::ObjectBase* obj, nw::EffectType type)
{
    if (!obj) { return false; }
    for (const auto& it : obj->effects()) {
        if (it.type == type) { return true; }
    }
    return false;
}

int queue_remove_effect_by(nw::ObjectBase* obj, nw::ObjectHandle creator)
{
    if (!obj) { return 0; }
    int processed = 0;
    for (const auto& handle : obj->effects()) {
        if (handle.effect->creator == creator) {
            nw::kernel::events().add(nw::kernel::EventType::remove_effect, obj, handle.effect);
            ++processed;
        }
    }
    return processed;
}

// == Items ===================================================================
// ============================================================================

// [TODO] Check polymorph, creature size for weapons
bool can_equip_item(const nw::Creature* obj, nw::Item* item, nw::EquipIndex slot)
{
    if (!obj || !item) { return false; }

    auto baseitem = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!baseitem) { return false; }

    if (!nw::kernel::rules().meets_requirement(baseitem->feat_requirement, obj)) {
        return false;
    }

    auto flag = 1 << size_t(slot);
    return baseitem->equipable_slots & flag;
}

bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot)
{
    if (!obj || !item) { return false; }
    if (!can_equip_item(obj, item, slot)) { return false; }
    unequip_item(obj, slot);
    obj->equipment.equips[size_t(slot)] = item;
    nw::process_item_properties(obj, item, slot, false);
    if (slot == nw::EquipIndex::chest) {
        obj->combat_info.ac_armor_base = calculate_item_ac(item);
    } else if (slot == nw::EquipIndex::lefthand && is_shield(item->baseitem)) {
        obj->combat_info.ac_shield_base = calculate_item_ac(item);
    }

    return true;
}

nw::Item* get_equipped_item(const nw::Creature* obj, nw::EquipIndex slot)
{
    nw::Item* result = nullptr;
    if (!obj) { return result; }

    auto& it = obj->equipment.equips[size_t(slot)];
    if (alt<nw::Item*>(it)) {
        result = std::get<nw::Item*>(it);
    }
    return result;
}

int get_relative_weapon_size(const nw::Creature* obj, const nw::Item* item)
{
    if (!obj || !item) { return 0; }
    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    return bi ? bi->weapon_size - obj->size : 0;
}

bool is_creature_weapon(const nw::Item* item)
{
    if (!item) { return false; }
    return item->baseitem == base_item_cbludgweapon
        || item->baseitem == base_item_cpiercweapon
        || item->baseitem == base_item_cslashweapon
        || item->baseitem == base_item_cslshprcweap;
}

bool is_double_sided_weapon(const nw::Item* item)
{
    if (!item) { return false; }
    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    return bi ? bi->weapon_wield == 8 : false;
}

bool is_light_weapon(const nw::Creature* obj, const nw::Item* item)
{
    if (!obj || !item) { return false; }
    return get_relative_weapon_size(obj, item) < 0;
}

bool is_monk_weapon(const nw::Item* item)
{
    if (!item) { return true; }
    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    return bi ? bi->is_monk_weapon : false;
}

bool is_ranged_weapon(const nw::Item* item)
{
    if (!item) { return false; }
    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    return bi ? bi->ranged : false;
}

bool is_shield(nw::BaseItem baseitem)
{
    return baseitem == base_item_smallshield
        || baseitem == base_item_largeshield
        || baseitem == base_item_towershield;
}

bool is_two_handed_weapon(const nw::Creature* obj, const nw::Item* item)
{
    if (!obj || !item) { return false; }
    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!bi) { return false; }
    return bi->weapon_size > obj->size;
}

bool is_unarmed_weapon(const nw::Item* item)
{
    if (!item) { return true; }
    auto bi = item->baseitem;
    return bi == base_item_gloves || bi == base_item_bracer || is_creature_weapon(item);
}

nw::Item* unequip_item(nw::Creature* obj, nw::EquipIndex slot)
{
    nw::Item* result = nullptr;
    if (!obj) { return result; }

    auto& it = obj->equipment.equips[size_t(slot)];
    if (alt<nw::Item*>(it)) {
        result = std::get<nw::Item*>(it);
        if (!result) { return result; }
        it = nullptr;
        nw::process_item_properties(obj, result, slot, true);
        if (slot == nw::EquipIndex::chest) {
            obj->combat_info.ac_armor_base = 0;
        } else if (slot == nw::EquipIndex::lefthand && is_shield(result->baseitem)) {
            obj->combat_info.ac_shield_base = 0;
        }
    }
    return result;
}

int saving_throw(const nw::ObjectBase* obj, nw::Save type, nw::SaveVersus type_vs,
    const nw::ObjectBase* versus)
{
    int result = 0;
    if (!obj) { return 0; }
    auto cre = obj->as_creature();

    nw::Versus vs;
    if (versus) { vs = versus->versus_me(); }

    // Innate - [TODO] Clean all this up..
    if (cre) {
        switch (*type) {
        default:
            break;
        case *saving_throw_fort:
            result += cre->stats.save_bonus.fort;
            break;
        case *saving_throw_reflex:
            result += cre->stats.save_bonus.reflex;
            break;
        case *saving_throw_will:
            result += cre->stats.save_bonus.will;
            break;
        }
    } else if (auto door = obj->as_door()) {
        switch (*type) {
        default:
            break;
        case *saving_throw_fort:
            result += door->saves.fort;
            break;
        case *saving_throw_reflex:
            result += door->saves.reflex;
            break;
        case *saving_throw_will:
            result += door->saves.will;
            break;
        }
    } else if (auto plc = obj->as_placeable()) {
        switch (*type) {
        default:
            break;
        case *saving_throw_fort:
            result += plc->saves.fort;
            break;
        case *saving_throw_reflex:
            result += plc->saves.reflex;
            break;
        case *saving_throw_will:
            result += plc->saves.will;
            break;
        }
    }

    if (cre) {
        // Ability
        switch (*type) {
        default:
            break;
        case *saving_throw_fort:
            result += get_ability_modifier(cre, ability_constitution);
            break;
        case *saving_throw_reflex:
            result += get_ability_modifier(cre, ability_dexterity);
            break;
        case *saving_throw_will:
            result += get_ability_modifier(cre, ability_wisdom);
            break;
        }

        // Class
        auto& classes = nw::kernel::rules().classes;
        for (size_t i = 0; i < nw::LevelStats::max_classes; ++i) {
            auto id = cre->levels.entries[i].id;
            int level = cre->levels.entries[i].level;

            if (id == nw::Class::invalid()) { break; }
            switch (*type) {
            default:
                break;
            case *saving_throw_fort:
                result += classes.get_class_save_bonus(id, level).fort;
                break;
            case *saving_throw_reflex:
                result += classes.get_class_save_bonus(id, level).reflex;
                break;
            case *saving_throw_will:
                result += classes.get_class_save_bonus(id, level).will;
                break;
            }
        }
    }

    // Effects
    auto begin = std::begin(obj->effects());
    auto end = std::end(obj->effects());
    int inc = 0, dec = 0;
    auto it = nw::find_first_effect_of(begin, end, effect_type_saving_throw_increase);
    while (it != end && (it->type == effect_type_saving_throw_increase || it->type == effect_type_saving_throw_decrease)) {
        auto save_type = nw::Save::make(it->subtype);
        if ((type == save_type || save_type == nw::Save::invalid())
            && it->effect->versus().match(vs)
            && it->effect->get_int(1) == *type_vs) {

            if (it->type == effect_type_saving_throw_increase) {
                inc += it->effect->get_int(0);
            } else if (it->type == effect_type_saving_throw_decrease) {
                dec += it->effect->get_int(0);
            }
        }
        ++it;
    }

    return result + std::clamp(inc - dec, -20, 20);
}

bool resolve_saving_throw(const nw::ObjectBase* obj, nw::Save type, int dc,
    nw::SaveVersus type_vs, const nw::ObjectBase* versus)
{
    static constexpr nw::DiceRoll d20{1, 20};
    if (!obj) { return false; }

    int roll = nw::roll_dice(d20);

    if (roll == 1) { return false; }
    if (roll == 20) { return true; }

    dc = std::clamp(dc, 1, 255);
    int save = saving_throw(obj, type, type_vs, versus);

    return roll + save >= dc;
}

// == Skills ==================================================================
// ============================================================================

int get_skill_rank(const nw::Creature* obj, nw::Skill skill, nw::ObjectBase* versus, bool base)
{
    if (!obj) { return 0; }

    nw::Versus vs;
    if (versus) { vs = versus->versus_me(); }

    int result = 0;
    auto adder = [&result](int value) { result += value; };

    auto& skill_array = nw::kernel::rules().skills;
    auto ski = skill_array.get(skill);
    if (!ski) {
        LOG_F(WARNING, "attempting to get skill rank of invalid skill: {}", *skill);
        return 0;
    }

    // Base
    result = obj->stats.get_skill_rank(skill);
    if (base) return result;

    bool untrained = true;
    if (result == 0) { untrained = ski->untrained; }
    if (untrained) {
        result += get_ability_modifier(obj, ski->ability);
    }

    // Master Feats
    nw::kernel::resolve_master_feats<int>(obj, skill, adder,
        mfeat_skill_focus, mfeat_skill_focus_epic);

    // Modifiers
    nw::kernel::resolve_modifier(obj, mod_type_skill, skill, adder);

    // Effects
    auto begin = std::begin(obj->effects());
    auto end = std::end(obj->effects());

    auto [bonus, it] = nw::sum_effects_of<int>(begin, end,
        effect_type_skill_increase, *skill);

    auto [decrease, _] = nw::sum_effects_of<int>(it, end,
        effect_type_skill_decrease, *skill);

    auto [min, max] = nw::kernel::effects().effect_limits_ability();
    return result + std::clamp(bonus - decrease, min, max);
}

bool resolve_skill_check(const nw::Creature* obj, nw::Skill skill, int dc, nw::ObjectBase* versus)
{
    static constexpr nw::DiceRoll d20{1, 20};
    auto rank = get_skill_rank(obj, skill, versus);
    if (rank + 20 < dc) { return false; }
    if (rank + nw::roll_dice(d20) < dc) { return false; }
    return true;
}

} // namespace nwn1
