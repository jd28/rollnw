#include "../runtime.hpp"

#include "../../functions.hpp"
#include "../../kernel/Rules.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../profiles/nwn1/scriptapi.hpp"
#include "../../scriptapi.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace nw::smalls {

namespace {

constexpr int32_t attack_state_primary_window = 1 << 0;
constexpr int32_t attack_state_has_offhand_attacks = 1 << 1;
constexpr int32_t attack_state_has_onhand_weapon = 1 << 2;
constexpr int32_t attack_state_has_unarmed_weapon = 1 << 3;
constexpr int32_t attack_state_has_cweapon1 = 1 << 4;
constexpr int32_t attack_state_has_cweapon2 = 1 << 5;
constexpr int32_t attack_state_has_cweapon3 = 1 << 6;
constexpr int32_t attack_state_random_shift = 8;
constexpr uint8_t attack_data_handle_type = 128;
constexpr int32_t number_of_attacks_ab_mask = 0x0fff;
constexpr int32_t number_of_attacks_iter_shift = 12;
constexpr int32_t number_of_attacks_iter_mask = 0x000f;
constexpr int32_t number_of_attacks_has_offhand_weapon = 1 << 16;
constexpr int32_t dual_wield_has_pair = 1 << 0;
constexpr int32_t dual_wield_off_is_light = 1 << 1;
constexpr int32_t unarmed_damage_level_mask = 0x00ff;
constexpr int32_t unarmed_damage_big_flag = 1 << 8;

int32_t aggregate_values(const std::vector<int32_t>& values, int32_t stack_policy)
{
    if (values.empty()) {
        return 0;
    }

    switch (stack_policy) {
    default:
    case 0: {
        int32_t sum = 0;
        for (auto value : values) {
            sum += value;
        }
        return sum;
    }
    case 1: {
        int32_t best = values[0];
        for (auto value : values) {
            best = std::max(best, value);
        }
        return best;
    }
    case 2: {
        int32_t worst = values[0];
        for (auto value : values) {
            worst = std::min(worst, value);
        }
        return worst;
    }
    case 3: {
        int32_t best = 0;
        int32_t worst = 0;
        for (auto value : values) {
            if (value > best) {
                best = value;
            } else if (value < worst) {
                worst = value;
            }
        }
        return best + worst;
    }
    }
}

int32_t build_attack_type_inputs(nw::Creature* creature)
{
    static constexpr nw::DiceRoll d3{1, 3};

    if (!creature) {
        return 1 << attack_state_random_shift;
    }

    int32_t state = 0;

    if (creature->combat_info.attack_current < creature->combat_info.attacks_onhand + creature->combat_info.attacks_extra) {
        state |= attack_state_primary_window;
    }

    if (creature->combat_info.attacks_offhand > 0) {
        state |= attack_state_has_offhand_attacks;
    }

    if (nwn1::get_weapon_by_attack_type(creature, nwn1::attack_type_onhand)) {
        state |= attack_state_has_onhand_weapon;
    }
    if (nwn1::get_weapon_by_attack_type(creature, nwn1::attack_type_unarmed)) {
        state |= attack_state_has_unarmed_weapon;
    }
    if (nwn1::get_weapon_by_attack_type(creature, nwn1::attack_type_cweapon1)) {
        state |= attack_state_has_cweapon1;
    }
    if (nwn1::get_weapon_by_attack_type(creature, nwn1::attack_type_cweapon2)) {
        state |= attack_state_has_cweapon2;
    }
    if (nwn1::get_weapon_by_attack_type(creature, nwn1::attack_type_cweapon3)) {
        state |= attack_state_has_cweapon3;
    }

    state |= nw::roll_dice(d3) << attack_state_random_shift;
    return state;
}

nw::Creature* as_creature(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_creature();
}

const nw::ObjectBase* as_object_const(nw::ObjectHandle obj)
{
    return nw::kernel::objects().get_object_base(obj);
}

nw::ObjectBase* as_object_base(nw::ObjectHandle obj)
{
    return nw::kernel::objects().get_object_base(obj);
}

nw::Item* as_item(nw::ObjectHandle obj)
{
    auto* base = nw::kernel::objects().get_object_base(obj);
    if (!base) {
        return nullptr;
    }
    return base->as_item();
}

nw::Item* get_weapon_for_attack_type(nw::ObjectHandle obj, int32_t attack_type)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return nullptr;
    }
    return nwn1::get_weapon_by_attack_type(cre, nw::AttackType::make(attack_type));
}

int32_t weapon_critical_multiplier(nw::ObjectHandle obj, int32_t attack_type)
{
    auto* weapon = get_weapon_for_attack_type(obj, attack_type);
    if (!weapon) {
        return 2;
    }

    auto baseitem = nw::kernel::rules().baseitems.get(weapon->baseitem);
    if (!baseitem) {
        return 2;
    }

    return baseitem->crit_multiplier;
}

