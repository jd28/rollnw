# Propset Architecture

**Version**: 0.1.0
**Last Updated**: 2026-07-13

## Overview

The propset system is the script-owned half of the object data architecture. Data schemas are defined in Smalls script, giving ruleset authors control over game and rules data layout without engine recompilation. C++ owns object lifetime, native components, serialization boundaries, and renderer-facing row storage. Runtime C++ should not reach into propset fields for game policy; scripts read propsets directly and communicate native side effects through explicit native functions.

The design is intentionally shaped around the access pattern of RPG game logic: **discrete event simulation**, not continuous batch processing. Combat resolves one event at a time — an attack fires, two specific creatures' data is fetched, a result is computed. You never need to iterate all creatures in combat simultaneously. This means the optimization target is fast per-object lookup, not cache-coherent iteration across thousands of entities, and the slab-based pool with O(1) handle lookup is exactly right for that.

---

## Two Worlds

### Script Propsets

Game-logic data — descriptors, appearance source data, ability scores, feats, item stats, object state — that script policy owns. Schemas are defined in `.smalls` files using the `[[propset(ObjectType)]]` annotation. Memory is managed by `PropsetPoolManager` in slab-based pools keyed by `ObjectHandle`. Scripts access propsets through the `get_propset` intrinsic.

```smalls
[[propset(Creature)]]
type CreatureStats {
    abilities: int[6];
    save_fort: int;
    save_reflex: int;
    save_will: int;
    skills: array!(int);
    feats: array!(int);
    race: int;
    gender: int;
    good_evil: int;
    lawful_chaotic: int;
    ac_natural_bonus: int;
    cr: float;
    cr_adjust: int;
    perception_range: int;
    disarmable: int;
    immortal: int;
    interruptable: int;
    lootable: int;
    pc: int;
    plot: int;
    chunk_death: int;
    bodybag: int;
    special_abilities: array!(SpecialAbility);
};
```

The engine registers propset types, initializes them for object handles, and imports/exports them at serialization boundaries. Generic JSON and legacy GFF conversion may read/write propsets as data transforms. Runtime policy should live in Smalls; C++ runtime reads from propsets are temporary compatibility bridges or conversion code, not the long-term architecture.

### Native Object Components

Engine-critical and engine-owned data — spatial state, local variables, geometry, visual rows, inventories, store inventories, item layout, vitals, and ability loadout rows — lives in `ObjectComponentSystem`. Components are C++ storage rows keyed by `ObjectHandle`. Some are contiguous vectors for common component data; inventories are lazily allocated because many objects do not carry one.

Scripts have no direct memory access to native components. They communicate through native functions that preserve C++ invariants. Equipping, inventory mutation, position changes, and visual row emission are engine transactions, not propset field writes.

Visual appearance follows the same rule: scripts emit explicit visual rows through bridge functions, and C++ stores those rows in object components for renderer/tooling consumption. The model/light/icon handoff is described in the [Visual Asset Protocol](../../render/docs/visual_asset_protocol.md).

---

## The Boundary: Bridge Functions

All communication between the script world and the engine-owned data goes through native function signatures. Scripts call `get_position(obj)` — not `get_propset(obj): Transform`. This is deliberate:

- Engine systems protect their own invariants (`set_position` can update spatial queries, trigger area transitions, notify pathfinding)
- No shared memory or type system leakage in either direction
- The script-engine boundary is entirely legible as a set of function signatures
- No "native propset" concept needs to exist in the script type system at all

Native components are a C++ implementation pattern, not a script-visible abstraction.

---

## Script Propset Schemas

### Creature

Creature data is split by lifetime and access pattern. Cold descriptor and appearance source data stay separate from rules stats, health, level progression, and transient combat/cache rows.

```smalls
[[propset(Creature)]]
type CreatureDescriptor {
    on_attacked: ResRef;
    on_blocked: ResRef;
    on_conversation: ResRef;
    // ... other event scripts ...

    conversation: ResRef;
    description: TextRef;
    name_first: TextRef;
    name_last: TextRef;
    deity: string;
    subrace: string;
    soundset: int;
    decay_time: int;
};

[[propset(Creature)]]
type CreatureAppearance {
    appearance: int;
    phenotype: int;
    tail: int;
    wings: int;
    portrait: ResRef;
    portrait_id: int;

    color_hair: int;
    color_skin: int;
    color_tattoo1: int;
    color_tattoo2: int;

    body_part_belt: int;
    // ... remaining body part fields ...
    body_part_torso: int;
};

[[propset(Creature)]]
type CreatureStats {
    abilities: int[6];       // STR=0, DEX=1, CON=2, INT=3, WIS=4, CHA=5
    save_fort: int;
    save_reflex: int;
    save_will: int;
    skills: array!(int);     // indexed by Skill newtype value
    feats: array!(int);      // Feat newtype values
    race: int;
    gender: int;
    good_evil: int;          // 0=evil, 50=neutral, 100=good
    lawful_chaotic: int;     // 0=chaotic, 50=neutral, 100=lawful
    ac_natural_bonus: int;
    cr: float;
    cr_adjust: int;
    perception_range: int;
    disarmable: int;
    immortal: int;
    interruptable: int;
    lootable: int;
    pc: int;
    plot: int;
    chunk_death: int;
    bodybag: int;
    special_abilities: array!(SpecialAbility);
};

[[propset(Creature)]]
type CreatureHealth {
    hp: int;
    hp_base_for_max: int;
    hp_current: int;
    hp_max: int;
    hp_temp: int;
    faction_id: int;
    starting_package: int;
};

[[propset(Creature)]]
type CreatureLevels {
    classes: int[8];       // Class IDs per slot
    class_levels: int[8];  // Level count per slot
    levelup_classes: array!(int);
    xp: int;
    walkrate: int;
};

[[transient, propset(Creature)]]
type CreatureCombat {
    attack_current: int;
    attacks_onhand: int;
    attacks_offhand: int;
    attacks_extra: int;
    combat_mode: int;
    ac_armor_base: int;
    ac_shield_base: int;
    size_ab_modifier: int;
    size_ac_modifier: int;
    target_distance_sq: float;
    target_state: int;
    hasted: int;
    size: int;
    // ... round/epoch/weapon cache fields ...
};

[[transient, propset(Creature)]]
type CreatureCombatCache {
    // Cached combat modifier rows and epoch keys.
};
```

