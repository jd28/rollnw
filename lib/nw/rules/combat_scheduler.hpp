#pragma once

#include <cstdint>

namespace nw {
struct AttackData;
struct Creature;
struct ObjectBase;

namespace smalls {
struct Runtime;
}

namespace combat {

/// Resolves and validates the configured package combat policy for this
/// Runtime/service generation.
bool initialize_policy(smalls::Runtime& runtime);

/// Resolves one attack using the configured combat policy module.
bool resolve_attack(Creature* attacker, ObjectBase* target, AttackData* out = nullptr);

/// Ticks until the attacker may attack again, per the configured policy.
/// `pacing_ticks` is an optional budget the caller wants the policy to divide;
/// 0 means "the policy's own pacing". The engine deliberately does not define a
/// combat round -- pacing belongs to the policy, which is what lets a
/// turn-based or action-driven mode replace it without touching the scheduler.
uint32_t resolve_attack_cooldown_ticks(const Creature* attacker, uint32_t pacing_ticks = 0);
bool schedule_attack(Creature* attacker, ObjectBase* target, uint64_t delay_ticks);
bool start_auto_attack(Creature* attacker, ObjectBase* target,
    uint64_t initial_delay_ticks = 0, uint32_t pacing_ticks = 0);
bool stop_auto_attack(Creature* attacker);
bool resolve_attack_and_schedule(Creature* attacker, ObjectBase* target,
    uint32_t pacing_ticks = 0, AttackData* out = nullptr);

/// Applies/removes effect intents carried by AttackData.
/// Returns number of effects processed.
int commit_attack_effects(AttackData* data);

/// Applies the resolved damage total to the target via the policy's
/// `commit_attack_damage` hook, setting AttackData::is_killing_blow.
/// Returns the hit points actually removed; 0 if the policy has no hook.
int commit_attack_damage(AttackData* data);

/// Commits a resolved attack: damage first, then effect intents.
/// Returns hit points removed plus effects processed.
int commit_attack(AttackData* data);

} // namespace combat
} // namespace nw
