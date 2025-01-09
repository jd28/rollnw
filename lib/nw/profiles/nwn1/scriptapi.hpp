#pragma once

#include "../../rules/Spell.hpp"
#include "../../rules/attributes.hpp"
#include "constants.hpp"

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

// ============================================================================
// == Object ==================================================================
// ============================================================================

// == Object: Effects =========================================================
// ============================================================================

// == Object: Hit Points ======================================================
// ============================================================================

/// Gets objects current hitpoints
int get_current_hitpoints(const nw::ObjectBase* obj);

/// Gets objects maximum hit points.
int get_max_hitpoints(const nw::ObjectBase* obj);

// == Object: Saves ============================================================
// =============================================================================

/// Determines saving throw
int saving_throw(const nw::ObjectBase* obj, nw::Save type, nw::SaveVersus type_vs = nw::SaveVersus::invalid(),
    const nw::ObjectBase* versus = nullptr);

/// Resolves saving throw
bool resolve_saving_throw(const nw::ObjectBase* obj, nw::Save type, int dc,
    nw::SaveVersus type_vs = nw::SaveVersus::invalid(), const nw::ObjectBase* versus = nullptr);

// ============================================================================
// == Creature ================================================================
// ============================================================================

// == Creature: Abilities =====================================================
// ============================================================================

/// Gets creatures ability score
int get_ability_score(const nw::Creature* obj, nw::Ability ability, bool base = false);

/// Gets creatures ability modifier
int get_ability_modifier(const nw::Creature* obj, nw::Ability ability, bool base = false);

/// Gets creatures dexterity modifier as modified by armor, etc.
int get_dex_modifier(const nw::Creature* obj);

// == Creature: Armor Class ===================================================
// ============================================================================

/// Calculate Armor Class versus another object
int calculate_ac_versus(const nw::ObjectBase* obj, const nw::ObjectBase* versus, bool is_touch_attack);

/// Calculates the armor class of a piece of armor
int calculate_item_ac(const nw::Item* obj);

// == Creature: Casting =======================================================
// ============================================================================

/// Adds a known spell
bool add_known_spell(nw::Creature* obj, nw::Class class_, nw::Spell spell);

/// Adds a memorized spell
bool add_memorized_spell(nw::Creature* obj, nw::Class class_, nw::Spell spell, nw::MetaMagicFlag meta = nw::metamagic_none);

/// Computes available slots that are able to be prepared or castable
int compute_total_spell_slots(const nw::Creature* obj, nw::Class class_, int spell_level);

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

// == Creature: Classes =======================================================
// ============================================================================

/// Determines if monk class abilities are usable and monk class level
std::pair<bool, int> can_use_monk_abilities(const nw::Creature* obj);

// == Creature: Combat ========================================================
// ============================================================================

/// Calculates base attack bonus
int base_attack_bonus(const nw::Creature* obj);

/// Determine if creature is flanked by an attacker
bool is_flanked(const nw::Creature* target, const nw::Creature* attacker);

/// Resolves an attack
/// @note All transient book keeping is done at the toplevel of this function,
/// any other that attacker and/or target are passed to, are passed as const.
std::unique_ptr<nw::AttackData> resolve_attack(nw::Creature* attacker, nw::ObjectBase* target);

/// Resolves attack bonus
int resolve_attack_bonus(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* versus = nullptr);

/// Resolves damage from an attack
/// @return Total damage, ``data`` holds individual damage totals
int resolve_attack_damage(const nw::Creature* obj, const nw::ObjectBase* versus, nw::AttackData* data);

/// Resolves an attack roll
nw::AttackResult resolve_attack_roll(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* vs, nw::AttackData* data = nullptr);

/// Resolves attack type
nw::AttackType resolve_attack_type(const nw::Creature* obj);

/// Resolves an concealment - i.e. the highest of concealment and miss chance
/// @return Concealment amount, bool that if ``true`` is from attacking creature i.e miss chance,
/// if ``false`` from target object i.e. concealment
std::pair<int, bool> resolve_concealment(const nw::ObjectBase* obj, const nw::ObjectBase* target, bool vs_ranged = false);