int32_t attack_bonus_effective_attack_type(nw::ObjectHandle obj, int32_t attack_type)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return *nwn1::attack_type_unarmed;
    }

    auto type = nw::AttackType::make(attack_type);
    auto* weapon = nwn1::get_weapon_by_attack_type(cre, type);
    if (!weapon) {
        return *nwn1::attack_type_unarmed;
    }
    return *type;
}

int32_t attack_bonus_dual_wield_penalty(nw::ObjectHandle obj, int32_t attack_type)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return 0;
    }

    auto [on, off] = nwn1::resolve_dual_wield_penalty(cre);
    auto type = nw::AttackType::make(attack_type);
    if (type == nwn1::attack_type_onhand) {
        return on;
    }
    if (type == nwn1::attack_type_offhand) {
        return off;
    }
    return 0;
}

int32_t attack_bonus_size_modifier(nw::ObjectHandle obj)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return 0;
    }
    return cre->combat_info.size_ab_modifier;
}

int32_t attack_bonus_modifier_sum(nw::ObjectHandle obj, int32_t attack_type, nw::ObjectHandle versus)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return 0;
    }

    int modifier = 0;
    auto mod_adder = [&modifier](int value) { modifier += value; };
    auto type = nw::AttackType::make(attack_type);
    auto* vs = as_object_const(versus);
    nw::kernel::resolve_modifier(cre, nwn1::mod_type_attack_bonus, type, vs, mod_adder);
    if (type != nwn1::attack_type_any) {
        nw::kernel::resolve_modifier(cre, nwn1::mod_type_attack_bonus, nwn1::attack_type_any, vs, mod_adder);
    }
    return modifier;
}

int32_t attack_bonus_effect_delta_clamped(nw::ObjectHandle obj, int32_t attack_type, nw::ObjectHandle versus)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return 0;
    }

    nw::Versus vs;
    if (auto* target = as_object_const(versus)) {
        vs = target->versus_me();
    }

    auto begin = std::begin(cre->effects());
    auto end = std::end(cre->effects());
    auto type = nw::AttackType::make(attack_type);

    int bonus = 0;
    auto bonus_adder = [&bonus](int mod) { bonus += mod; };
    auto it = nw::resolve_effects_of<int>(begin, end, nwn1::effect_type_attack_increase, *nwn1::attack_type_any, vs,
        bonus_adder, &nw::effect_extract_int0);

    if (type != nwn1::attack_type_any) {
        it = nw::resolve_effects_of<int>(it, end, nwn1::effect_type_attack_increase, *type, vs,
            bonus_adder, &nw::effect_extract_int0);
    }

    int decrease = 0;
    auto decrease_adder = [&decrease](int mod) { decrease += mod; };
    it = nw::resolve_effects_of<int>(it, end, nwn1::effect_type_attack_decrease, *nwn1::attack_type_any, vs,
        decrease_adder, &nw::effect_extract_int0);

    if (type != nwn1::attack_type_any) {
        nw::resolve_effects_of<int>(it, end, nwn1::effect_type_attack_decrease, *type, vs,
            decrease_adder, &nw::effect_extract_int0);
    }

    auto [min, max] = nw::kernel::effects().limits.attack;
    return std::clamp(bonus - decrease, min, max);
}

int32_t weapon_critical_threat(nw::ObjectHandle obj, int32_t attack_type)
{
    auto* weapon = get_weapon_for_attack_type(obj, attack_type);
    if (!weapon) {
        return 1;
    }

    auto baseitem = nw::kernel::rules().baseitems.get(weapon->baseitem);
    if (!baseitem) {
        return 1;
    }

    return baseitem->crit_threat;
}

bool weapon_has_keen(nw::ObjectHandle obj, int32_t attack_type)
{
    auto* weapon = get_weapon_for_attack_type(obj, attack_type);
    return nw::item_has_property(weapon, nwn1::ip_keen);
}

bool has_improved_critical(nw::ObjectHandle obj, int32_t attack_type)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return false;
    }

    auto* weapon = get_weapon_for_attack_type(obj, attack_type);
    auto base = nw::BaseItem::invalid();
    if (weapon) {
        base = weapon->baseitem;
    }

    return !!nw::kernel::resolve_master_feat<int>(cre, base, nwn1::mfeat_improved_crit);
}

int32_t attack_roll_armor_class(nw::ObjectHandle obj, nw::ObjectHandle versus)
{
    return nwn1::calculate_ac_versus(as_creature(obj), as_object_const(versus), false);
}

