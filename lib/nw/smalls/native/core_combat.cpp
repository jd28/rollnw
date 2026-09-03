#include "../runtime.hpp"

#include "../../functions.hpp"
#include "../../objects/Creature.hpp"
#include "../../objects/ObjectManager.hpp"
#include "../../rules/Dice.hpp"
#include "../../rules/combat.hpp"

#include <algorithm>
#include <limits>

namespace nw::smalls {

namespace {

const nw::ObjectBase* as_object_const(nw::ObjectHandle obj);

nw::Versus make_versus(int32_t race, int32_t align_flags) noexcept
{
    constexpr int32_t valid_align_flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x10;
    nw::Versus vs;
    if (race >= 0) {
        vs.race = nw::Race::make(race);
    }
    vs.align_flags = static_cast<nw::AlignmentFlags>(align_flags & valid_align_flags);
    return vs;
}

int32_t damage_immunity_effect_delta_uncached(const nw::ObjectBase* base, nw::EffectType increase_type,
    nw::EffectType decrease_type, int32_t damage_type, nw::Versus vs)
{
    if (!base || increase_type == nw::EffectType::invalid() || decrease_type == nw::EffectType::invalid()) {
        return 0;
    }

    auto begin = std::begin(base->effects());
    auto end = std::end(base->effects());
    auto [eff_bonus, _bonus_it] = nw::sum_effects_of<int>(begin, end, increase_type, damage_type, vs);
    auto [eff_penalty, _penalty_it] = nw::sum_effects_of<int>(begin, end, decrease_type, damage_type, vs);

    return eff_bonus - eff_penalty;
}

nw::Effect* effect_best_damage_reduction_uncached(const nw::ObjectBase* base, nw::EffectType effect_type, int32_t power)
{
    if (!base || effect_type == nw::EffectType::invalid() || power <= 0) {
        return nullptr;
    }

    auto begin = std::begin(base->effects());
    auto end = std::end(base->effects());
    auto it = nw::find_first_effect_of(begin, end, effect_type);
    if (it == end) {
        return nullptr;
    }

    nw::Effect* best_eff = nullptr;
    int32_t best = 0;
    int32_t best_remain = 0;
    while (it != end && it->type == effect_type) {
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

nw::Effect* effect_best_damage_resistance_uncached(const nw::ObjectBase* base, nw::EffectType effect_type,
    int32_t damage_type)
{
    if (!base || effect_type == nw::EffectType::invalid()) { return nullptr; }

    auto begin = std::begin(base->effects());
    auto end = std::end(base->effects());
    auto it = nw::find_first_effect_of(begin, end, effect_type);
    if (it == end) { return nullptr; }

    nw::Effect* best_eff = nullptr;
    int32_t best = 0;
    int32_t best_remain = 0;
    while (it != end && it->type == effect_type) {
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

struct EffectFolder {
    int32_t policy;
    int32_t acc = 0;   // sum / best-max / best-min / good-stack-best
    int32_t worst = 0; // policy 3: worst (most negative) value seen
    bool seen = false; // policy 1 and 2: first-value flag

    void add(int32_t v) noexcept
    {
        switch (policy) {
        default:
            break;
        case 0:
            acc += v;
            break;
        case 1:
            acc = seen ? std::max(acc, v) : v;
            seen = true;
            break;
        case 2:
            acc = seen ? std::min(acc, v) : v;
            seen = true;
            break;
        case 3:
            if (v > acc)
                acc = v;
            else if (v < worst)
                worst = v;
            break;
        }
    }

    int32_t result() const noexcept { return acc + (policy == 3 ? worst : 0); }
};

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

[[maybe_unused]] nw::ObjectBase* as_object_base(nw::ObjectHandle obj)
{
    return nw::kernel::objects().get_object_base(obj);
}

int32_t attack_bonus_effect_delta(nw::ObjectHandle obj, int32_t attack_type, int32_t any_attack_type,
    nw::EffectType increase_type, nw::EffectType decrease_type, nw::Versus vs)
{
    auto* cre = as_creature(obj);
    if (!cre || attack_type < 0 || any_attack_type < 0
        || increase_type == nw::EffectType::invalid() || decrease_type == nw::EffectType::invalid()) {
        return 0;
    }

    auto begin = std::begin(cre->effects());
    auto end = std::end(cre->effects());
    auto type = nw::AttackType::make(attack_type);
    auto any_type = nw::AttackType::make(any_attack_type);

    int bonus = 0;
    auto bonus_adder = [&bonus](int mod) { bonus += mod; };
    auto it = nw::resolve_effects_of<int>(begin, end, increase_type, *any_type, vs,
        bonus_adder, &nw::effect_extract_int0);

    if (type != any_type) {
        it = nw::resolve_effects_of<int>(it, end, increase_type, *type, vs,
            bonus_adder, &nw::effect_extract_int0);
    }

    int decrease = 0;
    auto decrease_adder = [&decrease](int mod) { decrease += mod; };
    it = nw::resolve_effects_of<int>(it, end, decrease_type, *any_type, vs,
        decrease_adder, &nw::effect_extract_int0);

    if (type != any_type) {
        nw::resolve_effects_of<int>(it, end, decrease_type, *type, vs,
            decrease_adder, &nw::effect_extract_int0);
    }

    return bonus - decrease;
}

int32_t damage_immunity_effect_delta(nw::ObjectHandle obj, nw::EffectType increase_type,
    nw::EffectType decrease_type, int32_t damage_type, nw::Versus vs)
{
    auto* base = as_object_const(obj);
    if (!base) {
        return 0;
    }

    return damage_immunity_effect_delta_uncached(base, increase_type, decrease_type, damage_type, vs);
}

int32_t object_effect_count(nw::ObjectHandle obj)
{
    auto* base = as_object_const(obj);
    if (!base) {
        return 0;
    }

    const size_t count = base->effects().size();
    return count <= static_cast<size_t>(std::numeric_limits<int32_t>::max())
        ? static_cast<int32_t>(count)
        : std::numeric_limits<int32_t>::max();
}

nw::TypedHandle object_effect_at(nw::ObjectHandle obj, int32_t index)
{
    auto* base = as_object_const(obj);
    if (!base || index < 0) {
        return {};
    }

    const auto& effects = base->effects();
    if (static_cast<size_t>(index) >= effects.size()) {
        return {};
    }

    auto it = std::begin(effects) + index;
    if (it->effect) {
        return it->effect->handle().to_typed_handle();
    }

    return {};
}

int32_t object_find_first_effect_of(nw::ObjectHandle obj, int32_t effect_type, int32_t subtype, int32_t start_index)
{
    auto* base = as_object_const(obj);
    if (!base) { return -1; }

    const auto& effects = base->effects();
    const size_t start = start_index > 0
        ? static_cast<size_t>(start_index)
        : 0;
    if (start >= effects.size()) { return -1; }

    auto begin = std::begin(effects);
    auto end = std::end(effects);
    auto it = find_first_effect_of(begin + start, end,
        EffectType::make(effect_type), subtype);
    if (it == end) { return -1; }

    const auto index = std::distance(begin, it);
    return index <= std::numeric_limits<int32_t>::max()
        ? static_cast<int32_t>(index)
        : -1;
}

nw::Effect* effect_best_damage_reduction(nw::ObjectHandle obj, nw::EffectType effect_type, int32_t power)
{
    auto* base = as_object_const(obj);
    if (!base || power <= 0) {
        return nullptr;
    }

    return effect_best_damage_reduction_uncached(base, effect_type, power);
}

nw::Effect* effect_best_damage_resistance(nw::ObjectHandle obj, nw::EffectType effect_type, int32_t damage_type)
{
    auto* base = as_object_const(obj);
    if (!base) { return nullptr; }

    return effect_best_damage_resistance_uncached(base, effect_type, damage_type);
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
    nw::Versus vs, int32_t int_index, int32_t stack_policy)
{
    auto* base = as_object_const(obj);
    if (!base || effect_type < 0 || int_index < 0
        || int_index >= nw::Effect::ints_count || stack_policy < 0 || stack_policy > 3) {
        return 0;
    }

    auto type = nw::EffectType::make(effect_type);
    EffectFolder folder{stack_policy};

    for (const auto& ent : base->effects()) {
        if (ent.type != type || !ent.effect) continue;
        if (subtype >= 0 && ent.subtype != subtype) continue;
        if (!ent.effect->versus().match(vs)) continue;
        folder.add(ent.effect->get_int(int_index));
    }

    return folder.result();
}

int32_t aggregate_effect_int_filtered(nw::ObjectHandle obj, int32_t effect_type, int32_t subtype,
    nw::Versus vs, int32_t filter_int_index, int32_t filter_int_value,
    int32_t int_index, int32_t stack_policy)
{
    auto* base = as_object_const(obj);
    if (!base || effect_type < 0 || int_index < 0 || int_index >= nw::Effect::ints_count
        || filter_int_index < 0 || filter_int_index >= nw::Effect::ints_count
        || stack_policy < 0 || stack_policy > 3) {
        return 0;
    }

    auto type = nw::EffectType::make(effect_type);
    EffectFolder folder{stack_policy};

    for (const auto& ent : base->effects()) {
        if (ent.type != type || !ent.effect) continue;
        if (ent.subtype != subtype) continue;
        if (!ent.effect->versus().match(vs)) continue;
        if (ent.effect->get_int(filter_int_index) != filter_int_value) continue;
        folder.add(ent.effect->get_int(int_index));
    }

    return folder.result();
}

int32_t roll_dice_amount(int32_t dice, int32_t sides, int32_t bonus, int32_t multiplier)
{
    nw::DiceRoll roll{dice, sides, bonus};
    return nw::roll_dice(roll, multiplier);
}

[[maybe_unused]] int32_t object_id(nw::ObjectHandle obj)
{
    return static_cast<int32_t>(static_cast<uint32_t>(obj.id));
}

int32_t creature_effect_version(nw::ObjectHandle obj)
{
    auto* cre = as_creature(obj);
    return cre ? static_cast<int32_t>(cre->effects().effect_version) : 0;
}

int32_t creature_equip_version(nw::ObjectHandle obj)
{
    auto* cre = as_creature(obj);
    return cre ? static_cast<int32_t>(cre->equipment.equip_version) : 0;
}

} // namespace

void register_core_combat(Runtime& rt)
{
    if (rt.get_native_module("core.combat")) {
        return;
    }

    rt.module("core.combat")
        .function("attack_bonus_effect_delta", +[](nw::ObjectHandle obj, int32_t attack_type, int32_t any_attack_type, int32_t increase_type, int32_t decrease_type, int32_t versus_race, int32_t versus_alignment_flags) -> int32_t { return attack_bonus_effect_delta(obj, attack_type, any_attack_type, nw::EffectType::make(increase_type), nw::EffectType::make(decrease_type), make_versus(versus_race, versus_alignment_flags)); })
        .function("roll_uniform_int", +[](int32_t min, int32_t max) -> int32_t { return roll_uniform_int(min, max); })
        .function("aggregate_effect_int", +[](nw::ObjectHandle obj, int32_t effect_type, int32_t subtype, int32_t versus_race, int32_t versus_alignment_flags, int32_t int_index, int32_t stack_policy) -> int32_t { return aggregate_effect_int(obj, effect_type, subtype, make_versus(versus_race, versus_alignment_flags), int_index, stack_policy); })
        .function("aggregate_effect_int_filtered", +[](nw::ObjectHandle obj, int32_t effect_type, int32_t subtype, int32_t versus_race, int32_t versus_alignment_flags, int32_t filter_int_index, int32_t filter_int_value, int32_t int_index, int32_t stack_policy) -> int32_t { return aggregate_effect_int_filtered(obj, effect_type, subtype, make_versus(versus_race, versus_alignment_flags), filter_int_index, filter_int_value, int_index, stack_policy); })
        .function("damage_immunity_effect_delta", +[](nw::ObjectHandle obj, int32_t increase_type, int32_t decrease_type, int32_t damage_type, int32_t versus_race, int32_t versus_alignment_flags) -> int32_t { return damage_immunity_effect_delta(obj, nw::EffectType::make(increase_type), nw::EffectType::make(decrease_type), damage_type, make_versus(versus_race, versus_alignment_flags)); })
        .function("object_effect_count", +[](nw::ObjectHandle obj) -> int32_t { return object_effect_count(obj); })
        .function("object_effect_at", +[](nw::ObjectHandle obj, int32_t index) -> nw::TypedHandle { return object_effect_at(obj, index); })
        .function("object_find_first_effect_of", +[](nw::ObjectHandle obj, int32_t effect_type, int32_t subtype, int32_t start_index) -> int32_t { return object_find_first_effect_of(obj, effect_type, subtype, start_index); })
        .function("effect_best_damage_reduction", +[](nw::ObjectHandle obj, int32_t effect_type, int32_t power) -> nw::TypedHandle {
            auto* eff = effect_best_damage_reduction(obj, nw::EffectType::make(effect_type), power);
            return eff ? eff->handle().to_typed_handle() : nw::TypedHandle{}; })
        .function("effect_best_damage_resistance", +[](nw::ObjectHandle obj, int32_t effect_type, int32_t damage_type) -> nw::TypedHandle {
            auto* eff = effect_best_damage_resistance(obj, nw::EffectType::make(effect_type), damage_type);
            return eff ? eff->handle().to_typed_handle() : nw::TypedHandle{}; })
        .function("roll_dice", +[](int32_t dice, int32_t sides, int32_t bonus, int32_t multiplier) -> int32_t { return roll_dice_amount(dice, sides, bonus, multiplier); })
        .function("creature_effect_version", +[](nw::ObjectHandle obj) -> int32_t { return creature_effect_version(obj); })
        .function("creature_equip_version", +[](nw::ObjectHandle obj) -> int32_t { return creature_equip_version(obj); })
        .finalize();
}

} // namespace nw::smalls
