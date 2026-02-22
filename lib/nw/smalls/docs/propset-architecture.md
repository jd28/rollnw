# Propset Architecture

**Version**: 0.1.0
**Last Updated**: 2026-02-20

## Overview

The propset system is an ECS-inspired component architecture for game objects. Data schemas are defined in Smalls script, giving ruleset authors full control over data layout without engine recompilation. The engine remains blind to game-logic data and communicates with scripts exclusively through native function boundaries.

The design is intentionally shaped around the access pattern of RPG game logic: **discrete event simulation**, not continuous batch processing. Combat resolves one event at a time — an attack fires, two specific creatures' data is fetched, a result is computed. You never need to iterate all creatures in combat simultaneously. This means the optimization target is fast per-object lookup, not cache-coherent iteration across thousands of entities, and the slab-based pool with O(1) handle lookup is exactly right for that.

---

## Two Worlds

### Script Propsets

Game-logic data — ability scores, feats, HP, class levels — that the engine has no need to know about. Schemas are defined in `.smalls` files using the `[[propset]]` annotation. Memory is managed by `PropsetPoolManager` in slab-based pools keyed by `ObjectHandle`. Scripts access propsets through the `get_propset` intrinsic.

```go
[[propset]]
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
};
```

The engine registers propset types and populates them at object load time, but does not read or write propset fields directly thereafter. Game logic that previously lived in C++ (AC calculation, feat checks, skill rolls) migrates to script and accesses data directly through propset fields.

### Native Arrays

Engine-critical data — position, visual appearance, physics bodies — that subsystems need to iterate efficiently every frame. These are plain C++ arrays indexed by object slot, owned by the relevant subsystem. The renderer owns `Transform transforms[MAX_OBJECTS]`. The physics system owns its own arrays. The object manager is the authority on slot lifetime.

Scripts have no direct access to native arrays. They are purely a C++ implementation detail for cache-coherent batch iteration by engine subsystems.

---

## The Boundary: Bridge Functions

All communication between the script world and the engine-owned data goes through native function signatures. Scripts call `get_position(obj)` — not `get_propset(obj): Transform`. This is deliberate:

- Engine systems protect their own invariants (`set_position` can update spatial queries, trigger area transitions, notify pathfinding)
- No shared memory or type system leakage in either direction
- The script-engine boundary is entirely legible as a set of function signatures
- No "native propset" concept needs to exist in the script type system at all

Native arrays are a C++ implementation pattern, not a script-visible abstraction.

---

## Script Propset Schemas

### Creature

Four propsets covering distinct data lifetimes and computational domains. Common operations touch at most two propsets.

```go
// "What this creature is" — rarely changes, consulted by most RPG calculations
[[propset]]
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
};

// Vital state — changes in combat, persisted to disk
[[propset]]
type CreatureHealth {
    hp: int;
    hp_current: int;
    hp_max: int;
    hp_temp: int;
    faction_id: int;
    starting_package: int;
};

// Class progression — changes on level-up, source of BAB and spell slots
[[propset]]
type CreatureLevels {
    classes: int[8];       // Class IDs per slot
    class_levels: int[8];  // Level count per slot
    xp: int;
    walkrate: int;
};

// Transient combat state — resets between sessions, not persisted
[[propset]]
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
| Spell slot check | `CreatureLevels` |

Nothing requires more than two propsets. The ceiling is AC and attack resolution, which are the most complex calculations and still only need the combat state alongside the sheet.

**Excluded from propsets (v1):**
- `SpellBook` — slot arrays per class per level, too complex for v1 field types; remains as native functions (`get_spell_slots`, `get_remaining_slots`)
- `LevelHistory` — per-level-up detail (feat/skill/ability choices); player-character only, rarely accessed from script; remains serialized in C++
- `CreatureScripts` — all Resref fields (strings); event handler names, bridge-function territory

### Item

```go
[[propset]]
type ItemStats {
    base_item: int;
    cost: int;
    cost_additional: int;
    stack_size: int;
    charges: int;
    cursed: int;
    identified: int;
    plot: int;
    stolen: int;
    // item properties: deferred — requires structured array representation
};
```

Item properties (the `ItemProperty` list) are deferred. They require either a structured encoding into `array!(int)` or v2 propset field types. Native functions cover them in the interim.

### Door

```go
[[propset]]
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

```go
[[propset]]
type PlaceableState {
    hp: int;
    hp_current: int;
    hardness: int;
    locked: int;
    plot: int;
    useable: int;
    has_inventory: int;
    static_: int;
};
```

### Other Objects

Triggers, waypoints, sounds, encounters — these carry minimal runtime state. Their script interaction is primarily through event handlers and native bridge functions. Propsets are not a priority for these types.

---

## Native Array System (Future)

This section documents the intended design for when rendering and larger simulations are in scope. Nothing here needs to be built yet.

### Concept

Each engine subsystem owns flat arrays indexed by object slot. The object manager assigns slots at creation and reclaims them at destruction. Arrays are pre-allocated at startup for `MAX_OBJECTS` capacity.

```cpp
// Renderer owns these
Transform   transforms[MAX_OBJECTS];
Appearance  appearances[MAX_OBJECTS];

// Physics system owns these
PhysicsBody physics_bodies[MAX_OBJECTS];
```

Iteration is a plain loop — no query system, no archetype machinery, no indirection:

```cpp
for (uint32_t i = 0; i < active_count; ++i) {
    render(transforms[active_slots[i]], appearances[active_slots[i]]);
}
```