int32_t damage_immunity_modifier_bonus(nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus)
{
    auto* base = as_object_const(obj);
    if (!base) {
        return 0;
    }

    return nw::kernel::max_modifier<int>(base, nwn1::mod_type_dmg_immunity, nw::Damage::make(damage_type), as_object_const(versus));
}

int32_t damage_immunity_effect_delta(nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus)
{
    auto* base = as_object_const(obj);
    if (!base) {
        return 0;
    }

    nw::Versus vs;
    if (auto* target = as_object_const(versus)) {
        vs = target->versus_me();
    }

    auto begin = std::begin(base->effects());
    auto end = std::end(base->effects());
    auto [eff_bonus, _bonus_it] = nw::sum_effects_of<int>(begin, end, nwn1::effect_type_damage_immunity_increase,
        damage_type, vs);
    auto [eff_penalty, _penalty_it] = nw::sum_effects_of<int>(begin, end, nwn1::effect_type_damage_immunity_decrease,
        damage_type, vs);

    return eff_bonus - eff_penalty;
}

int32_t damage_reduction_modifier_bonus(nw::ObjectHandle obj, nw::ObjectHandle versus)
{
    auto* base = as_object_const(obj);
    if (!base) {
        return 0;
    }
    return nw::kernel::sum_modifier<int>(base, nwn1::mod_type_dmg_reduction, as_object_const(versus));
}

int32_t damage_resistance_modifier_bonus(nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus)
{
    auto* base = as_object_const(obj);
    if (!base) {
        return 0;
    }
    return nw::kernel::max_modifier<int>(base, nwn1::mod_type_dmg_resistance, nw::Damage::make(damage_type), as_object_const(versus));
}

int32_t object_effect_count(nw::ObjectHandle obj)
{
    auto* base = as_object_const(obj);
    if (!base) {
        return 0;
    }

    return static_cast<int32_t>(base->effects().size());
}

nw::TypedHandle object_effect_at(nw::ObjectHandle obj, int32_t index)
{
    auto* base = as_object_const(obj);
    if (!base || index < 0) {
        return {};
    }

    auto begin = std::begin(base->effects());
    auto end = std::end(base->effects());
    int32_t i = 0;
    for (auto it = begin; it != end; ++it, ++i) {
        if (i == index) {
            if (it->effect) {
                return it->effect->handle().to_typed_handle();
            }
            return {};
        }
    }

    return {};
}

int32_t attack_roll_concealment_data(nw::ObjectHandle obj, nw::ObjectHandle target, bool vs_ranged)
{
    auto [conceal, source] = nwn1::resolve_concealment(as_object_const(obj), as_object_const(target), vs_ranged);
    return conceal | (source ? (1 << 16) : 0);
}

int32_t attack_roll_d20()
{
    static constexpr nw::DiceRoll d20{1, 20, 0};
    return nw::roll_dice(d20);
}

int32_t attack_roll_d100()
{
    static constexpr nw::DiceRoll d100{1, 100};
    return nw::roll_dice(d100);
}

int32_t aggregate_effect_int(nw::ObjectHandle obj, int32_t effect_type, int32_t subtype,
    nw::ObjectHandle versus, int32_t int_index, int32_t stack_policy)
{
    auto* base = as_object_const(obj);
    if (!base || int_index < 0) {
        return 0;
    }

    nw::Versus vs;
    if (auto* target = as_object_const(versus)) {
        vs = target->versus_me();
    }

    auto type = nw::EffectType::make(effect_type);
    std::vector<int32_t> values;
    values.reserve(base->effects().size());

    for (const auto& ent : base->effects()) {
        if (ent.type != type || !ent.effect) {
            continue;
        }
        if (subtype >= 0 && ent.subtype != subtype) {
            continue;
        }
        if (!ent.effect->versus().match(vs)) {
            continue;
        }
        values.push_back(ent.effect->get_int(int_index));
    }

    return aggregate_values(values, stack_policy);
}

int32_t aggregate_modifier_int(nw::ObjectHandle obj, int32_t modifier_type, int32_t subtype,
    nw::ObjectHandle versus, int32_t stack_policy)
{
    auto* base = as_object_const(obj);
    if (!base) {
        return 0;
    }

    auto* vs = as_object_const(versus);
    auto type = nw::ModifierType::make(modifier_type);
    std::vector<int32_t> values;
    values.reserve(16);
    auto add = [&values](int value) {
        values.push_back(value);
    };

    if (subtype < 0) {
        nw::kernel::resolve_modifier(base, type, vs, add);
    } else {
        for (const auto& mod : nw::kernel::rules().modifiers) {
            if (mod.type != type) {
                continue;
            }
            if (mod.subtype != -1 && mod.subtype != subtype) {
                continue;
            }
            nw::kernel::resolve_modifier(base, mod, add, vs, subtype);
        }
    }

    return aggregate_values(values, stack_policy);
}

