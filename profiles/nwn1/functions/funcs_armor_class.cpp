#include "funcs_armor_class.hpp"

#include "../constants.hpp"
#include "../effects.hpp"
#include "../functions.hpp"

#include <nw/components/Creature.hpp>
#include <nw/components/Item.hpp>
#include <nw/functions.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TwoDACache.hpp>

namespace nwn1 {

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

    auto it_bon = std::begin(obj->effects());
    auto it_pen = std::begin(obj->effects());
    auto end = std::end(obj->effects());

    std::array<int, 5> results{};

    int dodge_bon = 0;
    auto dodge_bon_adder = [&dodge_bon](int value) { dodge_bon += value; };
    it_bon = nw::resolve_effects_of<int>(it_bon, end, effect_type_ac_increase, *ac_dodge,
        dodge_bon_adder, &nw::effect_extract_int0);

    int dodge_pen = 0;
    auto dodge_pen_adder = [&dodge_pen](int value) { dodge_pen += value; };
    it_pen = nw::resolve_effects_of<int>(it_pen, end, effect_type_ac_decrease, *ac_dodge,
        dodge_pen_adder, &nw::effect_extract_int0);

    if (is_touch_attack) {
        int bonus = 0;
        auto bonus_maxer = [&bonus](int value) { bonus = std::max(bonus, value); };
        it_bon = nw::resolve_effects_of<int>(it_bon, end, effect_type_ac_increase, *ac_deflection,
            bonus_maxer, &nw::effect_extract_int0);

        int penalty = 0;
        auto penalty_maxer = [&penalty](int value) { penalty = std::max(penalty, value); };
        it_pen = nw::resolve_effects_of<int>(it_pen, end, effect_type_ac_increase, *ac_deflection,
            penalty_maxer, &nw::effect_extract_int0);
        results[ac_deflection.idx()] = bonus - penalty;
    } else {
        for (auto type : {ac_natural, ac_armor, ac_shield, ac_deflection}) {
            int bonus = 0;
            auto bonus_maxer = [&bonus](int value) { bonus = std::max(bonus, value); };
            it_bon = nw::resolve_effects_of<int>(it_bon, end, effect_type_ac_increase, *type,
                bonus_maxer, &nw::effect_extract_int0);

            int penalty = 0;
            auto penalty_maxer = [&penalty](int value) { penalty = std::max(penalty, value); };
            it_pen = nw::resolve_effects_of<int>(it_pen, end, effect_type_ac_decrease, *type,
                penalty_maxer, &nw::effect_extract_int0);
            results[type.idx()] = bonus - penalty;
        }
    }
    auto [min, max] = nw::kernel::rules().armor_class_effect_limits();

    natural += std::clamp(results[ac_natural.idx()], min, max);
    int armor = std::clamp(results[ac_armor.idx()], min, max);
    int deflect = std::clamp(results[ac_deflection.idx()], min, max);
    int dodge = std::clamp(dodge_bon - dodge_pen, min, max);
    int shield = std::clamp(results[ac_shield.idx()], min, max);

    LOG_F(INFO, "Dodge: {}", dodge);

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

} // namespace nwn1
