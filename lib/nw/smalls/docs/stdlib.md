# Standard Library

The standard library ships as file-based modules under `lib/nw/smalls/scripts/core/`.

## core.prelude

- `print(message: string)`
- `println(message: string)`
- `assert(cond: bool)`
- `error(message: string)`
- `panic(message: string)`
- `gc_collect()` (debug/testing)

## core.string

Intrinsics and native helpers for strings.

- `format(fmt: string, args: array!(string)): string`
- plus `len`, `substr`, `split`, `join`, case conversion, trimming, etc.

## core.array

- Intrinsics: `push`, `pop`, `len`, `get`, `set`, `clear`, `reserve`
- Helpers: `map`, `filter`, `reduce`, `sort`

## core.map

- Intrinsics: `len`, `get`, `set`, `has`, `remove`, `clear`
- Helpers: `has_key`, `keys`, `values`

## core.math

- `sin(x: float): float`
- `cos(x: float): float`
- `sqrt(x: float): float`
- `abs(x: float): float`

## core.constants

- Newtype wrappers for rules constants (`Ability`, `ArmorClass`, `AttackType`, `Class`, `Damage`, `DamageCategory`, `MissChanceType`, `Save`, `SaveVersus`, `Skill`, `EquipIndex`).
- Named constants for the full `core.effects` surface (abilities, AC types, attack types, class ids, damage types/categories, miss chance types, saving throws, save-vs categories, and skills).

## core.effects

- Handle-backed `Effect` lifecycle API (`create`, `apply`, `is_valid`, `destroy`).
- Effect constructor APIs for engine rule effects (`ability_modifier`, `armor_class_modifier`, `attack_modifier`, `bonus_spell_slot`, `concealment`, `damage_bonus`, `damage_immunity`, `damage_penalty`, `damage_reduction`, `damage_resistance`, `haste`, `hitpoints_temporary`, `miss_chance`, `save_modifier`, `skill_modifier`).
- Constructors use `core.constants` newtypes for rule categories.
- `destroy` is a no-op for engine-owned or borrowed effects.
- `apply` is a low-level attach call and does not model equip-time item property application context.

## core.object

- Object-context effect helpers (`apply_effect`, `has_effect_applied`, `remove_effect`).

## core.item

- `ItemProperty` — native value type struct with fields: `prop_type`, `subtype`, `cost_table`, `cost_value`, `param_table`, `param_value` (all `int`).
- Item-property list helpers (`property_count`, `clear_properties`).
- `process_item_properties(creature: Creature, item: Item, equip_index: int, remove: bool): int` — single entry point for item property effect application/removal. Called from C++ `process_item_properties`.
- Type-specific generator hooks:
  - `set_generator_for_type(prop_type: int, fn(ItemProperty): effects.Effect)` registers a type-specific translator.
  - `clear_generator_for_type(prop_type: int)` removes one typed translator.
  - `clear_generators()` clears all typed translators.
- Generator resolution: type-specific generator first; if none, falls back to C++ `EffectSystem::generate()`.
- `ip_gen_ability_modifier(ip: ItemProperty): effects.Effect` — proof-of-concept smalls generator for ability bonus/penalty item properties.
- `__ip_cost_table_int(prop_type: int, cost_value: int, column: string): int` — native helper to look up cost table integer values.

## core.creature

- Creature helpers (`get_ability_score`, `get_ability_modifier`, `can_equip_item`, `equip_item`, `get_equipped_item`, `unequip_item`).
- Ability APIs use `core.constants.Ability` newtypes.
- Equip APIs use `core.constants.EquipIndex` newtypes.

## Object Type Stubs

- Lightweight placeholder modules for future object-type APIs: `core.area`, `core.door`, `core.encounter`, `core.module`, `core.placeable`, `core.player`, `core.sound`, `core.store`, `core.trigger`, `core.waypoint`.
- Each exposes native object helpers: `is_valid(obj)`, module-specific `is_<type>(obj)`, module-specific `as_<type>(obj)`, and `type_id()`.
- First-pass interaction helpers are also available per module (for example `core.area.get_width/get_height`, `core.door.get_hp_current`, `core.module.get_area_count`, `core.sound.get_sound_count`, `core.trigger.get_geometry_point_count`).