/// Resolves critical multiplier
int resolve_critical_multiplier(const nw::Creature* obj, nw::AttackType type, const nw::ObjectBase* vs = nullptr);

/// Resolves critical threat range.
int resolve_critical_threat(const nw::Creature* obj, nw::AttackType type);

/// Resolves damage modifiers - soak, resist, immunity
void resolve_damage_modifiers(const nw::Creature* obj, const nw::ObjectBase* versus, nw::AttackData* data);

/// Resolves damage immunity
int resolve_damage_immunity(const nw::ObjectBase* obj, nw::Damage type, const nw::ObjectBase* versus = nullptr);

/// Resolves damage reduction
std::pair<int, nw::Effect*> resolve_damage_reduction(const nw::ObjectBase* obj, int power, const nw::ObjectBase* versus = nullptr);

/// Resolves damage resistance
std::pair<int, nw::Effect*> resolve_damage_resistance(const nw::ObjectBase* obj, nw::Damage type, const nw::ObjectBase* versus = nullptr);

/// Resolves dual-wield penalty
std::pair<int, int> resolve_dual_wield_penalty(const nw::Creature* obj);

/// Resolves iteration penalty
int resolve_iteration_penalty(const nw::Creature* attacker, nw::AttackType type);

/// Resolves number of attacks
std::pair<int, int> resolve_number_of_attacks(const nw::Creature* obj);

/// Resolve target state
nw::TargetState resolve_target_state(const nw::Creature* attacker, const nw::ObjectBase* target);

/// Resolve weapon base damage flags
nw::DamageFlag resolve_weapon_damage_flags(const nw::Item* weapon);

/// Resolves weapon power
int resolve_weapon_power(const nw::Creature* obj, const nw::Item* weapon);

/// Determines if a weapon is finessable
bool weapon_is_finessable(const nw::Creature* obj, nw::Item* weapon);

/// Calculates weapon iteration, e.g. 5 or 3 for monk weapons
int weapon_iteration(const nw::Creature* obj, const nw::Item* weapon);

// == Creature: Equips ========================================================
// ============================================================================

/// Determines if an item can be equipped
bool can_equip_item(const nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);

/// Equip an item
bool equip_item(nw::Creature* obj, nw::Item* item, nw::EquipIndex slot);

/// Gets an equipped item
nw::Item* get_equipped_item(const nw::Creature* obj, nw::EquipIndex slot);

/// Unequips an item
nw::Item* unequip_item(nw::Creature* obj, nw::EquipIndex slot);

// == Creature: Skills ========================================================
// ============================================================================

/// Determines creatures skill rank
int get_skill_rank(const nw::Creature* obj, nw::Skill skill,
    nw::ObjectBase* versus = nullptr, bool base = false);

/// Resolves skill check
bool resolve_skill_check(const nw::Creature* obj, nw::Skill skill, int dc,
    nw::ObjectBase* versus = nullptr);

// == Weapons =================================================================
// ============================================================================

/// Converts an equip index to an attack type
nw::AttackType equip_index_to_attack_type(nw::EquipIndex equip);

/// Gets relative weapon size
/// @note May or may not be what the game does
int get_relative_weapon_size(const nw::Creature* obj, const nw::Item* item);

/// Gets an equipped weapon by attack type
nw::Item* get_weapon_by_attack_type(const nw::Creature* obj, nw::AttackType type);

/// Determines if item is creature weapon
bool is_creature_weapon(const nw::Item* item);

/// Determines if item is double sided weapon
bool is_double_sided_weapon(const nw::Item* item);

/// Determines if item is a light weapon
bool is_light_weapon(const nw::Creature* obj, const nw::Item* item);

/// Determines if item is monk weapon
bool is_monk_weapon(const nw::Item* item);

/// Determines if item is ranged weapon
bool is_ranged_weapon(const nw::Item* item);

