#pragma once

#include <cstdint>

namespace nw {
struct AttackData;
struct Creature;
struct ObjectBase;

namespace combat {

/// Resolves one attack using the configured combat policy module.
bool resolve_attack(Creature* attacker, ObjectBase* target, AttackData* out = nullptr);

uint32_t resolve_attack_cooldown_ticks(const Creature* attacker, uint32_t round_ticks = 60);
bool schedule_attack(Creature* attacker, ObjectBase* target, uint64_t delay_ticks);
bool start_auto_attack(Creature* attacker, ObjectBase* target,
    uint64_t initial_delay_ticks = 0, uint32_t round_ticks = 60);
bool stop_auto_attack(Creature* attacker);
bool resolve_attack_and_schedule(Creature* attacker, ObjectBase* target,
    uint32_t round_ticks = 60, AttackData* out = nullptr);

/// Applies/removes effect intents carried by AttackData.
/// Returns number of effects processed.
int commit_attack_effects(AttackData* data);

} // namespace combat
} // namespace nw
