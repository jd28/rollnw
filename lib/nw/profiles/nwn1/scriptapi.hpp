#pragma once

#include <cstdint>

#include "../../rules/Spell.hpp"
#include "../../rules/attributes.hpp"
#include "constants.hpp"
#include "scriptbridge.hpp"

namespace nw {
enum struct AttackResult;
enum struct EquipIndex : uint32_t;
enum struct TargetState;
struct AttackData;
struct AttackType;
struct BaseItem;
struct Class;
struct Creature;
struct Damage;
struct DiceRoll;
struct Effect;
struct EffectType;
struct Item;
struct ItemProperty;
struct ObjectBase;
struct ObjectHandle;
struct Spell;
using DamageFlag = RuleFlag<Damage, 32>;
} // namespace nw

namespace nwn1 {

// NOTE:
// This namespace is a legacy compatibility surface while we migrate rules logic
// to profile scripts/data and profile-agnostic nw::combat APIs. New runtime
// features should prefer generic APIs. Declarations in this header are
// candidates for deletion once downstream callers are migrated.

struct CombatPolicyTimingSnapshot {
    uint64_t policy_call_ns = 0;
    uint64_t decode_ns = 0;
    uint64_t fallback_ns = 0;
    uint64_t iterations = 0;
};

struct CombatResolveTimingSnapshot {
    uint64_t prepare_ns = 0;
    uint64_t policy_call_ns = 0;
    uint64_t decode_ns = 0;
    uint64_t marshal_ns = 0;
    uint64_t total_ns = 0;
    uint64_t iterations = 0;
};

void set_combat_policy_timing_enabled(bool enabled) noexcept;
void reset_combat_policy_timing() noexcept;
CombatPolicyTimingSnapshot combat_policy_timing_snapshot() noexcept;
bool is_combat_policy_timing_enabled() noexcept;
void record_combat_policy_timing(uint64_t policy_ns, uint64_t decode_ns, uint64_t fallback_ns = 0) noexcept;

void set_combat_resolve_timing_enabled(bool enabled) noexcept;
void reset_combat_resolve_timing() noexcept;
CombatResolveTimingSnapshot combat_resolve_timing_snapshot() noexcept;
bool is_combat_resolve_timing_enabled() noexcept;
void record_combat_resolve_timing(uint64_t prepare_ns, uint64_t policy_ns, uint64_t decode_ns,
    uint64_t marshal_ns, uint64_t total_ns) noexcept;

/// Calculates a creatures challenge rating
/// @warning This is currently only an approximation of what the toolset calculates. It can be off by a bit
/// @ingroup Python
/// @since 0.46
float calculate_challenge_rating(const nw::Creature* obj);

// == Creature: Abilities =====================================================
// ============================================================================
// TODO: legacy compatibility; remove after migration to script/data APIs.

/// Gets creatures ability modifier
int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base = false);

// == Creature: Casting =======================================================
// ============================================================================
// TODO: legacy compatibility; remove after migration to script/data APIs.

/// Adds a known spell
bool add_known_spell(nw::Creature* obj, nw::Class class_, nw::Spell spell);

/// Adds a memorized spell
bool add_memorized_spell(nw::Creature* obj, nw::Class class_, nw::Spell spell, nw::MetaMagicFlag meta = nw::metamagic_none);

/// Computes available slots that are able to be prepared or castable
int compute_total_spell_slots(const nw::Creature* obj, nw::Class class_, int spell_level);

/// Computes total knownable spells at ``spell_level``
///
/// @ingroup Python
/// @since 0.46
int compute_total_spells_knowable(const nw::Creature* obj, nw::Class class_, int spell_level);

/// Gets the number of available slots at a particular spell level
int get_available_spell_slots(const nw::Creature* obj, nw::Class class_, int spell_level);

/// Gets available spell uses
int get_available_spell_uses(const nw::Creature* obj, nw::Class class_, nw::Spell spell, int min_spell_level = 0,
    nw::MetaMagicFlag meta = {});

/// Gets creature's caster level for specified class
int get_caster_level(const nw::Creature* obj, nw::Class class_);

/// Determines if creature knows a spell
bool get_knows_spell(const nw::Creature* obj, nw::Class class_, nw::Spell spell);

/// Gets spell DC
int get_spell_dc(const nw::Creature* obj, nw::Class class_, nw::Spell spell);

/// Recomputes all available spell slots
void recompute_all_availabe_spell_slots(nw::Creature* obj);

/// Removes a known spell and any preparations thereof.
void remove_known_spell(nw::Creature* obj, nw::Class class_, nw::Spell spell);

// == Creature: Combat ========================================================
// ============================================================================
// TODO: legacy compatibility

/// Calculates base attack bonus
int base_attack_bonus(const nw::Creature* obj);

// == Items ===================================================================
// ============================================================================
// TODO: legacy compatibility; remove after migration to script/data APIs.

/// Calculates the value of an item
/// @ingroup Python
/// @since 0.46
int calculate_item_value(const nw::Item* item);

} // namespace nwn1
