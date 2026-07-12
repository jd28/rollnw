#include "combat_scheduler.hpp"

#include "../kernel/Config.hpp"
#include "../kernel/EventSystem.hpp"
#include "../kernel/Kernel.hpp"
#include "../objects/Creature.hpp"
#include "../objects/ObjectManager.hpp"
#include "../profiles/nwn1/scriptbridge.hpp"
#include "../smalls/Array.hpp"
#include "../smalls/Bytecode.hpp"
#include "../smalls/runtime.hpp"
#include "combat.hpp"
#include "effects.hpp"

#include <algorithm>
#include <vector>

namespace nw::combat {
namespace {

StringView configured_combat_module() noexcept
{
    return kernel::config().combat_policy_module();
}

const smalls::StructDef* get_attack_data_def(smalls::Runtime& rt, const smalls::Value& value)
{
    const auto* type = rt.get_type(value.type_id);
    if (!type || type->type_kind != smalls::TK_struct || !type->type_params[0].is<smalls::StructID>()) {
        return nullptr;
    }

    if (rt.type_name(value.type_id) != "core.combat.AttackData") {
        return nullptr;
    }

    auto struct_id = type->type_params[0].as<smalls::StructID>();
    return rt.type_table_.get(struct_id);
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

struct ResolveAttackCache {
    String module;
    bool nwn1_initialized = false;
    bool known_missing = false;
    bool function_resolved = false;
    smalls::BytecodeModule* bytecode_module = nullptr;
    const smalls::CompiledFunction* compiled_function = nullptr;
    AttackDataOffsetCache offsets;
};

thread_local ResolveAttackCache s_resolve_attack;

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

AutoAttackState* find_auto_attack_state(ObjectHandle attacker)
{
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

bool resolve_attack(Creature* attacker, ObjectBase* target, AttackData* out)
{
    if (!attacker || !target) {
        return false;
    }

    auto module_sv = configured_combat_module();
    if (module_sv.empty()) {
        LOG_F(ERROR, "[combat] resolve_attack: no combat policy module configured");
        return false;
    }

    auto& rt = kernel::runtime();
    auto& cache = s_resolve_attack;

    // Invalidate cache when the configured policy module name changes.
    if (cache.module != module_sv) {
        cache = {};
        cache.module = String(module_sv);
    }

    // Always get the current BytecodeModule* to detect runtime restarts.
    auto* script = rt.get_module(module_sv);
    if (!script) {
        LOG_F(ERROR, "[combat] combat policy module '{}' not found", module_sv);
        return false;
    }
    auto* fresh_module = rt.get_or_compile_module(script);
    if (!fresh_module) {
        LOG_F(ERROR, "[combat] combat policy module '{}' failed to compile", module_sv);
        return false;
    }

    // If BytecodeModule pointer changed (e.g., runtime restarted between calls),
    // reset all derived cache state so we don't use dangling pointers.
    if (cache.bytecode_module != fresh_module) {
        cache.bytecode_module = fresh_module;
        cache.nwn1_initialized = false;
        cache.compiled_function = nullptr;
        cache.function_resolved = false;
        cache.known_missing = false;
        cache.offsets = {};
    }

    // Ensure nwn1 smalls are initialized once per (policy module, runtime instance).
    if (!cache.nwn1_initialized) {
        if (!nwn1::bridge::ensure_nwn1_smalls_initialized()) {
            return false;
        }
        cache.nwn1_initialized = true;
    }

    // Resolve and cache the compiled function pointer.
    if (!cache.function_resolved) {
        cache.compiled_function = cache.bytecode_module->get_function("resolve_attack");
        cache.function_resolved = true;
        cache.known_missing = (cache.compiled_function == nullptr);
        if (cache.known_missing) {
            LOG_F(ERROR, "[combat] combat policy '{}.resolve_attack' not found", module_sv);
        }
    }

    if (cache.known_missing) {
        return false;
    }

    Vector<smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(attacker->handle()));
    args.push_back(nwn1::bridge::make_object_arg(target->handle()));

    auto exec_result = rt.execute_compiled(cache.bytecode_module, cache.compiled_function, args);

    if (!exec_result.ok()) {
        LOG_F(WARNING, "[combat] {}.resolve_attack failed: {}", module_sv, exec_result.error_message);
        return false;
    }

    // Build the field-offset cache once from the first valid return value.
    if (!cache.offsets.valid) {
        const auto* def = get_attack_data_def(rt, exec_result.value);
        if (!def) {
            LOG_F(ERROR, "[combat] {}.resolve_attack returned invalid core.combat.AttackData", module_sv);
            return false;
        }
        auto& c = cache.offsets;
        auto resolve = [&](StringView name) -> AttackDataFieldCache {
            uint32_t idx = def->field_index(name);
            if (idx == UINT32_MAX) { return {}; }
            return {def->fields[idx].offset, def->fields[idx].type_id};
        };
        c.attack_type = resolve("attack_type");
        c.attack_result = resolve("attack_result");
        c.attack_roll = resolve("attack_roll");
        c.attack_bonus = resolve("attack_bonus");
        c.armor_class = resolve("armor_class");
        c.nth_attack = resolve("nth_attack");
        c.damage_total = resolve("damage_total");
        c.critical_multiplier = resolve("critical_multiplier");
        c.critical_threat = resolve("critical_threat");
        c.concealment = resolve("concealment");
        c.iteration_penalty = resolve("iteration_penalty");
        c.is_ranged = resolve("is_ranged");
        c.target_is_creature = resolve("target_is_creature");
        c.effects_to_apply = resolve("effects_to_apply");
        c.effects_to_remove = resolve("effects_to_remove");
        c.valid = (c.attack_type.offset != UINT32_MAX);
    }

    if (!cache.offsets.valid) {
        LOG_F(ERROR, "[combat] {}.resolve_attack AttackData missing required fields", module_sv);
        return false;
    }

    // Decode using pre-cached byte offsets.
    const auto& c = cache.offsets;
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
    if (!attacker) {
        return 1;
    }

    Vector<smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(attacker->handle()));
    args.push_back(smalls::Value::make_int(static_cast<int32_t>(round_ticks)));

    if (auto value = nwn1::bridge::call_nwn1_module_int("nwn1.combat", "resolve_attack_cooldown_ticks", args)) {
        return std::max<uint32_t>(1, static_cast<uint32_t>(*value));
    }

    return 1;
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
