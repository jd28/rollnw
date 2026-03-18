#pragma once

#include <cstdint>

namespace nw {
struct AttackData;
struct Creature;
struct ObjectBase;

namespace combat {

using ResolveAttackFn = bool (*)(Creature*, ObjectBase*, AttackData* out);
using ResolveCooldownTicksFn = uint32_t (*)(const Creature*, uint32_t);

void set_attack_scheduler_policy(ResolveAttackFn resolve_attack, ResolveCooldownTicksFn resolve_cooldown) noexcept;
void clear_attack_scheduler_policy() noexcept;
bool has_attack_scheduler_policy() noexcept;

/// Resolves one attack using the active profile combat policy.
bool resolve_attack(Creature* attacker, ObjectBase* target, AttackData* out = nullptr);

uint32_t resolve_attack_cooldown_ticks(const Creature* attacker, uint32_t round_ticks = 60);
bool schedule_attack(Creature* attacker, ObjectBase* target, uint64_t delay_ticks);
bool start_auto_attack(Creature* attacker, ObjectBase* target,
    uint64_t initial_delay_ticks = 0, uint32_t round_ticks = 60);
bool stop_auto_attack(Creature* attacker);
bool resolve_attack_and_schedule(Creature* attacker, ObjectBase* target,
    uint32_t round_ticks = 60, AttackData* out = nullptr);

} // namespace combat
} // namespace nw