### Registration

A subsystem registers its array so the object manager can initialize/clear slots on object creation/destruction:

```cpp
object_manager.register_native_array<Transform>(
    transform_array,
    MAX_OBJECTS,
    /*on_create=*/[](uint32_t slot) { transform_array[slot] = {}; },
    /*on_destroy=*/[](uint32_t slot) { /* cleanup if needed */ }
);
```

No propset pool, no hash map, no GC involvement. Slot lifetime is managed by the object manager directly.

### Script Access

Scripts access native array data exclusively through bridge functions. No `get_propset` equivalent for native arrays:

```go
// core/object.smalls — bridge functions, not propset access
[[native]] fn get_position(obj: object): vec3;
[[native]] fn set_position(obj: object, pos: vec3);
[[native]] fn get_facing(obj: object): float;
[[native]] fn set_facing(obj: object, facing: float);
[[native]] fn get_area(obj: object): object;
```

The bridge function handles side effects (spatial index update, area transition check, pathfinding notify). Scripts never touch Transform memory directly.

### Separation from Script Propsets

Native arrays and script propsets are completely separate systems with no shared infrastructure:

| | Script Propsets | Native Arrays |
|---|---|---|
| Schema defined in | Smalls script | C++ struct |
| Memory managed by | `PropsetPoolManager` | Subsystem directly |
| GC integration | Yes | No |
| Access from script | `get_propset` intrinsic | Bridge functions only |
| Access from C++ | Via propset pool API | Direct array index |
| Iteration pattern | Per-object lookup | Contiguous loop |
| Optimization target | Fast single-object fetch | Cache-coherent batch |

---

## Implementation Plan

### Phase 1 — Script Propset Schemas (Current)

Define propset schemas and wire them to existing C++ data as a bootstrap layer. The C++ data remains authoritative during this phase; propsets are populated from it at load time and flushed back at save time.

1. Define `CreatureStats`, `CreatureHealth`, `CreatureLevels`, `CreatureCombat` in `core/creature.smalls`
2. Define `ItemStats` in `core/item.smalls`
3. Define `DoorState`, `PlaceableState` in respective script modules
4. Register propset types from C++ at runtime startup (via `ModuleBuilder` or equivalent)
5. In `Creature::instantiate()` — allocate and populate propsets from existing C++ struct fields
6. In serialization path — flush propset data back to GFF/JSON on save
7. Write tests: load creature → check propset fields match GFF data → save → reload → compare

**Key constraint**: C++ structs (`CreatureStats`, `CombatInfo`, etc.) are not modified in this phase. The propset is a parallel copy, not yet canonical.

### Phase 2 — Game Logic Migration (Medium-term)

Move ruleset calculations from C++ into script. Propsets become canonical; C++ structs become load/save intermediaries only.

1. Implement combat system in script using propset direct field access
2. Replace native `get_ability_score`, `has_feat`, `get_skill_rank` with script functions reading `CreatureStats`
3. Replace native `get_total_levels`, `get_level_by_class` with script functions reading `CreatureLevels`
4. C++ combat event dispatcher calls script functions rather than executing game logic itself
5. Deprecate C++ game-logic functions as script equivalents cover them
6. Schema migration story: when a propset field is added, existing saves get default zero; document the policy

### Phase 3 — Native Array Infrastructure (Pre-rendering)

Build the subsystem array infrastructure before the renderer needs it.

1. Object manager gets slot assignment and reclamation (if not already present)
2. `register_native_array<T>()` API on object manager
3. `Transform` and `Appearance` arrays registered at startup
4. Bridge functions in `core/object.smalls` replace any direct field access patterns
5. Validate that no script code assumes position/appearance data is in a propset

### Phase 4 — Renderer Integration (Future)

Renderer and physics iterate native arrays directly. No per-object virtual dispatch.

1. Renderer reads `transforms[slot]` and `appearances[slot]` in its draw loop
2. `active_slots[]` maintained by object manager (compacted list of live slots)
3. Physics system registers its own arrays
4. Spatial query structures (area octree, etc.) update through `set_position` bridge function

---

## What Is Already Built

- `PropsetPoolManager` — slab pool, slot management, dirty tracking, unmanaged array support
- `[[propset]]` annotation — parsed and validated by `TypeResolver`
- Propset field validation — only allows POD types, `[[value_type]]` structs, fixed arrays, `array!(int|float|bool)`
- `get_propset` intrinsic — declared in `core/prelude.smalls`
- `array!(int)` as propset field — IArray bridge infrastructure for `feats` and `skills`

## What Is Not Yet Built

- Propset schema definitions in `.smalls` files (Phase 1)
- C++ registration of propset types at startup (Phase 1)
- Bootstrap population of propsets from existing C++ object data (Phase 1)
- Serialization round-trip through propsets (Phase 1)
- Script-side ruleset functions using propset fields (Phase 2)
- Native array registration API (Phase 3)
- Renderer/physics integration (Phase 4)

---

## Deferred / Out of Scope (v1)

- `SpellBook` in propsets — slot-per-level-per-class structure needs richer field types or a compact encoding
- `LevelHistory` in propsets — complex nested structure, player-character only
- `string` fields in propsets — requires GC root management within unmanaged slab memory; tracked as v2
- `object` handle fields in propsets — `TypedHandle` is POD and could work inline, but the policy and type-system representation need design
- `ItemProperty` list in propsets — structured encoding TBD
- Query system (iterate all objects with propset X) — not needed at current scale; revisit if simulation scope grows
