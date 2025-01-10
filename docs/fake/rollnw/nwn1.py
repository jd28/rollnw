from . import (
    Area,
    AttackData,
    Creature,
    DamageCategory,
    DiceRoll,
    Door,
    Effect,
    Encounter,
    Item,
    ItemProperty,
    ObjectBase,
    ObjectHandle,
    Placeable,
    Store,
    Trigger,
    Waypoint,
)

from typing import List, Optional, Tuple

# == CORE SCRIPT API ==========================================================
# =============================================================================


def apply_effect(obj: "ObjectBase", effect: "Effect") -> bool:
    """
    Applies an effect to an object.

    Args:
        obj: The object to which the effect is applied.
        effect: The effect to apply.

    Returns:
        bool: True if the effect was successfully applied, False otherwise.
    """
    pass


def has_effect_applied(obj: "ObjectBase", type: int, subtype: int = -1) -> bool:
    """
    Determines if an effect type is applied to an object.

    Args:
        obj: The object to check.
        type: The type of the effect.
        subtype: The subtype of the effect. Defaults to -1.

    Returns:
        bool: True if the effect type is applied, False otherwise.
    """
    pass


def remove_effect(obj: "ObjectBase", effect: "Effect", destroy: bool = True) -> bool:
    """
    Removes an effect from an object.

    Args:
        obj: The object from which the effect is removed.
        effect: The effect to remove.
        destroy: Whether to destroy the effect after removal. Defaults to True.

    Returns:
        bool: True if the effect was successfully removed, False otherwise.
    """
    pass


def remove_effects_by(obj: "ObjectBase", creator: "ObjectHandle") -> int:
    """
    Removes effects by creator.

    Args:
        obj: The object from which the effects are removed.
        creator: The creator of the effects.

    Returns:
        int: The number of effects removed.
    """
    pass


def count_feats_in_range(obj: "Creature", start: int, end: int) -> int:
    """
    Counts the number of known feats in the range [start, end].

    Args:
        obj: The creature to check.
        start: The starting feat in the range.
        end: The ending feat in the range.

    Returns:
        int: The number of known feats in the range.
    """
    pass


def get_all_available_feats(obj: "Creature") -> List[int]:
    """
    Gets all feats for which requirements are met.

    Args:
        obj: The creature to check.

    Returns:
        list: A list of feats for which requirements are met.
    """
    pass


def has_feat_successor(obj: "Creature", feat: int) -> Tuple[int, int]:
    """
    Gets the highest known successor feat.

    Args:
        obj: The creature to check.
        feat: The feat to find a successor for.

    Returns:
        tuple: A pair of the highest successor feat and an ``int`` representing that the feat is the nth successor.
        Returns ``(-1, 0)`` if no successor exists.
    """
    pass


def highest_feat_in_range(obj: "Creature", start: int, end: int) -> int:
    """
    Gets the highest known feat in range [start, end].

    Args:
        obj: The creature to check.
        start: The starting feat in the range.
        end: The ending feat in the range.

    Returns:
        int: The highest known feat in the range.
    """
    pass


def knows_feat(obj: "Creature", feat: int) -> bool:
    """
    Checks if an entity knows a given feat.

    Args:
        obj: The creature to check.
        feat: The feat to check.

    Returns:
        bool: True if the creature knows the feat, False otherwise.
    """
    pass


def item_has_property(item: "Item", type: int, subtype: int = -1) -> bool:
    """
    Determines if an item has a particular item property.

    Args:
        item: The item to check.
        type: The type of the property.
        subtype: The subtype of the property. Defaults to -1.

    Returns:
        bool: True if the item has the property, False otherwise.
    """
    pass


def itemprop_to_string(ip: "ItemProperty") -> str:
    """
    Converts an item property to an in-game style string.

    Args:
        ip: The item property to convert.

    Returns:
        str: The in-game style string representation of the item property.
    """
    pass

# == PROFILE SCRIPT API =======================================================
# =============================================================================

# =============================================================================
# == Object ===================================================================
# =============================================================================

# == Object: Effects ==========================================================
# =============================================================================

# == Object: Hit Points =======================================================
# =============================================================================


def get_current_hitpoints(obj: ObjectBase) -> int:
    """Gets objects current hitpoints"""


def get_max_hitpoints(obj: ObjectBase) -> int:
    """Gets objects maximum hit points."""
    pass


# == Object: Saves =============================================================
# ==============================================================================

def saving_throw(obj: ObjectBase, type: int, type_vs: int = -1,
                 versus: ObjectBase = None) -> int:
    """Determines saving throw"""


def resolve_saving_throw(obj: ObjectBase, type: int,  dc: int, type_vs: int = -1, versus: ObjectBase = None) -> bool:
    """Resolves saving throw"""
    pass

# =============================================================================
# == Creature =================================================================
# =============================================================================

# == Creature: Abilities ======================================================
# =============================================================================


def get_ability_score(obj: Creature, ability,  base: bool = False) -> int:
    """Gets creatures ability score"""
    pass


