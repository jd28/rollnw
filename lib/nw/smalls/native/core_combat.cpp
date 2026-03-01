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

constexpr uint8_t attack_data_handle_type = 128;

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

    const auto begin = std::begin(base->effects());
    const auto end = std::end(base->effects());
    if (begin + index >= end) {
        return {};
    }

    auto it = begin + index;
    if (it->effect) {
        return it->effect->handle().to_typed_handle();
    }

    return {};
}

int32_t object_find_first_effect_of(nw::ObjectHandle obj, int32_t effect_type, int32_t subtype, int32_t start_index)
{
    auto* base = as_object_const(obj);
    if (!base) { return -1; }

    if (start_index < 0) { start_index = 0; }
    auto begin = std::begin(base->effects());
    auto end = std::end(base->effects());

    if (begin + start_index >= end) {
        return -1;
    }

    auto it = find_first_effect_of(begin + start_index, end, EffectType::make(effect_type), subtype);
    if (it == end) { return -1; }
    return std::distance(begin, it);
}

nw::Effect* effect_best_damage_reduction(nw::ObjectHandle obj, int32_t power)
{
    auto* base = as_object_const(obj);
    if (!base || power <= 0) {
        return nullptr;
    }

    auto begin = std::begin(base->effects());
    auto end = std::end(base->effects());
    auto it = nw::find_first_effect_of(begin, end, nwn1::effect_type_damage_reduction);
    if (it == end) {
        return nullptr;
    }

    nw::Effect* best_eff = nullptr;
    int32_t best = 0;
    int32_t best_remain = 0;
    while (it != end && it->type == nwn1::effect_type_damage_reduction) {
        if (it->effect) {
            int32_t amount = it->effect->get_int(0);
            int32_t bypass = it->effect->get_int(1);
            int32_t remain = it->effect->get_int(2);
            if (bypass > power && (amount > best || (amount == best && remain > best_remain))) {
                best = amount;
                best_remain = remain;
                best_eff = it->effect;
            }
        }
        ++it;
    }

    return best_eff;
}

nw::Effect* effect_best_damage_resistance(nw::ObjectHandle obj, int32_t damage_type)
{
    auto* base = as_object_const(obj);
    if (!base) { return nullptr; }

    auto begin = std::begin(base->effects());
    auto end = std::end(base->effects());
    auto it = nw::find_first_effect_of(begin, end, nwn1::effect_type_damage_resistance);
    if (it == end) { return nullptr; }

    nw::Effect* best_eff = nullptr;
    int32_t best = 0;
    int32_t best_remain = 0;
    while (it != end && it->type == nwn1::effect_type_damage_resistance) {
        if (it->effect && it->effect->handle().subtype == damage_type) {
            int32_t amount = it->effect->get_int(0);
            int32_t remain = it->effect->get_int(1);
            if (amount > best || (amount == best && remain > best_remain)) {
                best = amount;
                best_remain = remain;
                best_eff = it->effect;
            }
        }
        ++it;
    }

    return best_eff;
}

int32_t attack_roll_concealment(nw::ObjectHandle obj, nw::ObjectHandle target, bool vs_ranged)
{
    auto [conceal, _source] = nwn1::resolve_concealment(as_object_const(obj), as_object_const(target), vs_ranged);
    return conceal;
}

bool attack_roll_concealment_is_miss_chance_source(nw::ObjectHandle obj, nw::ObjectHandle target, bool vs_ranged)
{
    auto [_conceal, source] = nwn1::resolve_concealment(as_object_const(obj), as_object_const(target), vs_ranged);
    return source;
}

int32_t roll_uniform_int(int32_t min, int32_t max)
{
    if (max < min) {
        return min;
    }

    nw::DiceRoll roll{1, max - min + 1, min - 1};
    return nw::roll_dice(roll);
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

int32_t weapon_base_damage_dice(int32_t base_item)
{
    auto item = nw::BaseItem::make(base_item);
    auto bi = nw::kernel::rules().baseitems.get(item);
    if (!bi) {
        return 0;
    }
    return bi->base_damage.dice;
}

int32_t weapon_base_damage_sides(int32_t base_item)
{
    auto item = nw::BaseItem::make(base_item);
    auto bi = nw::kernel::rules().baseitems.get(item);
    if (!bi) {
        return 0;
    }
    return bi->base_damage.sides;
}

int32_t weapon_base_damage_bonus(int32_t base_item)
{
    auto item = nw::BaseItem::make(base_item);
    auto bi = nw::kernel::rules().baseitems.get(item);
    if (!bi) {
        return 0;
    }
    return bi->base_damage.bonus;
}

int32_t roll_dice_amount(int32_t dice, int32_t sides, int32_t bonus, int32_t multiplier)
{
    nw::DiceRoll roll{dice, sides, bonus};
    return nw::roll_dice(roll, multiplier);
}

bool weapon_damage_has_bludgeoning(nw::ObjectHandle weapon)
{
    auto* item = as_item(weapon);
    if (!item) {
        return true;
    }

    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!bi) {
        return false;
    }

    switch (bi->weapon_type) {
    case 2:
    case 5:
        return true;
    default:
        return false;
    }
}

