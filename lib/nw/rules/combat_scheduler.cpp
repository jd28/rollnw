#include "combat_scheduler.hpp"

#include "../kernel/Config.hpp"
#include "../kernel/EventSystem.hpp"
#include "../kernel/Kernel.hpp"
#include "../objects/Creature.hpp"
#include "../objects/ObjectManager.hpp"
#include "../smalls/Array.hpp"
#include "../smalls/Bytecode.hpp"
#include "../smalls/Smalls.hpp"
#include "../smalls/runtime.hpp"
#include "combat.hpp"
#include "effects.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace nw::combat {
namespace {

bool is_int_compatible_type(const smalls::Runtime& runtime, smalls::TypeID type_id) noexcept
{
    while (true) {
        const auto* type = runtime.get_type(type_id);
        if (!type) { return false; }
        if (type->type_kind != smalls::TK_alias
            && type->type_kind != smalls::TK_newtype) {
            return type->primitive_kind == smalls::PK_int;
        }
        if (!type->type_params[0].is<smalls::TypeID>()) { return false; }
        type_id = type->type_params[0].as<smalls::TypeID>();
    }
}

bool is_array_type(const smalls::Runtime& runtime, smalls::TypeID type_id) noexcept
{
    const auto* type = runtime.get_type(type_id);
    return type && type->type_kind == smalls::TK_array;
}

const smalls::StructDef* get_value_struct_def(
    smalls::Runtime& runtime, smalls::TypeID type_id)
{
    const auto* type = runtime.get_type(type_id);
    if (!type || type->type_kind != smalls::TK_struct || !type->type_params[0].is<smalls::StructID>()) {
        return nullptr;
    }

    auto struct_id = type->type_params[0].as<smalls::StructID>();
    const auto* result = runtime.type_table_.get(struct_id);
    return result && result->is_value_type ? result : nullptr;
}

smalls::Value make_object_arg(smalls::Runtime& runtime, ObjectHandle handle)
{
    auto result = smalls::Value::make_object(handle);
    result.type_id = runtime.object_subtype_for_tag(handle.type);
    return result;
}

void read_effects_apply_at_offset(smalls::Runtime& rt, const smalls::Value& data,
    uint32_t offset, smalls::TypeID type_id,
    absl::InlinedVector<Effect*, 8>& out)
{
    out.clear();
    if (offset == UINT32_MAX) { return; }
    auto value = rt.read_value_field_at_offset(data, offset, type_id);
    if (!value.type_id.value) { return; }
    auto* values = smalls::detail::value_cast<smalls::IArray*>(&rt, value);
    if (!values) { return; }
    out.reserve(values->size());
    smalls::Value elem;
    for (size_t i = 0; i < values->size(); ++i) {
        if (!values->get_value(i, elem, rt)) { continue; }
        auto handle = smalls::detail::value_cast<TypedHandle>(&rt, elem);
        if (!handle.is_valid()) { continue; }
        if (auto* effect = kernel::effects().get(handle)) {
            out.push_back(effect);
            kernel::runtime().set_handle_ownership(handle, smalls::OwnershipMode::ENGINE_OWNED);
        }
    }
}

void read_effects_remove_at_offset(smalls::Runtime& rt, const smalls::Value& data,
    uint32_t offset, smalls::TypeID type_id,
    absl::InlinedVector<EffectHandle, 8>& out)
{
    out.clear();
    if (offset == UINT32_MAX) { return; }
    auto value = rt.read_value_field_at_offset(data, offset, type_id);
    if (!value.type_id.value) { return; }
    auto* values = smalls::detail::value_cast<smalls::IArray*>(&rt, value);
    if (!values) { return; }
    out.reserve(values->size());
    smalls::Value elem;
    for (size_t i = 0; i < values->size(); ++i) {
        if (!values->get_value(i, elem, rt)) { continue; }
        auto handle = smalls::detail::value_cast<TypedHandle>(&rt, elem);
        if (!handle.is_valid()) { continue; }
        if (auto* effect = kernel::effects().get(handle)) {
            out.push_back(effect->handle());
        }
    }
}

struct AttackDataFieldCache {
    uint32_t offset = UINT32_MAX;
    smalls::TypeID type_id;
};

struct AttackDataOffsetCache {
    bool valid = false;
    AttackDataFieldCache attack_type;
    AttackDataFieldCache attack_result;
    AttackDataFieldCache attack_roll;
    AttackDataFieldCache attack_bonus;
    AttackDataFieldCache armor_class;
    AttackDataFieldCache nth_attack;
    AttackDataFieldCache damage_total;
    AttackDataFieldCache critical_multiplier;
    AttackDataFieldCache critical_threat;
    AttackDataFieldCache concealment;
    AttackDataFieldCache iteration_penalty;
    AttackDataFieldCache is_ranged;
    AttackDataFieldCache target_is_creature;
    AttackDataFieldCache effects_to_apply;
    AttackDataFieldCache effects_to_remove;
};

struct CombatPolicyCache {
    uint64_t service_generation = 0;
    String module;
    smalls::BytecodeModule* bytecode_module = nullptr;
    const smalls::CompiledFunction* resolve_attack = nullptr;
    const smalls::CompiledFunction* resolve_attack_cooldown_ticks = nullptr;
    AttackDataOffsetCache offsets;
};

thread_local CombatPolicyCache combat_policy;

bool combat_policy_is_current() noexcept
{
    return combat_policy.bytecode_module
        && combat_policy.service_generation == kernel::services().generation()
        && combat_policy.module == kernel::config().combat_policy_module();
}

bool function_has_parameters(smalls::Runtime& runtime,
    const smalls::CompiledFunction* function,
    std::span<const smalls::TypeID> parameters)
{
    if (!function || function->param_count != parameters.size()) { return false; }
    for (size_t index = 0; index < parameters.size(); ++index) {
        if (runtime.get_function_param_type(function->function_type, index)
            != parameters[index]) {
            return false;
        }
    }
    return true;
}

bool resolve_attack_data_offsets(smalls::Runtime& runtime,
    smalls::TypeID result_type, AttackDataOffsetCache& result)
{
    const auto* definition = get_value_struct_def(runtime, result_type);
    if (!definition) { return false; }

    auto resolve = [&](StringView name,
                       bool (*accepts)(const smalls::Runtime&, smalls::TypeID)) {
        AttackDataFieldCache field;
        const uint32_t index = definition->field_index(name);
        if (index == UINT32_MAX) { return field; }
        const auto& source = definition->fields[index];
        if (!accepts(runtime, source.type_id)) { return field; }
        field.offset = source.offset;
        field.type_id = source.type_id;
        return field;
    };
    auto accepts_int = [](const smalls::Runtime& candidate_runtime,
                           smalls::TypeID type_id) {
        return is_int_compatible_type(candidate_runtime, type_id);
    };
    auto accepts_bool = [](const smalls::Runtime& candidate_runtime,
                            smalls::TypeID type_id) {
        return type_id == candidate_runtime.bool_type();
    };

    result.attack_type = resolve("attack_type", accepts_int);
    result.attack_result = resolve("attack_result", accepts_int);
    result.attack_roll = resolve("attack_roll", accepts_int);
    result.attack_bonus = resolve("attack_bonus", accepts_int);
    result.armor_class = resolve("armor_class", accepts_int);
    result.nth_attack = resolve("nth_attack", accepts_int);
    result.damage_total = resolve("damage_total", accepts_int);
    result.critical_multiplier = resolve("critical_multiplier", accepts_int);
    result.critical_threat = resolve("critical_threat", accepts_int);
    result.concealment = resolve("concealment", accepts_int);
    result.iteration_penalty = resolve("iteration_penalty", accepts_int);
    result.is_ranged = resolve("is_ranged", accepts_bool);
    result.target_is_creature = resolve("target_is_creature", accepts_bool);
    result.effects_to_apply = resolve("effects_to_apply", is_array_type);
    result.effects_to_remove = resolve("effects_to_remove", is_array_type);

    const std::array required{
        result.attack_type,
        result.attack_result,
        result.attack_roll,
        result.attack_bonus,
        result.armor_class,
        result.nth_attack,
        result.damage_total,
        result.critical_multiplier,
        result.critical_threat,
        result.concealment,
        result.iteration_penalty,
        result.is_ranged,
        result.target_is_creature,
        result.effects_to_apply,
        result.effects_to_remove,
    };
    result.valid = std::ranges::all_of(required,
        [](const AttackDataFieldCache& field) {
            return field.offset != UINT32_MAX;
        });
    return result.valid;
}

struct ScheduledAttackEvent {
    ObjectHandle attacker;
    ObjectHandle target;
};

struct AutoAttackEvent {
    ObjectHandle attacker;
    uint64_t generation;
};

struct AutoAttackState {
    ObjectHandle attacker;
    ObjectHandle target;
    uint64_t generation = 0;
    uint32_t round_ticks = 60;
    bool active = false;
};

thread_local std::vector<AutoAttackState> auto_attack_states;
thread_local uint64_t auto_attack_service_generation = 0;

void sync_auto_attack_service_generation()
{
    const uint64_t generation = kernel::services().generation();
    if (auto_attack_service_generation != generation) {
        auto_attack_states.clear();
        auto_attack_service_generation = generation;
    }
}

AutoAttackState* find_auto_attack_state(ObjectHandle attacker)
{
    sync_auto_attack_service_generation();
    for (auto& state : auto_attack_states) {
        if (state.attacker == attacker) {
            return &state;
        }
    }
    return nullptr;
}

AutoAttackState& upsert_auto_attack_state(ObjectHandle attacker)
{
    if (auto* state = find_auto_attack_state(attacker)) {
        return *state;
    }

    auto_attack_states.push_back(AutoAttackState{.attacker = attacker});
    return auto_attack_states.back();
}

void scheduled_attack_payload_delete(void* data)
{
    delete static_cast<ScheduledAttackEvent*>(data);
}

void auto_attack_payload_delete(void* data)
{
    delete static_cast<AutoAttackEvent*>(data);
}

bool creature_is_dead(const Creature& cre) noexcept
{
    if (const auto* vitals = kernel::objects().components().find_vitals(cre.handle())) {
        return vitals->hp_current <= 0;
    }
    return false;
}

void scheduled_attack_event_callback(const kernel::EventHandle& ev)
{
    auto* payload = static_cast<ScheduledAttackEvent*>(ev.data);
    if (!payload) {
        return;
    }

    auto* attacker = kernel::objects().get<Creature>(payload->attacker);
    auto* target = kernel::objects().get_object_base(payload->target);
    if (!attacker || !target) {
        return;
    }

    combat::resolve_attack(attacker, target);
}

void auto_attack_event_callback(const kernel::EventHandle& ev)
{
    auto* payload = static_cast<AutoAttackEvent*>(ev.data);
    if (!payload) {
        return;
    }

    auto* attacker = kernel::objects().get<Creature>(payload->attacker);
    auto* state = find_auto_attack_state(payload->attacker);
    if (!attacker || !state || !state->active || state->generation != payload->generation) {
        return;
    }

    auto* target = kernel::objects().get_object_base(state->target);
    if (!target) {
        state->active = false;
        return;
    }

    if (auto* cre = target->as_creature(); cre && creature_is_dead(*cre)) {
        state->active = false;
        return;
    }

    combat::resolve_attack(attacker, target);

    auto delay = combat::resolve_attack_cooldown_ticks(attacker, state->round_ticks);
    auto* next = new AutoAttackEvent{.attacker = payload->attacker, .generation = payload->generation};
    kernel::events().add_custom(payload->attacker, &auto_attack_event_callback, delay,
        next, &auto_attack_payload_delete);
}

} // namespace