/// Determines if item is unarmed weapon
bool is_unarmed_weapon(const nw::Item* item);

/// Determines if item is a shield
bool is_shield(nw::BaseItem baseitem);

/// Determines if item requires two hands to wield
bool is_two_handed_weapon(const nw::Creature* obj, const nw::Item* item);

/// Resolves creature weapon damage
nw::DiceRoll resolve_creature_damage(const nw::Creature* attacker, nw::Item* weapon);

/// Resolve unarmed base damage
nw::DiceRoll resolve_unarmed_damage(const nw::Creature* attacker);

/// Resolve weapon base damage
/// @note Includes specialization and arcane archer bonuses
nw::DiceRoll resolve_weapon_damage(const nw::Creature* attacker, nw::BaseItem item);

// ============================================================================
// == Effects =================================================================
// ============================================================================

/// Creates an ability modifier effect
nw::Effect* effect_ability_modifier(nw::Ability ability, int modifier);

/// Creates an armor class modifier effect
nw::Effect* effect_armor_class_modifier(nw::ArmorClass type, int modifier);

/// Creates an attack modifier effect
nw::Effect* effect_attack_modifier(nw::AttackType attack, int modifier);

/// Creates an bonus spell slot effect
nw::Effect* effect_bonus_spell_slot(nw::Class class_, int spell_level);

/// Creates concealment effect
nw::Effect* effect_concealment(int value, nw::MissChanceType type = miss_chance_type_normal);

/// Creates an damage bonus effect
nw::Effect* effect_damage_bonus(nw::Damage type, nw::DiceRoll dice, nw::DamageCategory cat = nw::DamageCategory::none);

/// Creates an damage immunity effect
/// @note Negative values create a vulnerability
nw::Effect* effect_damage_immunity(nw::Damage type, int value);

/// Creates an damage penalty effect
nw::Effect* effect_damage_penalty(nw::Damage type, nw::DiceRoll dice);

/// Creates an damage reduction effect
nw::Effect* effect_damage_reduction(int value, int power, int max = 0);

/// Creates an damage resistance effect
nw::Effect* effect_damage_resistance(nw::Damage type, int value, int max = 0);

/// Creates a haste effect
nw::Effect* effect_haste();

/// Creates temporary hitpoints effect
nw::Effect* effect_hitpoints_temporary(int amount);

/// Creates miss chance effect
nw::Effect* effect_miss_chance(int value, nw::MissChanceType type = miss_chance_type_normal);

/// Creates an skill modifier effect
nw::Effect* effect_save_modifier(nw::Save save, int modifier, nw::SaveVersus vs = nw::SaveVersus::invalid());

/// Creates an skill modifier effect
nw::Effect* effect_skill_modifier(nw::Skill skill, int modifier);

// ============================================================================
// == Item Properties =========================================================
// ============================================================================

/// Creates ability modifier item property
nw::ItemProperty itemprop_ability_modifier(nw::Ability ability, int modifier);

/// Creates armor modifier item property
nw::ItemProperty itemprop_armor_class_modifier(int value);

/// Creates attack modifier item property
nw::ItemProperty itemprop_attack_modifier(int value);

/// Creates bonus spell slot property
nw::ItemProperty itemprop_bonus_spell_slot(nw::Class class_, int spell_level);

/// Creates damage bonus item property
nw::ItemProperty itemprop_damage_bonus(int value);

/// Creates enhancement modifier item property
nw::ItemProperty itemprop_enhancement_modifier(int value);

/// Creates haste item property
nw::ItemProperty itemprop_haste();

/// Creates keen item property
nw::ItemProperty itemprop_keen();

/// Creates save modifier item property
nw::ItemProperty itemprop_save_modifier(nw::Save type, int modifier);

/// Creates save versus modifier item property
nw::ItemProperty itemprop_save_vs_modifier(nw::SaveVersus type, int modifier);

/// Creates skill modifier item property
nw::ItemProperty itemprop_skill_modifier(nw::Skill skill, int modifier);

} // namespace nwn1