**Common operation join cost:**

| Operation | Propsets |
|---|---|
| Ability check | `CreatureStats` |
| Has feat | `CreatureStats` |
| Skill check | `CreatureStats` |
| Apply damage | `CreatureHealth` |
| Death check | `CreatureHealth` |
| Save roll | `CreatureStats` |
| BAB / attack roll | `CreatureLevels` + `CreatureCombat` |
| AC calculation | `CreatureStats` + `CreatureCombat` |
| Spell slot check | `CreatureLevels` + native ability loadout |
| Visual model resolution | `CreatureAppearance` + equipment/item propsets |

Most common rules operations touch one or two propsets. Complex combat paths may also consult equipment, item propsets, native ability loadout rows, or transient caches. Keep those joins explicit in Smalls instead of hiding them behind C++ object members.

`[[transient]]` propsets are runtime/cache rows. Component/propset JSON
initializes them from defaults on load and does not write them to durable
fixtures or saves.

**Excluded from propsets (v1):**
- Spell preparation/loadout rows — mutable runtime state owned by the native
  `ObjectAbilityLoadout` component; legacy NWN1 `SpellBook` lists exist only
  as GFF import/export compatibility.
- `LevelHistory` detail beyond class slots — feat/skill/ability choices remain
  player-character compatibility data. `CreatureLevels.levelup_classes` carries
  the class-slot projection used by current script rules.
- Equipped items and inventory ownership — native components, with Smalls hooks
  for policy and visual/effect updates.

### Item

```smalls
[[native]]
type ItemProperty {
    prop_type: int;
    subtype: int;
    cost_table: int;
    cost_value: int;
    param_table: int;
    param_value: int;
};

[[propset(Item)]]
type ItemDescriptor {
    description: TextRef;
    description_id: TextRef;
};

[[propset(Item)]]
type ItemStats {
    base_item: int;
    armor_id: int;
    armor_dex_bonus: int;
    armor_dex_bonus_valid: int;
    armor_ac_bonus: int;
    armor_ac_bonus_valid: int;
    item_properties: array!(ItemProperty);
    cost: int;
    cost_additional: int;
    stack_size: int;
    charges: int;
    cursed: int;
    identified: int;
    plot: int;
    stolen: int;
};

[[propset(Item)]]
type ItemVisuals {
    model_colors: int[item_model_color_count];
    model_parts: int[item_model_part_count];
    part_colors: int[item_part_color_count];
};
```

`ItemProperty` is a native struct value stored in `ItemStats.item_properties`.
Smalls processes properties into effect rows on equip/unequip. C++ still owns the
effect application/removal boundary and the inventory/equipment transaction.

### Door

```smalls
[[propset(Door)]]
type DoorState {
    hp: int;
    hp_current: int;
    hardness: int;
    locked: int;
    lock_dc: int;
    bash_dc: int;
    open_state: int;
    plot: int;
    interruptable: int;
};
```

### Placeable

```smalls
[[propset(Placeable)]]
type PlaceableState {
    hp: int;
    hp_current: int;
    hardness: int;
    locked: int;
    plot: int;
    useable: int;
    has_inventory: int;
    static: int;
    appearance: int;
    light_color: int;
};
```

### Other Objects

These now have minimal object-type-scoped propsets:

| Object | Propset |
|---|---|
| Encounter | `EncounterState`, with `array!(EncounterSpawn)` |
| Sound | `SoundState` |
| Store | `StoreState` |
| Trigger | `TriggerState` |
| Waypoint | `WaypointState` |

Area and module data remain mostly C++/format-owned for now. Their contents
reference sub-objects, components, and module-level resources rather than a
single object-local rules state row.

---

## Native Object Component System

### Concept

