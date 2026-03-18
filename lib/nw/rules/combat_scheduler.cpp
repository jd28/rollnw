#include "combat_scheduler.hpp"

#include "../kernel/EventSystem.hpp"
#include "../kernel/Kernel.hpp"
#include "../objects/Creature.hpp"
#include "../objects/ObjectManager.hpp"

#include <algorithm>
#include <vector>

namespace nw::combat {
namespace {

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

ResolveAttackFn g_resolve_attack = nullptr;
ResolveCooldownTicksFn g_resolve_cooldown = nullptr;

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

void scheduled_attack_event_callback(const kernel::EventHandle& ev)
{
    auto* payload = static_cast<ScheduledAttackEvent*>(ev.data);
    if (!payload || !g_resolve_attack) {
        return;
    }

    auto* attacker = kernel::objects().get<Creature>(payload->attacker);
    auto* target = kernel::objects().get_object_base(payload->target);
    if (!attacker || !target) {
        return;
    }

    resolve_attack(attacker, target);
}

void auto_attack_event_callback(const kernel::EventHandle& ev)
{
    auto* payload = static_cast<AutoAttackEvent*>(ev.data);
    if (!payload || !g_resolve_attack) {
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

    if (auto* cre = target->as_creature(); cre && cre->hp_current <= 0) {
        state->active = false;
        return;
    }

    resolve_attack(attacker, target);

    auto delay = resolve_attack_cooldown_ticks(attacker, state->round_ticks);
    auto* next = new AutoAttackEvent{.attacker = payload->attacker, .generation = payload->generation};
    kernel::events().add_custom(payload->attacker, &auto_attack_event_callback, delay,
        next, &auto_attack_payload_delete);
}

} // namespace

void set_attack_scheduler_policy(ResolveAttackFn resolve_attack, ResolveCooldownTicksFn resolve_cooldown) noexcept
{
    g_resolve_attack = resolve_attack;
    g_resolve_cooldown = resolve_cooldown;
}

void clear_attack_scheduler_policy() noexcept
{
    g_resolve_attack = nullptr;
    g_resolve_cooldown = nullptr;
}

bool has_attack_scheduler_policy() noexcept
{
    return g_resolve_attack != nullptr;
}

bool resolve_attack(Creature* attacker, ObjectBase* target, AttackData* out)
{
    if (!g_resolve_attack) {
        return false;
    }
    return g_resolve_attack(attacker, target, out);
}

uint32_t resolve_attack_cooldown_ticks(const Creature* attacker, uint32_t round_ticks)
{
    if (!g_resolve_cooldown) {
        return 1;
    }
    return std::max<uint32_t>(1, g_resolve_cooldown(attacker, round_ticks));
}

bool schedule_attack(Creature* attacker, ObjectBase* target, uint64_t delay_ticks)
{
    if (!attacker || !target || !g_resolve_attack) {
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
    if (!attacker || !target || !g_resolve_attack) {
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
    if (!resolve_attack(attacker, target, out) || !attacker || !target) {
        return false;
    }

    auto delay = resolve_attack_cooldown_ticks(attacker, round_ticks);
    schedule_attack(attacker, target, delay);
    return true;
}

} // namespace nw::combat
