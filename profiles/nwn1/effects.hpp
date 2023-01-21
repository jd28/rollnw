#pragma once

#include "constants.hpp"
#include "nw/objects/Equips.hpp"
#include "nw/rules/combat.hpp"

namespace nw {
struct Effect;
struct ItemProperty;
struct ObjectBase;
} // namespace nw

namespace nwn1 {

// == Loaders =================================================================
// ============================================================================

void load_effects();
void load_itemprop_generators();

// == Effect Creation =========================================================
// ============================================================================

/// Creates an ability modifier effect
nw::Effect* effect_ability_modifier(nw::Ability ability, int modifier);

/// Creates an armor class modifier effect
nw::Effect* effect_armor_class_modifier(nw::ArmorClass type, int modifier);

/// Creates an attack modifier effect
nw::Effect* effect_attack_modifier(nw::AttackType attack, int modifier);

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

// == Effect Apply/Remove =====================================================
// ============================================================================

bool effect_apply_is_creature(nw::ObjectBase* obj, const nw::Effect*);
bool effect_remove_is_creature(nw::ObjectBase* obj, const nw::Effect*);

bool effect_apply_is_valid(nw::ObjectBase* obj, const nw::Effect*);
bool effect_remove_is_valid(nw::ObjectBase* obj, const nw::Effect*);

bool effect_haste_apply(nw::ObjectBase* obj, const nw::Effect*);
bool effect_haste_remove(nw::ObjectBase* obj, const nw::Effect*);

bool effect_hitpoints_temp_apply(nw::ObjectBase* obj, const nw::Effect* effect);
bool effect_hitpoints_temp_remove(nw::ObjectBase* obj, const nw::Effect* effect);

// == Item Property Creation ==================================================
// ============================================================================

/// Creates ability modifier item property
nw::ItemProperty itemprop_ability_modifier(nw::Ability ability, int modifier);

/// Creates armor modifier item property
nw::ItemProperty itemprop_armor_class_modifier(int value);

/// Creates attack modifier item property
nw::ItemProperty itemprop_attack_modifier(int value);

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

// == Item Property Generators ================================================
// ============================================================================

nw::Effect* ip_gen_ability_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem);
nw::Effect* ip_gen_ac_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem baseitem);
nw::Effect* ip_gen_attack_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem);
nw::Effect* ip_gen_damage_bonus(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem);
nw::Effect* ip_gen_enhancement_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem);
nw::Effect* ip_gen_haste(const nw::ItemProperty&, nw::EquipIndex, nw::BaseItem);
nw::Effect* ip_gen_save_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem);
nw::Effect* ip_gen_save_vs_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem);
nw::Effect* ip_gen_skill_modifier(const nw::ItemProperty& ip, nw::EquipIndex, nw::BaseItem);

} // namespace nwn1