bool weapon_power_monk_applies(nw::ObjectHandle obj, nw::ObjectHandle weapon)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return false;
    }

    auto* item = as_item(weapon);
    bool is_monk_or_null = !item || nwn1::is_monk_weapon(item);
    auto [can_use, _level] = nwn1::can_use_monk_abilities(cre);
    return can_use && is_monk_or_null;
}

int32_t weapon_power_item_property_bonus(nw::ObjectHandle weapon)
{
    auto* item = as_item(weapon);
    if (!item) {
        return 0;
    }

    int32_t result = 0;
    for (const auto& ip : item->properties) {
        if (ip.type == *nwn1::ip_attack_bonus || ip.type == *nwn1::ip_enhancement_bonus) {
            result = std::max(result, static_cast<int32_t>(ip.cost_value));
        }
    }
    return result;
}

bool weapon_power_is_bow(nw::ObjectHandle weapon)
{
    auto* item = as_item(weapon);
    if (!item) {
        return false;
    }

    auto base = item->baseitem;
    return base == nwn1::base_item_longbow || base == nwn1::base_item_shortbow;
}

int32_t weapon_damage_specialization_bonus(nw::ObjectHandle obj, int32_t base_item)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return 0;
    }

    auto item = nw::BaseItem::make(base_item);
    if (!!nw::kernel::resolve_master_feat<int>(cre, item, nwn1::mfeat_weapon_spec_epic)) {
        return 8;
    }
    if (!!nw::kernel::resolve_master_feat<int>(cre, item, nwn1::mfeat_weapon_spec)) {
        return 4;
    }

    return 0;
}

bool weapon_damage_is_bow_base_item(int32_t base_item)
{
    auto item = nw::BaseItem::make(base_item);
    return item == nwn1::base_item_longbow || item == nwn1::base_item_shortbow;
}

int32_t weapon_base_damage(int32_t base_item)
{
    auto item = nw::BaseItem::make(base_item);
    auto bi = nw::kernel::rules().baseitems.get(item);
    if (!bi) {
        return 0;
    }

    int32_t dice = std::clamp(bi->base_damage.dice, 0, 255);
    int32_t sides = std::clamp(bi->base_damage.sides, 0, 255);
    int32_t bonus = std::clamp(bi->base_damage.bonus, -32768, 32767);
    return dice | (sides << 8) | ((bonus & 0xffff) << 16);
}

bool is_unarmed_weapon(nw::ObjectHandle weapon)
{
    return nwn1::is_unarmed_weapon(as_item(weapon));
}

int32_t weapon_base_item(nw::ObjectHandle weapon)
{
    auto* item = as_item(weapon);
    if (!item) {
        return *nw::BaseItem::invalid();
    }
    return *item->baseitem;
}

int32_t roll_dice_amount(int32_t dice, int32_t sides, int32_t bonus, int32_t multiplier)
{
    nw::DiceRoll roll{dice, sides, bonus};
    return nw::roll_dice(roll, multiplier);
}

int32_t weapon_damage_flags_mask(nw::ObjectHandle weapon)
{
    int32_t mask = 0;
    auto* item = as_item(weapon);
    if (!item) {
        return 1 << *nwn1::damage_type_bludgeoning;
    }

    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!bi) {
        return mask;
    }

    switch (bi->weapon_type) {
    default:
        break;
    case 1:
        mask |= 1 << *nwn1::damage_type_piercing;
        break;
    case 2:
        mask |= 1 << *nwn1::damage_type_bludgeoning;
        break;
    case 3:
        mask |= 1 << *nwn1::damage_type_slashing;
        break;
    case 4:
        mask |= 1 << *nwn1::damage_type_piercing;
        mask |= 1 << *nwn1::damage_type_slashing;
        break;
    case 5:
        mask |= 1 << *nwn1::damage_type_bludgeoning;
        mask |= 1 << *nwn1::damage_type_piercing;
        break;
    }

    return mask;
}

int32_t number_of_attacks_inputs(nw::ObjectHandle obj)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return 0;
    }

    auto item_on = nwn1::get_equipped_item(cre, nw::EquipIndex::righthand);
    int32_t iter = nwn1::weapon_iteration(cre, item_on);
    iter = std::clamp(iter, 0, number_of_attacks_iter_mask);

    int32_t state = std::clamp(nwn1::base_attack_bonus(cre), 0, number_of_attacks_ab_mask)
        | (iter << number_of_attacks_iter_shift);

    auto item_off = nwn1::get_equipped_item(cre, nw::EquipIndex::lefthand);
    if (item_off) {
        auto item_off_bi = nw::kernel::rules().baseitems.get(item_off->baseitem);
        if (item_off_bi && item_off_bi->weapon_type != 0) {
            state |= number_of_attacks_has_offhand_weapon;
        }
    }

    return state;
}