bool initialize_policy(smalls::Runtime& runtime)
{
    CombatPolicyCache candidate;
    candidate.service_generation = kernel::services().generation();
    candidate.module = kernel::config().combat_policy_module();
    if (candidate.module.empty()) {
        LOG_F(ERROR, "[combat] no package combat policy module is configured");
        combat_policy = {};
        return false;
    }

    auto* script = runtime.load_module(candidate.module);
    if (!script || script->errors() != 0) {
        LOG_F(ERROR, "[combat] failed to load package combat policy '{}'",
            candidate.module);
        combat_policy = {};
        return false;
    }
    candidate.bytecode_module = runtime.get_or_compile_module(script);
    if (!candidate.bytecode_module) {
        LOG_F(ERROR, "[combat] failed to compile package combat policy '{}'",
            candidate.module);
        combat_policy = {};
        return false;
    }

    candidate.resolve_attack = candidate.bytecode_module->get_function("resolve_attack");
    candidate.resolve_attack_cooldown_ticks = candidate.bytecode_module->get_function("resolve_attack_cooldown_ticks");
    const auto creature_type = runtime.object_subtype_for_tag(ObjectType::creature);
    const std::array attack_parameters{creature_type, runtime.object_type()};
    const std::array cooldown_parameters{creature_type, runtime.int_type()};
    if (!function_has_parameters(runtime, candidate.resolve_attack,
            attack_parameters)
        || !function_has_parameters(runtime,
            candidate.resolve_attack_cooldown_ticks, cooldown_parameters)
        || !candidate.resolve_attack_cooldown_ticks
        || !is_int_compatible_type(runtime,
            candidate.resolve_attack_cooldown_ticks->return_type)) {
        LOG_F(ERROR,
            "[combat] package policy '{}' has invalid required function signatures",
            candidate.module);
        combat_policy = {};
        return false;
    }

    if (!resolve_attack_data_offsets(runtime,
            candidate.resolve_attack->return_type, candidate.offsets)) {
        LOG_F(ERROR,
            "[combat] package policy '{}.resolve_attack' has an invalid result layout",
            candidate.module);
        combat_policy = {};
        return false;
    }

    combat_policy = std::move(candidate);
    return true;
}

