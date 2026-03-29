#include "functions.hpp"

#include "kernel/Config.hpp"
#include "kernel/EventSystem.hpp"
#include "kernel/Kernel.hpp"
#include "objects/Creature.hpp"
#include "profiles/nwn1/propset_populate.hpp"
#include "profiles/nwn1/scriptapi.hpp"
#include "profiles/nwn1/scriptbridge.hpp"
#include "rules/combat_scheduler.hpp"
#include "rules/effects.hpp"
#include "scriptapi.hpp"
#include "util/profile.hpp"
#include "smalls/Array.hpp"
#include "smalls/Bytecode.hpp"
#include "smalls/runtime.hpp"

#include <algorithm>
#include <chrono>

namespace nw {
namespace {

nw::StringView configured_combat_module() noexcept
{
    return nw::kernel::config().combat_policy_module();
}

bool is_int_compatible_type(nw::smalls::Runtime& rt, nw::smalls::TypeID type_id) noexcept
{
    while (type_id != rt.int_type()) {
        const auto* type = rt.get_type(type_id);
        if (!type) {
            return false;
        }

        if (type->type_kind != nw::smalls::TK_alias && type->type_kind != nw::smalls::TK_newtype) {
            return type->primitive_kind == nw::smalls::PK_int;
        }

        if (type->type_params.empty() || !type->type_params[0].is<nw::smalls::TypeID>()) {
            return false;
        }

        type_id = type->type_params[0].as<nw::smalls::TypeID>();
    }

    return true;
}

const nw::smalls::StructDef* get_attack_data_def(nw::smalls::Runtime& rt, const nw::smalls::Value& value)
{
    const auto* type = rt.get_type(value.type_id);
    if (!type || type->type_kind != nw::smalls::TK_struct || !type->type_params[0].is<nw::smalls::StructID>()) {
        return nullptr;
    }

    if (rt.type_name(value.type_id) != "core.combat.AttackData") {
        return nullptr;
    }

    auto struct_id = type->type_params[0].as<nw::smalls::StructID>();
    return rt.type_table_.get(struct_id);
}

bool read_attack_data_int_field(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    const nw::smalls::StructDef* def, nw::StringView field_name, int32_t& out)
{
    if (!def) {
        return false;
    }

    uint32_t field_index = def->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return false;
    }

    const auto& field = def->fields[field_index];
    auto value = rt.read_value_field_at_offset(data, field.offset, field.type_id);
    if (!value.type_id.value || !is_int_compatible_type(rt, value.type_id)) {
        return false;
    }

    out = value.data.ival;
    return true;
}

bool read_attack_data_bool_field(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    const nw::smalls::StructDef* def, nw::StringView field_name, bool& out)
{
    if (!def) {
        return false;
    }

    uint32_t field_index = def->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return false;
    }

    const auto& field = def->fields[field_index];
    auto value = rt.read_value_field_at_offset(data, field.offset, field.type_id);
    if (!value.type_id.value) {
        return false;
    }

    if (value.type_id == rt.bool_type()) {
        out = value.data.bval;
        return true;
    }

    if (!is_int_compatible_type(rt, value.type_id)) {
        return false;
    }

    out = value.data.ival != 0;
    return true;
}

void read_effects_apply_at_offset(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    uint32_t offset, nw::smalls::TypeID type_id,
    absl::InlinedVector<nw::Effect*, 8>& out)
{
    out.clear();
    if (offset == UINT32_MAX) { return; }
    auto value = rt.read_value_field_at_offset(data, offset, type_id);
    if (!value.type_id.value) { return; }
    auto* values = nw::smalls::detail::value_cast<nw::smalls::IArray*>(&rt, value);
    if (!values) { return; }
    out.reserve(values->size());
    nw::smalls::Value elem;
    for (size_t i = 0; i < values->size(); ++i) {
        if (!values->get_value(i, elem, rt)) { continue; }
        auto handle = nw::smalls::detail::value_cast<nw::TypedHandle>(&rt, elem);
        if (!handle.is_valid()) { continue; }
        if (auto* effect = nw::kernel::effects().get(handle)) {
            out.push_back(effect);
            nw::kernel::runtime().set_handle_ownership(handle, nw::smalls::OwnershipMode::ENGINE_OWNED);
        }
    }
}

