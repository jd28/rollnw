from typing import Any, Tuple

import rollnw


def base_attack_bonus(arg0: rollnw.Creature) -> int: ...
def calculate_ac_versus(obj: rollnw.ObjectBase, versus: rollnw.ObjectBase = ...,
                        is_touch_attack: bool = ...) -> int: ...


def calculate_item_ac(arg0: rollnw.Item) -> int: ...
def can_equip_item(arg0: rollnw.Creature, arg1: rollnw.Item,
                   arg2: rollnw.EquipIndex) -> bool: ...


def can_use_monk_abilities(arg0: rollnw.Creature) -> Tuple[bool, int]: ...
def effect_ability_modifier(arg0: Ability, arg1: int) -> rollnw.Effect: ...
def effect_armor_class_modifier(arg0, arg1: int) -> rollnw.Effect: ...
def effect_attack_modifier(arg0: AttackType, arg1: int) -> rollnw.Effect: ...
def effect_haste() -> rollnw.Effect: ...
def effect_skill_modifier(arg0: Skill, arg1: int) -> rollnw.Effect: ...
def equip_index_to_attack_type(arg0: rollnw.EquipIndex) -> AttackType: ...


def equip_item(arg0: rollnw.Creature, arg1: rollnw.Item,
               arg2: rollnw.EquipIndex) -> bool: ...


def get_ability_modifier(obj: rollnw.Creature,
                         ability: Ability, base: bool = ...) -> int: ...
def get_ability_score(obj: rollnw.Creature,
                      ability: Ability, base: bool = ...) -> int: ...


def get_caster_level(obj: rollnw.Creature, class_: int) -> int: ...
def get_dex_modifier(arg0: rollnw.Creature) -> int: ...


def get_equipped_item(arg0: rollnw.Creature,
                      arg1: rollnw.EquipIndex) -> rollnw.Item: ...


def get_skill_rank(arg0: rollnw.Creature, arg1: Skill,
                   arg2: rollnw.ObjectBase, arg3: bool) -> int: ...


def get_spell_dc(obj: rollnw.Creature, class_: int, spell: int) -> int: ...


def get_weapon_by_attack_type(
    arg0: rollnw.Creature, arg1: AttackType) -> rollnw.Item: ...


def is_flanked(arg0: rollnw.Creature, arg1: rollnw.Creature) -> bool: ...
def is_ranged_weapon(arg0: rollnw.Item) -> bool: ...
def is_shield(arg0) -> bool: ...
def itemprop_ability_modifier(
    arg0: Ability, arg1: int) -> rollnw.ItemProperty: ...


def itemprop_armor_class_modifier(arg0: int) -> rollnw.ItemProperty: ...
def itemprop_attack_modifier(arg0: int) -> rollnw.ItemProperty: ...
def itemprop_enhancement_modifier(arg0: int) -> rollnw.ItemProperty: ...
def itemprop_haste() -> rollnw.ItemProperty: ...
def itemprop_skill_modifier(arg0: Skill, arg1: int) -> rollnw.ItemProperty: ...


def resolve_attack(arg0: rollnw.Creature,
                   arg1: rollnw.ObjectBase) -> rollnw.AttackData: ...


def resolve_attack_bonus(obj: rollnw.Creature, type: AttackType,
                         versus: rollnw.ObjectBase = ...) -> int: ...


def resolve_attack_damage(
    arg0: rollnw.Creature, arg1: rollnw.ObjectBase, arg2: rollnw.AttackData) -> int: ...
def resolve_attack_roll(arg0: rollnw.Creature,
                        arg1: rollnw.ObjectBase) -> rollnw.AttackData: ...


def resolve_attack_type(arg0: rollnw.Creature) -> AttackType: ...


def resolve_concealment(arg0: rollnw.ObjectBase,
                        arg1: rollnw.ObjectBase, arg2: bool) -> Tuple[int, bool]: ...


def resolve_critical_multiplier(
    arg0: rollnw.Creature, arg1: AttackType, arg2: rollnw.ObjectBase) -> int: ...


def resolve_critical_threat(arg0: rollnw.Creature,
                            arg1: AttackType) -> int: ...


def resolve_damage_immunity(arg0: rollnw.ObjectBase,
                            arg1, arg2: rollnw.ObjectBase) -> int: ...


def resolve_damage_modifiers(
    arg0: rollnw.Creature, arg1: rollnw.ObjectBase, arg2: rollnw.AttackData) -> None: ...


def resolve_damage_reduction(arg0: rollnw.ObjectBase, arg1: int,
                             arg2: rollnw.ObjectBase) -> Tuple[int, rollnw.Effect]: ...
def resolve_damage_resistance(arg0: rollnw.ObjectBase, arg1,
                              arg2: rollnw.ObjectBase) -> Tuple[int, rollnw.Effect]: ...


def resolve_dual_wield_penalty(arg0: rollnw.Creature) -> Tuple[int, int]: ...
def resolve_iteration_penalty(
    arg0: rollnw.Creature, arg1: AttackType) -> int: ...


def resolve_number_of_attacks(obj: rollnw.Creature) -> Tuple[int, int]: ...
def resolve_target_state(*args, **kwargs) -> Any: ...
def resolve_unarmed_damage(arg0: rollnw.Creature) -> rollnw.DiceRoll: ...
def resolve_weapon_damage(arg0: rollnw.Creature, arg1) -> rollnw.DiceRoll: ...
def resolve_weapon_damage_flags(*args, **kwargs) -> Any: ...
def resolve_weapon_power(arg0: rollnw.Creature, arg1: rollnw.Item) -> int: ...
def unequip_item(arg0: rollnw.Creature,
                 arg1: rollnw.EquipIndex) -> rollnw.Item: ...


def weapon_is_finessable(arg0: rollnw.Creature, arg1: rollnw.Item) -> bool: ...
def weapon_iteration(arg0: rollnw.Creature, arg1: rollnw.Item) -> int: ...