bool resolve_attack(Creature* attacker, ObjectBase* target, AttackData* out)
{
    if (!attacker || !target) { return false; }

    auto& rt = kernel::runtime();
    if (!combat_policy_is_current()) {
        LOG_F(ERROR,
            "[combat] package combat policy is unavailable for the current service generation");
        return false;
    }

    Vector<smalls::Value> args;
    args.push_back(make_object_arg(rt, attacker->handle()));
    args.push_back(make_object_arg(rt, target->handle()));

    auto exec_result = rt.execute_compiled(combat_policy.bytecode_module,
        combat_policy.resolve_attack, args);

    if (!exec_result.ok()) {
        LOG_F(WARNING, "[combat] {}.resolve_attack failed: {}",
            combat_policy.module, exec_result.error_message);
        return false;
    }
    if (exec_result.value.type_id != combat_policy.resolve_attack->return_type) {
        LOG_F(ERROR, "[combat] {}.resolve_attack returned an invalid result type",
            combat_policy.module);
        return false;
    }

    // Decode using pre-cached byte offsets.
    const auto& c = combat_policy.offsets;
    auto read_int = [&](const AttackDataFieldCache& fc) -> int32_t {
        return rt.read_value_field_at_offset(exec_result.value, fc.offset, fc.type_id).data.ival;
    };
    auto read_bool = [&](const AttackDataFieldCache& fc) -> bool {
        auto v = rt.read_value_field_at_offset(exec_result.value, fc.offset, fc.type_id);
        return (fc.type_id == rt.bool_type()) ? v.data.bval : (v.data.ival != 0);
    };

    AttackData scratch;
    auto* data = out ? out : &scratch;
    data->attacker = attacker;
    data->target = target;
    data->type = AttackType::make(read_int(c.attack_type));
    data->target_is_creature = read_bool(c.target_is_creature);
    data->is_ranged_attack = read_bool(c.is_ranged);
    data->nth_attack = read_int(c.nth_attack);
    data->result = static_cast<AttackResult>(read_int(c.attack_result));
    data->attack_roll = read_int(c.attack_roll);
    data->attack_bonus = read_int(c.attack_bonus);
    data->armor_class = read_int(c.armor_class);
    data->damage_total = read_int(c.damage_total);
    data->multiplier = read_int(c.critical_multiplier);
    data->threat_range = read_int(c.critical_threat);
    data->concealment = read_int(c.concealment);
    data->iteration_penalty = read_int(c.iteration_penalty);

    read_effects_apply_at_offset(rt, exec_result.value,
        c.effects_to_apply.offset, c.effects_to_apply.type_id, data->effects_to_apply);
    read_effects_remove_at_offset(rt, exec_result.value,
        c.effects_to_remove.offset, c.effects_to_remove.type_id, data->effects_to_remove);

    return true;
}