def get_ability_modifier(obj: Creature, ability,  base: bool = False) -> int:
    """Gets creatures ability modifier"""
    pass


def get_dex_modifier(obj: Creature) -> int:
    """Gets creatures dexterity modifier as modified by armor, etc."""
    pass

# == Creature: Armor Class ====================================================
# =============================================================================


def calculate_ac_versus(obj: ObjectBase, versus: Optional[ObjectBase] = None,
                        is_touch_attack: bool = False) -> int:
    """Calculate Armor Class versus another object"""
    pass


def calculate_item_ac(obj: Item) -> int:
    """Calculates the armor class of a piece of armor"""
    pass

# == Creature: Casting ========================================================
# =============================================================================


def add_known_spell(obj: Creature, class_: int, spell: int) -> bool:
    """Adds a known spell"""
    pass


def add_memorized_spell(obj: Creature, class_: int, spell: int, meta: int = 0) -> bool:
    """Adds a memorized spell"""
    pass


def compute_total_spell_slots(obj: Creature,  class_: int, spell_level: int) -> int:
    """Computes available slots that are able to be prepared or castable"""
    pass


def get_available_spell_slots(obj: Creature,  class_: int, spell_level: int) -> int:
    """Gets the number of available slots at a particular spell level"""
    pass


def get_available_spell_uses(obj: Creature, class_: int, spell: int,  min_spell_level: int = 0, meta: int = 0) -> int:
    """Gets available spell uses"""
    pass


def get_caster_level(obj: Creature, class_: int) -> int:
    """Gets creatures caster level
    """
    return 0


def get_knows_spell(obj: Creature, class_: int, spell: int) -> bool:
    """Determines if creature knows a spell"""
    pass


def get_spell_dc(obj: Creature, class_: int, spell: int) -> int:
    """Gets spell DC
    """
    return 0


def recompute_all_availabe_spell_slots(obj: Creature) -> None:
    """Recomputes all available spell slots"""
    pass


def remove_known_spell(obj: Creature, class_: int, spell: int) -> None:
    """Removes a known spell and any preparations thereof."""
    pass

# == Creature: Classes ========================================================
# =============================================================================


def can_use_monk_abilities(obj: Creature) -> Tuple[bool, int]:
    """Determines if monk class abilities are usable and monk class level"""
    pass

# == Creature: Combat =========================================================
# =============================================================================


def attacks_per_second(obj: Creature, type, versus: ObjectBase) -> float:
    """Number of attacks per second
    """
    pass


def base_attack_bonus(obj: Creature) -> int:
    """Calculates base attack bonus"""
    pass


def equip_index_to_attack_type(equip):
    """Converts an equip index to an attack type"""
    pass


def get_weapon_by_attack_type(obj: Creature, type) -> Item:
    """Gets an equipped weapon by attack type"""
    pass


def is_flanked(target: Creature, attacker: Creature) -> bool:
    """
    """
    pass


def resolve_attack(obj: Creature, type, versus: ObjectBase):
    """Resolves an attack"""
    pass


def resolve_attack_bonus(obj: Creature, type, versus: Optional[ObjectBase] = None) -> int:
    """Calculates attack bonus"""
    pass


def resolve_attack_damage(obj: Creature, versus: ObjectBase, data: AttackData) -> int:
    """Resolves damage from an attack"""
    pass


def resolve_concealment(obj: ObjectBase, type, target: ObjectBase, vs_ranged: bool) -> Tuple[int, bool]:
    """Resolves an concealment - i.e. the highest of concealment and miss chance
    """
    pass


def resolve_critical_multiplier(obj: Creature, type, versus: Optional[ObjectBase] = None) -> int:
    """Resolves critical multiplier"""
    pass


def resolve_critical_threat(obj: Creature, type) -> int:
    """Resolves critical multiplier"""
    pass


def resolve_damage_modifiers(obj: Creature, versus: ObjectBase, data: AttackData) -> None:
    """Resolves resistance, immunity, and reduction
    """
    pass


def resolve_damage_immunity(obj: ObjectBase, dmg_type, versus: Optional[ObjectBase] = None) -> int:
    """Resolves damage immunity
    """
    pass


def resolve_damage_reduction(obj: ObjectBase, power: int, versus: Optional[ObjectBase] = None) -> Tuple[int, Effect]:
    """Resolves damage reduction
    """
    pass


def resolve_damage_resistance(obj: ObjectBase,  dmg_type, versus: Optional[ObjectBase] = None) -> Tuple[int, Effect]:
    """Resolves damage resistance
    """
    pass


def resolve_dual_wield_penalty(obj: Creature) -> Tuple[int, int]:
    """ Resolves dual wield attack bonus penalty
    """
    pass


def resolve_iteration_penalty(obj: Creature, attack_type):
    """Resolves iteration attack bonus penalty
    """
    pass


def resolve_number_of_attacks(obj: Creature, offhand: bool = False) -> Tuple[int, int]:
    """Calculates number of attacks"""
    pass