void read_effects_remove_at_offset(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    uint32_t offset, nw::smalls::TypeID type_id,
    absl::InlinedVector<nw::EffectHandle, 8>& out)
{
    out.clear();
    if (offset == UINT32_MAX) { return; }
    auto value = rt.read_value_field_at_offset(data, offset, type_id);
    if (!value.type_id.value) { return; }
    auto* values = nw::smalls::detail::value_cast<nw::smalls::IArray*>(&rt, value);
    if (!values) { return; }
    out.reserve(values->size());
    nw::smalls::Value elem;
    for (size_t i = 0; i < values->size(); ++i) {
        if (!values->get_value(i, elem, rt)) { continue; }
        auto handle = nw::smalls::detail::value_cast<nw::TypedHandle>(&rt, elem);
        if (!handle.is_valid()) { continue; }
        if (auto* effect = nw::kernel::effects().get(handle)) {
            out.push_back(effect->handle());
        }
    }
}

void read_attack_data_apply_effects_field(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    const nw::smalls::StructDef* def, nw::StringView field_name,
    absl::InlinedVector<nw::Effect*, 8>& out)
{
    out.clear();
    if (!def) {
        return;
    }

    uint32_t field_index = def->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return;
    }

    const auto& field = def->fields[field_index];
    auto value = rt.read_value_field_at_offset(data, field.offset, field.type_id);
    if (!value.type_id.value) {
        return;
    }

    auto* values = nw::smalls::detail::value_cast<nw::smalls::IArray*>(&rt, value);
    if (!values) {
        return;
    }

    out.reserve(values->size());
    nw::smalls::Value elem;
    for (size_t i = 0; i < values->size(); ++i) {
        if (!values->get_value(i, elem, rt)) {
            continue;
        }

        auto handle = nw::smalls::detail::value_cast<nw::TypedHandle>(&rt, elem);
        if (!handle.is_valid()) {
            continue;
        }

        if (auto* effect = nw::kernel::effects().get(handle)) {
            out.push_back(effect);
            nw::kernel::runtime().set_handle_ownership(handle, nw::smalls::OwnershipMode::ENGINE_OWNED);
        }
    }
}

void read_attack_data_remove_effects_field(nw::smalls::Runtime& rt, const nw::smalls::Value& data,
    const nw::smalls::StructDef* def, nw::StringView field_name,
    absl::InlinedVector<nw::EffectHandle, 8>& out)
{
    out.clear();
    if (!def) {
        return;
    }

    uint32_t field_index = def->field_index(field_name);
    if (field_index == UINT32_MAX) {
        return;
    }

    const auto& field = def->fields[field_index];
    auto value = rt.read_value_field_at_offset(data, field.offset, field.type_id);
    if (!value.type_id.value) {
        return;
    }

    auto* values = nw::smalls::detail::value_cast<nw::smalls::IArray*>(&rt, value);
    if (!values) {
        return;
    }

    out.reserve(values->size());
    nw::smalls::Value elem;
    for (size_t i = 0; i < values->size(); ++i) {
        if (!values->get_value(i, elem, rt)) {
            continue;
        }

        auto handle = nw::smalls::detail::value_cast<nw::TypedHandle>(&rt, elem);
        if (!handle.is_valid()) {
            continue;
        }

        if (auto* effect = nw::kernel::effects().get(handle)) {
            out.push_back(effect->handle());
        }
    }
}

struct AttackDataFieldCache {
    uint32_t offset = UINT32_MAX;
    nw::smalls::TypeID type_id;
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
    nw::String module;
    bool nwn1_initialized = false;
    bool known_missing = false;
    bool function_resolved = false;
    nw::smalls::BytecodeModule* bytecode_module = nullptr;
    const nw::smalls::CompiledFunction* compiled_function = nullptr;
    AttackDataOffsetCache offsets;
};

thread_local ResolveAttackCache s_resolve_attack;