uint32_t resolve_attack_cooldown_ticks(const Creature* attacker, uint32_t round_ticks)
{
    if (!attacker || !combat_policy_is_current()) { return 1; }

    auto& runtime = kernel::runtime();
    Vector<smalls::Value> args;
    args.push_back(make_object_arg(runtime, attacker->handle()));
    const auto bounded_round_ticks = std::min<uint32_t>(round_ticks,
        static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));
    args.push_back(smalls::Value::make_int(
        static_cast<int32_t>(bounded_round_ticks)));

    const auto result = runtime.execute_compiled(combat_policy.bytecode_module,
        combat_policy.resolve_attack_cooldown_ticks, args);
    if (!result.ok()) {
        LOG_F(WARNING, "[combat] {}.resolve_attack_cooldown_ticks failed: {}",
            combat_policy.module, result.error_message);
        return 1;
    }

    return result.value.data.ival > 0
        ? static_cast<uint32_t>(result.value.data.ival)
        : 1;
}

bool schedule_attack(Creature* attacker, ObjectBase* target, uint64_t delay_ticks)
{
    if (!attacker || !target) {
        return false;
    }

    auto* payload = new ScheduledAttackEvent{attacker->handle(), target->handle()};
    kernel::events().add_custom(attacker->handle(), &scheduled_attack_event_callback, delay_ticks,
        payload, &scheduled_attack_payload_delete);
    return true;
}