def resolve_target_state(obj: Creature, versus: ObjectBase):
    """Resolves damage from an attack"""
    pass


def resolve_unarmed_damage(obj: Creature) -> DiceRoll:
    """Resolves unarmed damage"""
    pass


def resolve_weapon_damage(obj: Creature, weapon: Item) -> DiceRoll:
    """Resolves weapon damage"""
    pass


def resolve_weapon_power(obj: Creature, weapon: Item) -> int:
    """Resolves weapon power"""
    pass


def weapon_is_finessable(obj: Creature, weapon: Item) -> bool:
    """Determines if a weapon is finessable"""
    pass


def weapon_iteration(obj: Creature, weapon: Item) -> int:
    """Calculates weapon iteration, e.g. 5 or 3 for monk weapons"""
    pass

# == Creature: Equips =========================================================
# =============================================================================


def can_equip_item(obj: Creature, item: Item, slot: int) -> bool:
    """Determines if an item can be equipped"""
    pass


def equip_item(obj: Creature, item: Item, slot: int) -> bool:
    """Equip an item"""
    pass


def get_equipped_item(obj: Creature, slot) -> Item:
    """Gets an equipped item"""
    pass


def unequip_item(obj: Creature, slot: int) -> Item:
    """Unequips an item"""
    pass


# == Creature: Skills ========================================================
# ============================================================================


def get_skill_rank(obj: Creature, skill: int, versus: ObjectBase = None, base: bool = False):
    """Determines creatures skill rank"""
    pass


def resolve_skill_check(obj: Creature, skill: int,  dc: int, versus: ObjectBase = None) -> bool:
    """Resolves skill check"""
    pass

# =============================================================================
# == Effects ==================================================================
# =============================================================================


def effect_ability_modifier(ability: int, modifier: int) -> Effect:
    """Creates an ability modifier effect."""
    pass


def effect_armor_class_modifier(type: int, modifier: int) -> Effect:
    """Creates an armor class modifier effect."""
    pass


def effect_attack_modifier(attack: int, modifier: int) -> Effect:
    """Creates an attack modifier effect."""
    pass


def effect_bonus_spell_slot(class_: int, spell_level: int) -> Effect:
    """Creates a bonus spell slot effect."""
    pass


def effect_concealment(value: int, type: int = nw.miss_chance_type_normal) -> Effect:
    """Creates a concealment effect."""
    pass


def effect_damage_bonus(type: int, dice: DiceRoll, cat: DamageCategory = DamageCategory.none) -> Effect:
    """Creates a damage bonus effect."""
    pass


def effect_damage_immunity(type: int, value: int) -> Effect:
    """Creates a damage immunity effect. Negative values create a vulnerability."""
    pass


def effect_damage_penalty(type: int, dice: DiceRoll) -> Effect:
    """Creates a damage penalty effect."""
    pass


def effect_damage_reduction(value: int, power: int, max: int = 0) -> Effect:
    """Creates a damage reduction effect."""
    pass


def effect_damage_resistance(type: int, value: int, max: int = 0) -> Effect:
    """Creates a damage resistance effect."""
    pass


def effect_haste() -> Effect:
    """Creates a haste effect."""
    pass


def effect_hitpoints_temporary(amount: int) -> Effect:
    """Creates temporary hitpoints effect."""
    pass


def effect_miss_chance(value: int, type: int = 0) -> Effect:
    """Creates a miss chance effect."""
    pass


def effect_save_modifier(save: int, modifier: int, vs: int = -1) -> Effect:
    """Creates a save modifier effect."""
    pass


def effect_skill_modifier(skill: int, modifier: int) -> Effect:
    """Creates a skill modifier effect."""
    pass

# =============================================================================
# == Item Properties ==========================================================
# =============================================================================


def itemprop_ability_modifier(ability, modifier) -> ItemProperty:
    """Creates ability modifier item property"""
    pass


def itemprop_armor_class_modifier(value) -> ItemProperty:
    """Creates armor modifier item property"""
    pass


def itemprop_attack_modifier(value) -> ItemProperty:
    """Creates attack modifier item property"""
    pass


def itemprop_bonus_spell_slot(class_: int, spell_level: int) -> ItemProperty:
    """Creates bonus spell slot property"""
    pass


def itemprop_damage_bonus(type: int, value: int) -> ItemProperty:
    """Creates damage bonus item property"""
    pass


def itemprop_enhancement_modifier(value: int) -> ItemProperty:
    """Creates enhancement modifier item property"""
    pass


def itemprop_haste() -> ItemProperty:
    """Creates haste item property"""
    pass


def itemprop_keen() -> ItemProperty:
    """Creates keen item property"""
    pass


def itemprop_save_modifier(type: int, modifier: int) -> ItemProperty:
    """Creates save modifier item property"""
    pass


def itemprop_save_vs_modifier(type: int, modifier: int) -> ItemProperty:
    """Creates save versus modifier item property"""
    pass


def itemprop_skill_modifier(skill,  modifier) -> ItemProperty:
    """Creates skill modifier item property"""
    pass