inline uint64_t resolve_attack_now_ns() noexcept
{
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

// == Effects =================================================================
// ============================================================================

int effect_extract_int0(const nw::EffectHandle& handle)
{
    return handle.effect ? handle.effect->get_int(0) : 0;
}

int queue_remove_effect_by(nw::ObjectBase* obj, nw::ObjectHandle creator)
{
    if (!obj) { return 0; }
    int processed = 0;
    for (const auto& handle : obj->effects()) {
        if (handle.effect->handle().creator == creator) {
            nw::kernel::events().add(nw::kernel::EventType::remove_effect, obj, handle.effect);
            ++processed;
        }
    }
    return processed;
}

// == Item Properties =========================================================
// ============================================================================

int process_item_properties(nw::Creature* obj, const nw::Item* item, nw::EquipIndex index, bool remove)
{
    NW_PROFILE_SCOPE_N("process_item_properties");
    if (!obj || !item) { return 0; }

    auto& rt = nw::kernel::runtime();
    nwn1::populate_item_propsets(&rt, const_cast<nw::Item*>(item));

    nw::Vector<nw::smalls::Value> args;

    auto creature_value = nw::smalls::Value::make_object(obj->handle());
    creature_value.type_id = rt.object_subtype_for_tag(obj->handle().type);
    args.push_back(creature_value);

    auto item_value = nw::smalls::Value::make_object(item->handle());
    item_value.type_id = rt.object_subtype_for_tag(item->handle().type);
    args.push_back(item_value);

    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(index)));
    args.push_back(nw::smalls::Value::make_bool(remove));

    constexpr uint64_t item_props_gas_limit = 10'000'000;
    auto result = rt.execute_script("core.item", "process_item_properties", args, item_props_gas_limit);
    if (result.ok()) { return result.value.data.ival; }
    LOG_F(ERROR, "[functions] process_item_properties failed: {}", result.error_message);
    return 0;
}

// == Combat ==================================================================
// ============================================================================