bool weapon_damage_has_piercing(nw::ObjectHandle weapon)
{
    auto* item = as_item(weapon);
    if (!item) {
        return false;
    }

    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!bi) {
        return false;
    }

    switch (bi->weapon_type) {
    case 1:
    case 4:
    case 5:
        return true;
    default:
        return false;
    }
}

bool weapon_damage_has_slashing(nw::ObjectHandle weapon)
{
    auto* item = as_item(weapon);
    if (!item) {
        return false;
    }

    auto bi = nw::kernel::rules().baseitems.get(item->baseitem);
    if (!bi) {
        return false;
    }

    switch (bi->weapon_type) {
    case 3:
    case 4:
        return true;
    default:
        return false;
    }
}

bool dual_wield_has_pair_flag(nw::ObjectHandle obj)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return false;
    }

    auto* rh = nwn1::get_equipped_item(cre, nw::EquipIndex::righthand);
    if (!rh) {
        return false;
    }

    auto rh_bi = nw::kernel::rules().baseitems.get(rh->baseitem);
    if (!rh_bi || !rh_bi->weapon_type) {
        return false;
    }

    if (nwn1::is_double_sided_weapon(rh)) {
        return true;
    }

    auto* lh = nwn1::get_equipped_item(cre, nw::EquipIndex::lefthand);
    if (!lh) {
        return false;
    }

    auto lh_bi = nw::kernel::rules().baseitems.get(lh->baseitem);
    return lh_bi && lh_bi->weapon_type;
}

bool dual_wield_off_is_light_flag(nw::ObjectHandle obj)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return false;
    }

    auto* rh = nwn1::get_equipped_item(cre, nw::EquipIndex::righthand);
    if (!rh) {
        return false;
    }

    auto rh_bi = nw::kernel::rules().baseitems.get(rh->baseitem);
    if (!rh_bi || !rh_bi->weapon_type) {
        return false;
    }

    if (nwn1::is_double_sided_weapon(rh)) {
        return true;
    }

    auto* lh = nwn1::get_equipped_item(cre, nw::EquipIndex::lefthand);
    if (!lh) {
        return false;
    }

    auto lh_bi = nw::kernel::rules().baseitems.get(lh->baseitem);
    if (!lh_bi || !lh_bi->weapon_type) {
        return false;
    }

    return nwn1::is_light_weapon(cre, lh);
}