int32_t dual_wield_penalty_inputs(nw::ObjectHandle obj)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return 0;
    }

    auto rh = nwn1::get_equipped_item(cre, nw::EquipIndex::righthand);
    if (!rh) {
        return 0;
    }

    auto rh_bi = nw::kernel::rules().baseitems.get(rh->baseitem);
    if (!rh_bi || !rh_bi->weapon_type) {
        return 0;
    }

    bool off_is_light = false;
    if (!nwn1::is_double_sided_weapon(rh)) {
        auto lh = nwn1::get_equipped_item(cre, nw::EquipIndex::lefthand);
        if (!lh) {
            return 0;
        }

        auto lh_bi = nw::kernel::rules().baseitems.get(lh->baseitem);
        if (!lh_bi || !lh_bi->weapon_type) {
            return 0;
        }

        off_is_light = nwn1::is_light_weapon(cre, lh);
    } else {
        off_is_light = true;
    }

    int32_t state = dual_wield_has_pair;
    if (off_is_light) {
        state |= dual_wield_off_is_light;
    }

    return state;
}

int32_t unarmed_damage_inputs(nw::ObjectHandle obj)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return 0;
    }

    auto [_can_use, level] = nwn1::can_use_monk_abilities(cre);
    int32_t state = std::clamp(level, 0, unarmed_damage_level_mask);
    if (cre->size >= nwn1::creature_size_medium) {
        state |= unarmed_damage_big_flag;
    }

    return state;
}

struct ScriptAttackData {
    int32_t attack_type = -1;
    int32_t attack_result = -1;
    int32_t attack_roll = 0;
    int32_t attack_bonus = 0;
    int32_t armor_class = 0;
    int32_t nth_attack = 0;
    int32_t damage_total = 0;
    int32_t critical_multiplier = 0;
    int32_t critical_threat = 0;
    int32_t concealment = 0;
    int32_t iteration_penalty = 0;
    bool is_ranged = false;
    bool target_is_creature = false;
};

nw::TypedHandlePool& attack_data_pool()
{
    static nw::TypedHandlePool pool(sizeof(ScriptAttackData));
    return pool;
}

ScriptAttackData* get_attack_data(nw::TypedHandle h)
{
    if (!h.is_valid() || h.type != attack_data_handle_type) {
        return nullptr;
    }
    return static_cast<ScriptAttackData*>(attack_data_pool().get(h));
}

void destroy_attack_data(nw::TypedHandle h)
{
    if (!h.is_valid() || h.type != attack_data_handle_type) {
        return;
    }
    attack_data_pool().destroy(h);
}

} // namespace