C++ object components hold engine-owned data and explicit cross-system protocols. They are keyed by `ObjectHandle`, owned by `ObjectComponentSystem`, and accessed through native functions or C++ component APIs.

```cpp
struct ObjectSpatialState {
    ObjectHandle owner{};
    ObjectID area = object_invalid;
    glm::vec3 position{0.0f};
    glm::vec3 orientation{0.0f};
    glm::vec3 scale{1.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angular_velocity{0.0f};
    uint32_t flags = 0;
};

struct ObjectVisualState {
    ObjectHandle owner{};
    Vector<ObjectVisualModel> models;
    Vector<ObjectVisualLight> lights;
    PltColors base_plt_colors{};
    uint32_t base_plt_color_mask = 0;
    int32_t appearance = -1;
    int32_t body_variant = 0;
};
```

Current native component families:

| Component | Owner / Reason |
|---|---|
| Spatial | position, orientation, area, velocity, future physics integration |
| Local data | dynamic script variables; sparse and mutable |
| Vitals | engine-facing current/max HP bridge |
| Geometry | triggers, encounters, spawn points, highlight bounds |
| Visual | script-emitted model/light rows for renderer/tooling |
| Item layout | inventory dimensions for items |
| Inventory / store inventory | ownership grids; lazily allocated |
| Ability loadout | known/slotted/unslotted ability rows |

The common case is not “all objects have all components.” Components are created when an object actually needs them. This keeps empty objects cheap and makes optional systems visible in code.

### Script Access

Scripts access native component data exclusively through bridge functions. No `get_propset` equivalent exists for native components:

```smalls
// core/object.smalls — bridge functions, not propset access
[[native]] fn get_position(obj: object): vec3;
[[native]] fn set_position(obj: object, pos: vec3);
[[native]] fn get_facing(obj: object): float;
[[native]] fn set_facing(obj: object, facing: float);
[[native]] fn get_area(obj: object): object;
```

The bridge function handles side effects such as spatial state updates, object transfer policy, pathing hooks, or renderer row invalidation. Scripts never touch component memory directly.

Visual rows use the same boundary:

```smalls
[[native]] fn clear_visual(obj: object, appearance: int): bool;
[[native]] fn add_visual_model_row(
    obj: object,
    model: ResRef,
    attach_to: ResRef,
    attach_from: ResRef,
    kind: int,
    slot: int,
    part: int,
    source_part: int,
    model_part: int,
    flags: int): bool;
```

### Separation from Script Propsets

Native components and script propsets are separate storage systems:

| | Script Propsets | Native Components |
|---|---|---|
| Schema defined in | Smalls script | C++ struct |
| Memory managed by | `PropsetPoolManager` | `ObjectComponentSystem` / component owner |
| GC integration | Propsets are not GC-scanned; unmanaged arrays are cleaned on object destruction | No |
| Access from script | `get_propset` intrinsic | Bridge functions only |
| Runtime C++ policy access | Avoid; use Smalls or native rows | Direct C++ component API |
| Serialization | Generic propset JSON plus legacy GFF import/export policies | Fixed component JSON/GFF sections |
| Optimization target | Fast single-object fetch | Explicit C++ data protocol and optional component storage |

---

## Current Implementation

- `PropsetPoolManager` — slab pool, slot management, dirty tracking, unmanaged array support
- `[[propset(ObjectType)]]` annotation — parsed and validated by `TypeResolver`
- Propset field validation — allows POD types, `string`, `[[value_type]]` structs, fixed arrays, and supported unmanaged arrays
- `get_propset` intrinsic — declared in `core/prelude.smalls`
- Object-type propset registration at runtime startup
- GFF import/export through `PropsetGffImporter` and `PropsetGffExporter`
- Generic component/propset JSON through `object_to_component_propset_json` and `object_from_component_propset_json`
- Durable propset JSON sections keyed by qualified Smalls type name
- `[[transient]]` skip policy for durable JSON output
- Object component JSON sections for native components such as spatial, local data, geometry, inventory, visuals, and ability loadout
- Script-side rules for combat, modifiers, item property processing, spell slot/known-spell logic, creature sizing, and visual row resolution
- Visual asset protocol from Smalls resolvers into `ObjectVisualState`

**Key constraint**: while a C++ mirror still has runtime consumers, remove those consumers before deleting the mirror. Do not add new runtime reads from propsets in C++; use Smalls functions, native components, or explicit row protocols as the boundary.

---

## Deferred / Out of Scope (v1)

- Spell preparation/loadout in propsets — slot-per-level-per-class runtime rows
  stay in the native `ObjectAbilityLoadout` component for now; NWN1 `SpellBook`
  remains only a legacy GFF list adapter.
- Full `LevelHistory` in propsets — player-character compatibility detail; only the class-slot projection is currently in `CreatureLevels`
- General `object` handle fields in propsets — needs policy for lifetime, serialization, and type-system representation
- Area/module conversion to propsets — left mostly C++/format-owned until their object/subresource boundaries are clearer
- Schema migration policy beyond zero/default initialization for newly added fields
- Query system (iterate all objects with propset X) — not needed at current scale; revisit if simulation scope grows