bool resolve_attack(Creature* attacker, ObjectBase* target, AttackData* out)
{
    if (!attacker || !target) {
        return false;
    }

    auto module_sv = configured_combat_module();
    if (module_sv.empty()) {
        LOG_F(ERROR, "[functions] resolve_attack: no combat policy module configured");
        return false;
    }

    auto& rt = nw::kernel::runtime();
    auto& cache = s_resolve_attack;

    // Invalidate cache when the configured policy module name changes.
    if (cache.module != module_sv) {
        cache = {};
        cache.module = nw::String(module_sv);
    }

    // Always get the current BytecodeModule* to detect runtime restarts.
    // get_or_compile_module is O(1) when the module is already compiled (hash map lookup).
    auto* script = rt.get_module(module_sv);
    if (!script) {
        LOG_F(ERROR, "[functions] combat policy module '{}' not found", module_sv);
        return false;
    }
    auto* fresh_module = rt.get_or_compile_module(script);
    if (!fresh_module) {
        LOG_F(ERROR, "[functions] combat policy module '{}' failed to compile", module_sv);
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
            LOG_F(ERROR, "[functions] combat policy '{}.resolve_attack' not found", module_sv);
        }
    }

    if (cache.known_missing) {
        return false;
    }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(attacker->handle()));
    args.push_back(nwn1::bridge::make_object_arg(target->handle()));

    const bool policy_timing = nwn1::is_combat_policy_timing_enabled();
    const bool resolve_timing = nwn1::is_combat_resolve_timing_enabled();
    const uint64_t t_start = resolve_timing ? resolve_attack_now_ns() : 0;
    const uint64_t t0 = (policy_timing || resolve_timing) ? resolve_attack_now_ns() : 0;

    auto exec_result = rt.execute_compiled(cache.bytecode_module, cache.compiled_function, args);

    const uint64_t t1 = (policy_timing || resolve_timing) ? resolve_attack_now_ns() : 0;

    if (!exec_result.ok()) {
        LOG_F(WARNING, "[functions] {}.resolve_attack failed: {}", module_sv, exec_result.error_message);
        return false;
    }

    // Build the field-offset cache once from the first valid return value.
    if (!cache.offsets.valid) {
        const auto* def = get_attack_data_def(rt, exec_result.value);
        if (!def) {
            LOG_F(ERROR, "[functions] {}.resolve_attack returned invalid core.combat.AttackData", module_sv);
            return false;
        }
        auto& c = cache.offsets;
        auto resolve = [&](nw::StringView name) -> AttackDataFieldCache {
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
        LOG_F(ERROR, "[functions] {}.resolve_attack AttackData missing required fields", module_sv);
        return false;
    }

    // Fast decode using pre-cached byte offsets — no per-field string lookup.
    const auto& c = cache.offsets;
    auto read_int = [&](const AttackDataFieldCache& fc) -> int32_t {
        return rt.read_value_field_at_offset(exec_result.value, fc.offset, fc.type_id).data.ival;
    };
    auto read_bool = [&](const AttackDataFieldCache& fc) -> bool {
        auto v = rt.read_value_field_at_offset(exec_result.value, fc.offset, fc.type_id);
        return (fc.type_id == rt.bool_type()) ? v.data.bval : (v.data.ival != 0);
    };

    const uint64_t t2 = (policy_timing || resolve_timing) ? resolve_attack_now_ns() : 0;
    if (policy_timing) {
        nwn1::record_combat_policy_timing(t1 - t0, t2 - t1);
    }

    nw::AttackData scratch;
    auto* data = out ? out : &scratch;
    data->attacker = attacker;
    data->target = target;
    data->type = nw::AttackType::make(read_int(c.attack_type));
    // data->weapon = nwn1::get_weapon_by_attack_type(attacker, data->type);
    data->target_is_creature = read_bool(c.target_is_creature);
    data->is_ranged_attack = read_bool(c.is_ranged);
    data->nth_attack = read_int(c.nth_attack);
    data->result = static_cast<nw::AttackResult>(read_int(c.attack_result));
    data->attack_roll = read_int(c.attack_roll);
    data->attack_bonus = read_int(c.attack_bonus);
    data->armor_class = read_int(c.armor_class);
    data->damage_total = read_int(c.damage_total);
    data->multiplier = read_int(c.critical_multiplier);
    data->threat_range = read_int(c.critical_threat);
    data->concealment = read_int(c.concealment);
    data->iteration_penalty = read_int(c.iteration_penalty);

    const uint64_t t3 = resolve_timing ? resolve_attack_now_ns() : 0;

    read_effects_apply_at_offset(rt, exec_result.value,
        c.effects_to_apply.offset, c.effects_to_apply.type_id, data->effects_to_apply);
    read_effects_remove_at_offset(rt, exec_result.value,
        c.effects_to_remove.offset, c.effects_to_remove.type_id, data->effects_to_remove);

    if (resolve_timing) {
        const uint64_t t4 = resolve_attack_now_ns();
        nwn1::record_combat_resolve_timing(
            t0 - t_start,
            t1 - t0,
            t3 - t1,
            t4 - t3,
            t4 - t_start);
    }

    return true;
}

uint32_t resolve_attack_cooldown_ticks(const Creature* attacker, uint32_t round_ticks)
{
    if (!attacker) {
        return 1;
    }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(attacker->handle()));
    args.push_back(nw::smalls::Value::make_int(static_cast<int32_t>(round_ticks)));

    if (auto value = nwn1::bridge::call_nwn1_module_int("nwn1.combat", "resolve_attack_cooldown_ticks", args)) {
        return std::max<uint32_t>(1, static_cast<uint32_t>(*value));
    }

    return 1;
}

bool schedule_attack(Creature* attacker, ObjectBase* target, uint64_t delay_ticks)
{
    return combat::schedule_attack(attacker, target, delay_ticks);
}

bool start_auto_attack(Creature* attacker, ObjectBase* target,
    uint64_t initial_delay_ticks, uint32_t round_ticks)
{
    return combat::start_auto_attack(attacker, target, initial_delay_ticks, round_ticks);
}

bool stop_auto_attack(Creature* attacker)
{
    return combat::stop_auto_attack(attacker);
}

bool resolve_attack_and_schedule(Creature* attacker, ObjectBase* target,
    uint32_t round_ticks, AttackData* out)
{
    return combat::resolve_attack_and_schedule(attacker, target, round_ticks, out);
}

int commit_attack_effects(AttackData* data)
{
    return combat::commit_attack_effects(data);
}

} // namespace nw