void register_core_combat(Runtime& rt)
{
    if (rt.get_native_module("core.combat")) {
        return;
    }

    auto attack_data_type = rt.type_id("core.combat.AttackData", true);
    rt.register_handle_destructor(attack_data_type, +[](nw::TypedHandle h, HeapPtr) { destroy_attack_data(h); });

    rt.module("core.combat")
        .handle_type("AttackData", attack_data_handle_type)
        .function("_cpp_base_attack_bonus", +[](nw::ObjectHandle obj) -> int32_t { return nwn1::base_attack_bonus(as_creature(obj)); })
        .function("_cpp_is_flanked", +[](nw::ObjectHandle target, nw::ObjectHandle attacker) -> bool { return nwn1::is_flanked(as_creature(target), as_creature(attacker)); })
        .function("_cpp_resolve_attack_bonus", +[](nw::ObjectHandle obj, int32_t attack_type, nw::ObjectHandle versus) -> int32_t { return nwn1::resolve_attack_bonus(as_creature(obj), nw::AttackType::make(attack_type), as_object_const(versus)); })
        .function("_cpp_attack_bonus_effective_attack_type", +[](nw::ObjectHandle obj, int32_t attack_type) -> int32_t { return attack_bonus_effective_attack_type(obj, attack_type); })
        .function("_cpp_attack_bonus_dual_wield_penalty", +[](nw::ObjectHandle obj, int32_t attack_type) -> int32_t { return attack_bonus_dual_wield_penalty(obj, attack_type); })
        .function("_cpp_attack_bonus_size_modifier", +[](nw::ObjectHandle obj) -> int32_t { return attack_bonus_size_modifier(obj); })
        .function("_cpp_attack_bonus_modifier_sum", +[](nw::ObjectHandle obj, int32_t attack_type, nw::ObjectHandle versus) -> int32_t { return attack_bonus_modifier_sum(obj, attack_type, versus); })
        .function("_cpp_attack_bonus_effect_delta_clamped", +[](nw::ObjectHandle obj, int32_t attack_type, nw::ObjectHandle versus) -> int32_t { return attack_bonus_effect_delta_clamped(obj, attack_type, versus); })
        .function("_cpp_resolve_attack_roll", +[](nw::ObjectHandle obj, int32_t attack_type, nw::ObjectHandle versus) -> int32_t { return static_cast<int32_t>(nwn1::resolve_attack_roll(as_creature(obj), nw::AttackType::make(attack_type), as_object_const(versus), nullptr)); })
        .function("_cpp_resolve_attack_type", +[](nw::ObjectHandle obj) -> int32_t { return *nwn1::resolve_attack_type(as_creature(obj)); })
        .function("_cpp_attack_type_inputs", +[](nw::ObjectHandle obj) -> int32_t { return build_attack_type_inputs(as_creature(obj)); })
        .function("_cpp_resolve_critical_multiplier", +[](nw::ObjectHandle obj, int32_t attack_type, nw::ObjectHandle versus) -> int32_t { return nwn1::resolve_critical_multiplier(as_creature(obj), nw::AttackType::make(attack_type), as_object_const(versus)); })
        .function("_cpp_resolve_critical_threat", +[](nw::ObjectHandle obj, int32_t attack_type) -> int32_t { return nwn1::resolve_critical_threat(as_creature(obj), nw::AttackType::make(attack_type)); })
        .function("_cpp_weapon_critical_multiplier", +[](nw::ObjectHandle obj, int32_t attack_type) -> int32_t { return weapon_critical_multiplier(obj, attack_type); })
        .function("_cpp_weapon_critical_threat", +[](nw::ObjectHandle obj, int32_t attack_type) -> int32_t { return weapon_critical_threat(obj, attack_type); })
        .function("_cpp_weapon_has_keen", +[](nw::ObjectHandle obj, int32_t attack_type) -> bool { return weapon_has_keen(obj, attack_type); })
        .function("_cpp_has_improved_critical", +[](nw::ObjectHandle obj, int32_t attack_type) -> bool { return has_improved_critical(obj, attack_type); })
        .function("_cpp_attack_roll_armor_class", +[](nw::ObjectHandle obj, nw::ObjectHandle versus) -> int32_t { return attack_roll_armor_class(obj, versus); })
        .function("_cpp_attack_roll_concealment_data", +[](nw::ObjectHandle obj, nw::ObjectHandle target, bool vs_ranged) -> int32_t { return attack_roll_concealment_data(obj, target, vs_ranged); })
        .function("_cpp_attack_roll_d20", +[]() -> int32_t { return attack_roll_d20(); })
        .function("_cpp_attack_roll_d100", +[]() -> int32_t { return attack_roll_d100(); })
        .function("_cpp_aggregate_effect_int", +[](nw::ObjectHandle obj, int32_t effect_type, int32_t subtype, nw::ObjectHandle versus, int32_t int_index, int32_t stack_policy) -> int32_t { return aggregate_effect_int(obj, effect_type, subtype, versus, int_index, stack_policy); })
        .function("_cpp_aggregate_modifier_int", +[](nw::ObjectHandle obj, int32_t modifier_type, int32_t subtype, nw::ObjectHandle versus, int32_t stack_policy) -> int32_t { return aggregate_modifier_int(obj, modifier_type, subtype, versus, stack_policy); })
        .function("_cpp_damage_immunity_modifier_bonus", +[](nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus) -> int32_t { return damage_immunity_modifier_bonus(obj, damage_type, versus); })
        .function("_cpp_damage_immunity_effect_delta", +[](nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus) -> int32_t { return damage_immunity_effect_delta(obj, damage_type, versus); })
        .function("_cpp_damage_reduction_modifier_bonus", +[](nw::ObjectHandle obj, nw::ObjectHandle versus) -> int32_t { return damage_reduction_modifier_bonus(obj, versus); })
        .function("_cpp_damage_resistance_modifier_bonus", +[](nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus) -> int32_t { return damage_resistance_modifier_bonus(obj, damage_type, versus); })
        .function("_cpp_object_effect_count", +[](nw::ObjectHandle obj) -> int32_t { return object_effect_count(obj); })
        .function("_cpp_object_effect_at", +[](nw::ObjectHandle obj, int32_t index) -> nw::TypedHandle { return object_effect_at(obj, index); })
        .function("_cpp_resolve_damage_immunity", +[](nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus) -> int32_t { return nwn1::resolve_damage_immunity(as_object_const(obj), nw::Damage::make(damage_type), as_object_const(versus)); })
        .function("_cpp_weapon_power_monk_applies", +[](nw::ObjectHandle obj, nw::ObjectHandle weapon) -> bool { return weapon_power_monk_applies(obj, weapon); })
        .function("_cpp_weapon_power_item_property_bonus", +[](nw::ObjectHandle weapon) -> int32_t { return weapon_power_item_property_bonus(weapon); })
        .function("_cpp_weapon_power_is_bow", +[](nw::ObjectHandle weapon) -> bool { return weapon_power_is_bow(weapon); })
        .function("_cpp_weapon_damage_specialization_bonus", +[](nw::ObjectHandle obj, int32_t base_item) -> int32_t { return weapon_damage_specialization_bonus(obj, base_item); })
        .function("_cpp_weapon_damage_is_bow_base_item", +[](int32_t base_item) -> bool { return weapon_damage_is_bow_base_item(base_item); })
        .function("_cpp_weapon_base_damage", +[](int32_t base_item) -> int32_t { return weapon_base_damage(base_item); })
        .function("_cpp_weapon_damage_flags_mask", +[](nw::ObjectHandle weapon) -> int32_t { return weapon_damage_flags_mask(weapon); })
        .function("_cpp_unarmed_damage_inputs", +[](nw::ObjectHandle obj) -> int32_t { return unarmed_damage_inputs(obj); })
        .function("_cpp_is_creature_weapon", +[](nw::ObjectHandle weapon) -> bool { return nwn1::is_creature_weapon(as_item(weapon)); })
        .function("_cpp_is_unarmed_weapon", +[](nw::ObjectHandle weapon) -> bool { return is_unarmed_weapon(weapon); })
        .function("_cpp_weapon_base_item", +[](nw::ObjectHandle weapon) -> int32_t { return weapon_base_item(weapon); })
        .function("_cpp_roll_dice", +[](int32_t dice, int32_t sides, int32_t bonus, int32_t multiplier) -> int32_t { return roll_dice_amount(dice, sides, bonus, multiplier); })
        .function("_cpp_number_of_attacks_inputs", +[](nw::ObjectHandle obj) -> int32_t { return number_of_attacks_inputs(obj); })
        .function("_cpp_dual_wield_penalty_inputs", +[](nw::ObjectHandle obj) -> int32_t { return dual_wield_penalty_inputs(obj); })
        .function("_cpp_resolve_weapon_power", +[](nw::ObjectHandle obj, nw::ObjectHandle weapon) -> int32_t { return nwn1::resolve_weapon_power(as_creature(obj), as_item(weapon)); })
        .function("_cpp_is_ranged_attack", +[](nw::ObjectHandle obj, int32_t attack_type) -> bool {
            auto* cre = as_creature(obj);
            if (!cre) {
                return false;
            }
            auto* weapon = nwn1::get_weapon_by_attack_type(cre, nw::AttackType::make(attack_type));
            return nwn1::is_ranged_weapon(weapon); })
        .function("_cpp_attack_current", +[](nw::ObjectHandle obj) -> int32_t {
            auto* cre = as_creature(obj);
            return cre ? cre->combat_info.attack_current : 0; })
        .function("_cpp_object_is_creature", +[](nw::ObjectHandle obj) -> bool {
            auto* base = as_object_base(obj);
            return base && base->as_creature(); })
        .function("_cpp_object_is_pc", +[](nw::ObjectHandle obj) -> bool {
            auto* base = as_object_base(obj);
            if (!base) {
                return false;
            }
            if (auto* creature = base->as_creature()) {
                return creature->pc;
            }
            if (auto* player = base->as_player()) {
                return player->pc;
            }
            return false; })
        .function("_cpp_prepare_attack_round", +[](nw::ObjectHandle attacker) {
            auto* cre = as_creature(attacker);
            if (!cre) {
                return;
            }

            auto [onhand, offhand] = nwn1::resolve_number_of_attacks(cre);
            cre->combat_info.attacks_onhand = onhand;
            cre->combat_info.attacks_offhand = offhand;

            int total_attacks = cre->combat_info.attacks_onhand
                + cre->combat_info.attacks_offhand
                + cre->combat_info.attacks_extra;
            if (cre->combat_info.attack_current >= total_attacks) {
                cre->combat_info.attack_current = 0;
            } })
        .function("_cpp_advance_attack_round", +[](nw::ObjectHandle attacker) {
            auto* cre = as_creature(attacker);
            if (cre) {
                ++cre->combat_info.attack_current;
            } })
        .function("_cpp_attack_data_create", +[]() -> nw::TypedHandle {
            auto handle = attack_data_pool().allocate();
            handle.type = attack_data_handle_type;
            auto* out = get_attack_data(handle);
            if (!out) {
                destroy_attack_data(handle);
                return {};
            }
            *out = {};
            return handle; })
        .function("_cpp_resolve_attack", +[](nw::ObjectHandle attacker, nw::ObjectHandle target) -> nw::TypedHandle {
            auto attack = nwn1::resolve_attack(as_creature(attacker), as_object_base(target));
            if (!attack) {
                return {};
            }

            auto handle = attack_data_pool().allocate();
            handle.type = attack_data_handle_type;

            auto* out = get_attack_data(handle);
            if (!out) {
                destroy_attack_data(handle);
                return {};
            }

            out->attack_type = *attack->type;
            out->attack_result = static_cast<int32_t>(attack->result);
            out->attack_roll = attack->attack_roll;
            out->attack_bonus = attack->attack_bonus;
            out->armor_class = attack->armor_class;
            out->nth_attack = attack->nth_attack;
            out->damage_total = attack->damage_total;
            out->critical_multiplier = attack->multiplier;
            out->critical_threat = attack->threat_range;
            out->concealment = attack->concealment;
            out->iteration_penalty = attack->iteration_penalty;
            out->is_ranged = attack->is_ranged_attack;
            out->target_is_creature = attack->target_is_creature;
            return handle; })
        .function("_cpp_attack_data_is_valid", +[](nw::TypedHandle data) -> bool { return get_attack_data(data) != nullptr; })
        .function("_cpp_attack_data_attack_type", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->attack_type : -1; })
        .function("_cpp_attack_data_attack_result", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->attack_result : -1; })
        .function("_cpp_attack_data_attack_roll", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->attack_roll : 0; })
        .function("_cpp_attack_data_attack_bonus", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->attack_bonus : 0; })
        .function("_cpp_attack_data_armor_class", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->armor_class : 0; })
        .function("_cpp_attack_data_nth_attack", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->nth_attack : 0; })
        .function("_cpp_attack_data_damage_total", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->damage_total : 0; })
        .function("_cpp_attack_data_critical_multiplier", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->critical_multiplier : 0; })
        .function("_cpp_attack_data_critical_threat", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->critical_threat : 0; })
        .function("_cpp_attack_data_concealment", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->concealment : 0; })
        .function("_cpp_attack_data_iteration_penalty", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->iteration_penalty : 0; })
        .function("_cpp_attack_data_is_ranged", +[](nw::TypedHandle data) -> bool {
            auto* ad = get_attack_data(data);
            return ad ? ad->is_ranged : false; })
        .function("_cpp_attack_data_set_attack_type", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->attack_type = value;
            } })
        .function("_cpp_attack_data_set_attack_result", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->attack_result = value;
            } })
        .function("_cpp_attack_data_set_attack_roll", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->attack_roll = value;
            } })
        .function("_cpp_attack_data_set_attack_bonus", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->attack_bonus = value;
            } })
        .function("_cpp_attack_data_set_armor_class", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->armor_class = value;
            } })
        .function("_cpp_attack_data_set_nth_attack", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->nth_attack = value;
            } })
        .function("_cpp_attack_data_set_damage_total", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->damage_total = value;
            } })
        .function("_cpp_attack_data_set_critical_multiplier", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->critical_multiplier = value;
            } })
        .function("_cpp_attack_data_set_critical_threat", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->critical_threat = value;
            } })
        .function("_cpp_attack_data_set_concealment", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->concealment = value;
            } })
        .function("_cpp_attack_data_set_iteration_penalty", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->iteration_penalty = value;
            } })
        .function("_cpp_attack_data_set_is_ranged", +[](nw::TypedHandle data, bool value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->is_ranged = value;
            } })
        .function("_cpp_attack_data_target_is_creature", +[](nw::TypedHandle data) -> bool {
            auto* ad = get_attack_data(data);
            return ad ? ad->target_is_creature : false; })
        .function("_cpp_attack_data_set_target_is_creature", +[](nw::TypedHandle data, bool value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->target_is_creature = value;
            } })
        .finalize();
}

bool try_copy_attack_data(nw::TypedHandle handle,
    int32_t& attack_type, int32_t& attack_result, int32_t& attack_roll, int32_t& attack_bonus,
    int32_t& armor_class, int32_t& nth_attack, int32_t& damage_total, int32_t& critical_multiplier,
    int32_t& critical_threat, int32_t& concealment, int32_t& iteration_penalty,
    bool& is_ranged, bool& target_is_creature)
{
    auto* data = get_attack_data(handle);
    if (!data) {
        return false;
    }

    attack_type = data->attack_type;
    attack_result = data->attack_result;
    attack_roll = data->attack_roll;
    attack_bonus = data->attack_bonus;
    armor_class = data->armor_class;
    nth_attack = data->nth_attack;
    damage_total = data->damage_total;
    critical_multiplier = data->critical_multiplier;
    critical_threat = data->critical_threat;
    concealment = data->concealment;
    iteration_penalty = data->iteration_penalty;
    is_ranged = data->is_ranged;
    target_is_creature = data->target_is_creature;
    return true;
}

} // namespace nw::smalls