bool start_auto_attack(Creature* attacker, ObjectBase* target,
    uint64_t initial_delay_ticks, uint32_t round_ticks)
{
    if (!attacker || !target) {
        return false;
    }

    auto& state = upsert_auto_attack_state(attacker->handle());
    state.target = target->handle();
    state.round_ticks = std::max<uint32_t>(1, round_ticks);
    state.active = true;
    ++state.generation;

    auto* payload = new AutoAttackEvent{.attacker = attacker->handle(), .generation = state.generation};
    kernel::events().add_custom(attacker->handle(), &auto_attack_event_callback, initial_delay_ticks,
        payload, &auto_attack_payload_delete);
    return true;
}

bool stop_auto_attack(Creature* attacker)
{
    if (!attacker) {
        return false;
    }

    auto* state = find_auto_attack_state(attacker->handle());
    if (!state || !state->active) {
        return false;
    }

    state->active = false;
    ++state->generation;
    return true;
}

bool resolve_attack_and_schedule(Creature* attacker, ObjectBase* target,
    uint32_t round_ticks, AttackData* out)
{
    if (!combat::resolve_attack(attacker, target, out) || !attacker || !target) {
        return false;
    }

    auto delay = combat::resolve_attack_cooldown_ticks(attacker, round_ticks);
    combat::schedule_attack(attacker, target, delay);
    return true;
}

int commit_attack_effects(AttackData* data)
{
    if (!data || !data->target) {
        return 0;
    }

    nw::Vector<nw::Effect*> to_apply;
    to_apply.reserve(data->effects_to_apply.size());
    for (auto* eff : data->effects_to_apply) {
        if (eff) {
            to_apply.push_back(eff);
        }
    }

    nw::Vector<nw::Effect*> apply_failed;
    kernel::effects().apply_to(data->target, to_apply, &apply_failed);
    for (auto* eff : apply_failed) {
        kernel::effects().destroy(eff);
    }

    nw::Vector<nw::Effect*> to_remove;
    to_remove.reserve(data->effects_to_remove.size());
    for (const auto& handle : data->effects_to_remove) {
        auto* eff = handle.effect ? handle.effect : kernel::effects().get(handle.runtime_handle);
        if (!eff) {
            continue;
        }

        to_remove.push_back(eff);
    }

    kernel::effects().remove_from(data->target, to_remove, true);

    data->effects_to_apply.clear();
    data->effects_to_remove.clear();
    return static_cast<int>(to_apply.size() + to_remove.size());
}

} // namespace nw::combat