int32_t unarmed_damage_level(nw::ObjectHandle obj)
{
    auto* cre = as_creature(obj);
    if (!cre) {
        return 0;
    }
    auto [_can_use, level] = nwn1::can_use_monk_abilities(cre);
    return level;
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
        .function("is_flanked", +[](nw::ObjectHandle target, nw::ObjectHandle attacker) -> bool { return nwn1::is_flanked(as_creature(target), as_creature(attacker)); })
        .function("attack_bonus_effect_delta_clamped", +[](nw::ObjectHandle obj, int32_t attack_type, nw::ObjectHandle versus) -> int32_t { return attack_bonus_effect_delta_clamped(obj, attack_type, versus); })
        .function("weapon_critical_multiplier", +[](nw::ObjectHandle obj, int32_t attack_type) -> int32_t { return weapon_critical_multiplier(obj, attack_type); })
        .function("weapon_critical_threat", +[](nw::ObjectHandle obj, int32_t attack_type) -> int32_t { return weapon_critical_threat(obj, attack_type); })
        .function("weapon_has_keen", +[](nw::ObjectHandle obj, int32_t attack_type) -> bool { return weapon_has_keen(obj, attack_type); })
        .function("has_improved_critical", +[](nw::ObjectHandle obj, int32_t attack_type) -> bool { return has_improved_critical(obj, attack_type); })
        .function("attack_roll_armor_class", +[](nw::ObjectHandle obj, nw::ObjectHandle versus) -> int32_t { return attack_roll_armor_class(obj, versus); })
        .function("attack_roll_concealment", +[](nw::ObjectHandle obj, nw::ObjectHandle target, bool vs_ranged) -> int32_t { return attack_roll_concealment(obj, target, vs_ranged); })
        .function("attack_roll_is_miss_chance_source", +[](nw::ObjectHandle obj, nw::ObjectHandle target, bool vs_ranged) -> bool { return attack_roll_concealment_is_miss_chance_source(obj, target, vs_ranged); })
        .function("roll_uniform_int", +[](int32_t min, int32_t max) -> int32_t { return roll_uniform_int(min, max); })
        .function("aggregate_effect_int", +[](nw::ObjectHandle obj, int32_t effect_type, int32_t subtype, nw::ObjectHandle versus, int32_t int_index, int32_t stack_policy) -> int32_t { return aggregate_effect_int(obj, effect_type, subtype, versus, int_index, stack_policy); })
        .function("aggregate_modifier_int", +[](nw::ObjectHandle obj, int32_t modifier_type, int32_t subtype, nw::ObjectHandle versus, int32_t stack_policy) -> int32_t { return aggregate_modifier_int(obj, modifier_type, subtype, versus, stack_policy); })
        .function("damage_immunity_modifier_bonus", +[](nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus) -> int32_t { return damage_immunity_modifier_bonus(obj, damage_type, versus); })
        .function("damage_immunity_effect_delta", +[](nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus) -> int32_t { return damage_immunity_effect_delta(obj, damage_type, versus); })
        .function("damage_reduction_modifier_bonus", +[](nw::ObjectHandle obj, nw::ObjectHandle versus) -> int32_t { return damage_reduction_modifier_bonus(obj, versus); })
        .function("damage_resistance_modifier_bonus", +[](nw::ObjectHandle obj, int32_t damage_type, nw::ObjectHandle versus) -> int32_t { return damage_resistance_modifier_bonus(obj, damage_type, versus); })
        .function("object_effect_count", +[](nw::ObjectHandle obj) -> int32_t { return object_effect_count(obj); })
        .function("object_effect_at", +[](nw::ObjectHandle obj, int32_t index) -> nw::TypedHandle { return object_effect_at(obj, index); })
        .function("object_find_first_effect_of", +[](nw::ObjectHandle obj, int32_t effect_type, int32_t subtype, int32_t start_index) -> int32_t { return object_find_first_effect_of(obj, effect_type, subtype, start_index); })
        .function("effect_best_damage_reduction", +[](nw::ObjectHandle obj, int32_t power) -> nw::TypedHandle {
            auto* eff = effect_best_damage_reduction(obj, power);
            return eff ? eff->handle().to_typed_handle() : nw::TypedHandle{}; })
        .function("effect_best_damage_resistance", +[](nw::ObjectHandle obj, int32_t damage_type) -> nw::TypedHandle {
            auto* eff = effect_best_damage_resistance(obj, damage_type);
            return eff ? eff->handle().to_typed_handle() : nw::TypedHandle{}; })
        .function("weapon_power_monk_applies", +[](nw::ObjectHandle obj, nw::ObjectHandle weapon) -> bool { return weapon_power_monk_applies(obj, weapon); })
        .function("weapon_power_item_property_bonus", +[](nw::ObjectHandle weapon) -> int32_t { return weapon_power_item_property_bonus(weapon); })
        .function("weapon_damage_specialization_bonus", +[](nw::ObjectHandle obj, int32_t base_item) -> int32_t { return weapon_damage_specialization_bonus(obj, base_item); })
        .function("weapon_base_damage_dice", +[](int32_t base_item) -> int32_t { return weapon_base_damage_dice(base_item); })
        .function("weapon_base_damage_sides", +[](int32_t base_item) -> int32_t { return weapon_base_damage_sides(base_item); })
        .function("weapon_base_damage_bonus", +[](int32_t base_item) -> int32_t { return weapon_base_damage_bonus(base_item); })
        .function("weapon_damage_has_bludgeoning", +[](nw::ObjectHandle weapon) -> bool { return weapon_damage_has_bludgeoning(weapon); })
        .function("weapon_damage_has_piercing", +[](nw::ObjectHandle weapon) -> bool { return weapon_damage_has_piercing(weapon); })
        .function("weapon_damage_has_slashing", +[](nw::ObjectHandle weapon) -> bool { return weapon_damage_has_slashing(weapon); })
        .function("unarmed_damage_level", +[](nw::ObjectHandle obj) -> int32_t { return unarmed_damage_level(obj); })
        .function("roll_dice", +[](int32_t dice, int32_t sides, int32_t bonus, int32_t multiplier) -> int32_t { return roll_dice_amount(dice, sides, bonus, multiplier); })
        .function("dual_wield_has_pair", +[](nw::ObjectHandle obj) -> bool { return dual_wield_has_pair_flag(obj); })
        .function("dual_wield_off_is_light", +[](nw::ObjectHandle obj) -> bool { return dual_wield_off_is_light_flag(obj); })
        .function("prepare_attack_round", +[](nw::ObjectHandle attacker) {
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
        .function("advance_attack_round", +[](nw::ObjectHandle attacker) {
            auto* cre = as_creature(attacker);
            if (cre) {
                ++cre->combat_info.attack_current;
            } })
        .function("attack_data_create", +[]() -> nw::TypedHandle {
            auto handle = attack_data_pool().allocate();
            handle.type = attack_data_handle_type;
            auto* out = get_attack_data(handle);
            if (!out) {
                destroy_attack_data(handle);
                return {};
            }
            *out = {};
            return handle; })
        .function("attack_data_apply_damage_total", +[](nw::ObjectHandle attacker, nw::ObjectHandle target, nw::TypedHandle data) -> int32_t {
            auto* cre = as_creature(attacker);
            auto* tgt = as_object_base(target);
            auto* ad = get_attack_data(data);
            if (!cre || !tgt || !ad) {
                return 0;
            }

            nw::AttackData native{};
            native.attacker = cre;
            native.target = tgt;
            native.type = nw::AttackType::make(ad->attack_type);
            native.weapon = nwn1::get_weapon_by_attack_type(cre, native.type);
            native.target_state = nwn1::resolve_target_state(cre, tgt);
            native.target_is_creature = ad->target_is_creature;
            native.is_ranged_attack = ad->is_ranged;
            native.nth_attack = ad->nth_attack;
            native.result = static_cast<nw::AttackResult>(ad->attack_result);
            native.attack_roll = ad->attack_roll;
            native.attack_bonus = ad->attack_bonus;
            native.armor_class = ad->armor_class;
            native.multiplier = ad->critical_multiplier;
            native.threat_range = ad->critical_threat;
            native.concealment = ad->concealment;
            native.iteration_penalty = ad->iteration_penalty;

            ad->damage_total = nwn1::resolve_attack_damage(cre, tgt, &native);
            return ad->damage_total; })
        .function("resolve_attack", +[](nw::ObjectHandle attacker, nw::ObjectHandle target) -> nw::TypedHandle {
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
        .function("attack_data_is_valid", +[](nw::TypedHandle data) -> bool { return get_attack_data(data) != nullptr; })
        .function("attack_data_attack_type", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->attack_type : -1; })
        .function("attack_data_attack_result", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->attack_result : -1; })
        .function("attack_data_attack_roll", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->attack_roll : 0; })
        .function("attack_data_attack_bonus", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->attack_bonus : 0; })
        .function("attack_data_armor_class", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->armor_class : 0; })
        .function("attack_data_nth_attack", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->nth_attack : 0; })
        .function("attack_data_damage_total", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->damage_total : 0; })
        .function("attack_data_critical_multiplier", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->critical_multiplier : 0; })
        .function("attack_data_critical_threat", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->critical_threat : 0; })
        .function("attack_data_concealment", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->concealment : 0; })
        .function("attack_data_iteration_penalty", +[](nw::TypedHandle data) -> int32_t {
            auto* ad = get_attack_data(data);
            return ad ? ad->iteration_penalty : 0; })
        .function("attack_data_is_ranged", +[](nw::TypedHandle data) -> bool {
            auto* ad = get_attack_data(data);
            return ad ? ad->is_ranged : false; })
        .function("attack_data_set_attack_type", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->attack_type = value;
            } })
        .function("attack_data_set_attack_result", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->attack_result = value;
            } })
        .function("attack_data_set_attack_roll", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->attack_roll = value;
            } })
        .function("attack_data_set_attack_bonus", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->attack_bonus = value;
            } })
        .function("attack_data_set_armor_class", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->armor_class = value;
            } })
        .function("attack_data_set_nth_attack", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->nth_attack = value;
            } })
        .function("attack_data_set_damage_total", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->damage_total = value;
            } })
        .function("attack_data_set_critical_multiplier", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->critical_multiplier = value;
            } })
        .function("attack_data_set_critical_threat", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->critical_threat = value;
            } })
        .function("attack_data_set_concealment", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->concealment = value;
            } })
        .function("attack_data_set_iteration_penalty", +[](nw::TypedHandle data, int32_t value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->iteration_penalty = value;
            } })
        .function("attack_data_set_is_ranged", +[](nw::TypedHandle data, bool value) {
            auto* ad = get_attack_data(data);
            if (ad) {
                ad->is_ranged = value;
            } })
        .function("attack_data_target_is_creature", +[](nw::TypedHandle data) -> bool {
            auto* ad = get_attack_data(data);
            return ad ? ad->target_is_creature : false; })
        .function("attack_data_set_target_is_creature", +[](nw::TypedHandle data, bool value) {
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
