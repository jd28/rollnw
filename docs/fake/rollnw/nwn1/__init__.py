from .. import ObjectBase, ObjectHandle, Area, Creature, Door, Encounter, Item, Placeable, Store, Trigger, Waypoint
from .. import Effect, ItemProperty

# == Abilities ===============================================================
# ============================================================================


def get_ability_score(obj: Creature, ability,  base: bool = False) -> int:
    """Gets creatures ability score"""
    pass


def get_ability_modifier(obj: Creature, ability,  base: bool = False) -> int:
    """Gets creatures ability modifier"""
    pass


def get_dex_modifier(obj: Creature) -> int:
    """Gets creatures dexterity modifier as modified by armor, etc."""
    pass

# == Armor Class =============================================================
# ============================================================================


def calculate_ac_versus(obj: ObjectBase, versus: ObjectBase = None, is_touch_attack: bool = False) -> int:
    """Calculate Armor Class versus another object"""
    pass


def calculate_item_ac(obj: Item) -> int:
    """Calculates the armor class of a piece of armor"""
    pass

# == Classes =================================================================
# ============================================================================


def can_use_monk_abilities(obj: Creature) -> tuple[bool, int]:
    """Determines if monk class abilities are usable and monk class level"""
    pass

# == Combat ==================================================================
# ============================================================================


def attack_bonus(obj: Creature, type, versus=None) -> int:
    """Calculates attack bonus"""
    pass


# float attacks_per_second(const nw:: Creature * obj, const  * vs)
#     """Number of attacks per second"""


def base_attack_bonus(obj: Creature) -> int:
    """Calculates base attack bonus"""
    pass


def equip_index_to_attack_type(equip):
    """Converts an equip index to an attack type"""
    pass


def number_of_attacks(obj: Creature, offhand: bool = False) -> int:
    """Calculates number of attacks"""
    pass


def get_weapon_by_attack_type(obj: Creature, type) -> Item:
    """Gets an equipped weapon by attack type"""
    pass


def weapon_is_finessable(obj: Creature, weapon: Item) -> bool:
    """Determines if a weapon is finessable"""
    pass


def weapon_iteration(obj: Creature, weapon: Item) -> int:
    """Calculates weapon iteration, e.g. 5 or 3 for monk weapons"""
    pass

# == Effect Creation =========================================================
# ============================================================================


def effect_ability_modifier(ability, modifier) -> Effect:
    """Creates an ability modifier effect"""
    pass


def effect_armor_class_modifier(type, modifier) -> Effect:
    """Creates an armor class modifier effect"""
    pass


def effect_attack_modifier(attack, modifier) -> Effect:
    """Creates an attack modifier effect"""
    pass


def effect_haste() -> Effect:
    """Creates a haste effect"""
    pass


def effect_skill_modifier(skill, modifier) -> Effect:
    """Creates an skill modifier effect"""
    pass

# == Item Property Creation ==================================================
# ============================================================================


def itemprop_ability_modifier(ability, modifier) -> ItemProperty:
    """Creates ability modifier item property"""
    pass


def itemprop_armor_class_modifier(value) -> ItemProperty:
    """Creates armor modifier item property"""
    pass


def itemprop_attack_modifier(value) -> ItemProperty:
    """Creates attack modifier item property"""
    pass


def itemprop_enhancement_modifier(value) -> ItemProperty:
    """Creates enhancement modifier item property"""
    pass


def itemprop_haste() -> ItemProperty:
    """Creates haste item property"""
    pass


def itemprop_skill_modifier(skill,  modifier) -> ItemProperty:
    """Creates skill modifier item property"""
    pass


# == Effects =================================================================
# ============================================================================

def queue_remove_effect_by(obj: ObjectBase, creator: ObjectHandle):
    """Queues remove effect events by effect creator"""
    pass

# == Items ===================================================================
# ============================================================================


def can_equip_item(obj: Creature, item, slot):
    """Determines if an item can be equipped"""
    pass


def equip_item(obj, item, slot):
    """Equip an item"""
    pass


def get_equipped_item(obj: Creature, slot):
    """Gets an equipped item"""
    pass


def is_ranged_weapon(item: Item) -> bool:
    """Determines if weapon is ranged"""
    pass


def is_shield(baseitem) -> bool:
    """Determines if item is a shield"""
    pass


def unequip_item(obj, slot):
    """Unequips an item"""
    pass

# == Skills ==================================================================
# ============================================================================


def get_skill_rank(obj: Creature, skill, versus=None, base=False):
    """Determines creatures skill rank"""
    pass
