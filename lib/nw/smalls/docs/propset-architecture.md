# Propset Architecture

- **Version**: 0.3.0
- **Last Updated**: 2026-08-01
- **Status**: Normative target architecture

## Overview

The propset system is the profile-owned half of the object data architecture.
Data schemas are defined in Smalls, giving ruleset authors control over game
and rules data layout without engine recompilation. C++ owns object lifetime,
native components, native ABI values and operations, serialization mechanics,
and renderer-facing row storage. Runtime C++ must not reach into propset fields
for game policy; profile scripts read propsets directly and communicate native
side effects through explicit native functions.

The package-layer and cross-boundary rules in
[`conventions.md`](conventions.md) apply throughout this document. Profile root
resolution and schema bootstrap are defined in
[`profile-packages.md`](profile-packages.md).

The dominant rules access pattern is discrete event processing: an attack or
interaction resolves a small known set of object handles. Propset storage
therefore supports direct handle lookup. System boundaries still use plural
batch transforms; one event is the same path with a small row count. Table
import, serialization, visual publication, and tool projections remain linear
batch operations rather than repeated scalar bridge calls.

---

## Normative Ownership Rule

`[[native]]`, `[[propset]]`, and a C++ object component describe three
different boundaries. They are not interchangeable storage annotations.

| Kind | Defined by | Owns | Smalls access |
|---|---|---|---|
| Native ABI function or value | `core.*` declaration plus C++ registration | A copied value or invariant-preserving engine operation | Typed call or copied value |
| Native object component | C++ | Engine data required directly by native systems or protected by native ownership/lifetime invariants | Native ABI operations only |
| Profile propset | Selected profile's `<root>.propsets` module | Object-local game schema and script policy state | `get_propset!` |
| Profile rules/config batch | Selected profile module | Shared interpretation, tuning, and policy tables | Ordinary Smalls values |
| Toolset projection | Selected toolset module | Replaceable editor state, labels, filters, and rows | RmlUi data-model binding |

Every production `[[propset]]` belongs to the selected profile's single
`<root>.propsets` module. `core.*` contains the native ABI and reusable
profile-neutral implementations, but no propsets, profile constants, table
interpretation, or profile imports. Propsets still under `core.*` or another
profile module are migration work, not precedent.

Qualified propset names are serialized protocol identifiers. Moving a propset
from `core.item.ItemStats` to `nwn1.propsets.ItemStats` changes the JSON section
key deliberately. Serialization reads and writes only the target names; older
archives must be reimported from their source format. There is no runtime alias,
fallback lookup, or duplicate schema.

A `[[native]]` value type may be stored as a field or array element inside a
profile propset when it is the exact copied value exchanged with C++. The field
type does not transfer ownership of the containing schema to `core.*`.

There is no "native propset" category. Engine-owned component storage remains
C++ data and is exposed through native operations. Profile-owned propset
storage remains a Smalls schema even though `PropsetPoolManager` allocates its
memory.

### Classification Procedure

Classify a datum from its real readers, writers, frequency, and invariants:

1. List every current reader and writer. Separate serialization/conversion
   from runtime consumption.
2. If a native subsystem must consume the datum directly, or a write must
   preserve native ownership, spatial, renderer, inventory, or lifetime
   invariants, use a native component plus typed native operations.
3. If the selected game defines the schema and normal reads and writes are
   rules-policy operations, use a profile propset.
4. If C++ and Smalls exchange one copied fixed-shape value, define a native
   value type in the relevant `core.*` module. Do not create a component only
   to expose that value.
5. If the datum is shared immutable rules data, partition it by consumer:
   native addressing/storage facts become native `Info` rows and script policy
   becomes profile `Rules` rows. Join them by a stable scalar key.
6. If the datum exists only to display or interact with an editor, it is a
   toolset projection, not object storage.

Do not classify from names such as "visual," "stats," "rules," model prefixes,
or source table columns. The consumer and required invariant decide the owner.

### Reference Classifications

| Datum | Owner | Reason |
|---|---|---|
| Position, orientation, area membership | Native spatial component | Native spatial/pathing systems consume it and writes have side effects |
| Inventory ownership and equipment slots | Native inventory components | Ownership and occupancy must change atomically |
| Resolved model/light rows | Native visual component | Renderer-facing protocol, already resolved before consumption |
| Item model parts and color indices | Native item visual component | Fixed native layout with renderer invalidation invariants |
| `ItemProperty` row value | Native `core.item` value | Exact copied row exchanged across the ABI |
| Item descriptor, stats, and property collection | `nwn1.propsets` | NWN object schema and policy state |
| Base-item model/icon/layout facts | Native `BaseItemInfo` facts | Native asset addressing and inventory geometry |
| Base-item combat and requirement policy | `nwn1.item` rules/config | Moddable game behavior |
| Creature ability scores, feats, classes, and scripts | Profile propsets | Profile-defined object schema consumed by rules |
| Editor rows, filters, selected tab, and labels | Toolset Smalls | Replaceable authoring projection |

### Cost Of Each Choice

The ownership choice determines where cost is paid:

| Owner | Runtime cost | Maintenance and migration cost |
|---|---|---|
| Native component | Native allocation/access and explicit component serialization | C++ rebuild, fixed layout/versioning, typed bridge and invariant tests |
| Profile propset | Handle lookup plus profile-script reads/writes; dynamic fields carry managed lifetime work | Qualified schema key, profile bootstrap, reimport when the schema name changes |
| Native ABI value/fact | Value conversion or copy across the VM boundary | Registered layout and explicit range/error contract |
| Native `Info` plus profile `Rules` split | One import pass per projection and a scalar-key join at use sites | Each source field must have exactly one runtime owner |
| Toolset projection | Rebuild on invalidation and viewport-bounded RmlUi materialization | No durable object migration; workflow remains replaceable |

No general performance ranking is implied. Use the simplest owner that serves
the observed consumers and preserves the required invariant. If a proposed
move is performance-motivated, measure the representative access pattern
before and after; ownership alone is not evidence of speed.

### Cross-Boundary Protocol

Native operations consume and produce copied values or homogeneous batches:

```text
profile propsets + profile Rules + native Info
    -> profile Smalls resolution
    -> copied resolved row batch
    -> invariant-preserving native component mutation
    -> renderer, inventory, spatial, or other native consumer
```

The producer owns source data. Values crossing the ABI are copied; no pointer
into a propset pool, VM arena, profile table, or C++ container crosses the
boundary. The receiving native component owns accepted rows for the live
object lifetime. Stable scalar IDs join native facts and profile policy.

Plural batch operations are the default. A singular operation is a thin
`count = 1` use of the same validation and mutation path unless the target is
a true singleton such as the selected profile.

Invalid or stale object handles reject the operation. Invalid row counts,
out-of-range values, duplicate destinations, and stale before-values reject
the complete batch without partial mutation. A missing or invalid required
profile schema fails profile initialization. Durable JSON with an obsolete or
missing qualified propset/component section fails load; it does not select a
fallback owner.

---

## Two Worlds

### Script Propsets

Game-logic data such as descriptors, appearance source data, ability scores,
feats, item stats, and object state belongs to the selected profile. Schemas
are defined in profile `.smalls` files using the
`[[propset(ObjectType)]]` annotation. Memory is managed by
`PropsetPoolManager` in slab-based pools keyed by `ObjectHandle`. Scripts
access propsets through the `get_propset!` intrinsic.

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

The engine registers propset types, initializes them for object handles, and
imports/exports them at serialization boundaries. Generic JSON reflection and
profile legacy-format conversion may read or write propsets as data transforms.
Runtime policy lives in Smalls. C++ runtime reads from propsets are temporary
compatibility bridges or conversion code, not the long-term architecture.

The required `<root>.propsets` module is loaded once by the selected profile's
runtime bootstrap. It does not depend on the optional rules init module or an
editor import. Missing or invalid required schemas fail runtime initialization
before any object can be loaded.

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
// core.item: native ABI
[[native]]
type ItemProperty {
    prop_type: int;
    subtype: int;
    cost_table: int;
    cost_value: int;
    param_table: int;
    param_value: int;
    tag: string;
};
```

```smalls
// nwn1.propsets: selected profile persistence protocol
from core.types import { TextRef };

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
    cost: int;
    cost_additional: int;
    stack_size: int;
    charges: int;
    cursed: int;
    identified: int;
    plot: int;
    stolen: int;
};
```

`core.item.ItemProperty` is a native struct value stored in the native
`ObjectItemPropertyState` component. `nwn1.item` reads an ordered snapshot,
writes a whole replacement batch, and registers NWN1 property/effect row
constructors. `core.item.process_item_properties` performs the reusable batch
dispatch on equip/unequip. C++ owns only the bounded storage widths, snapshot
transfer, and effect application/removal boundary.

Item model parts and PLT colors live in the native `ObjectItemVisualState`
component. Smalls reads and mutates that fixed layout through `core.item` batch
functions so visual-change policy remains scriptable without reflecting storage
as a propset. Component/propset JSON requires the native
`components.item_properties` and `components.item_visuals` sections and writes
only native component rows. Archives with former propset representations are
rejected and must be reimported.

The visual component contract is one contiguous row per live item, owned by
`ObjectComponentSystem` for the item's lifetime:

- `model_colors`: exactly 6 unsigned 8-bit values.
- `model_parts`: exactly 19 unsigned 16-bit values.
- `part_colors`: exactly 114 unsigned 8-bit values in part-major order; `255`
  means inherit the corresponding model color.

JSON load and Smalls mutation write whole batches into this layout. Wrong
counts, negative values, overflow, and duplicate batch destinations are
rejected without partial writes. Legacy GFF import converts valid part fields
into one complete row; negative or overflowing part values are dropped and
remain at the zero default. JSON with the former `ItemVisuals` propset and no
native component is rejected; reimport is the migration path.

The property component is one handle-indexed row with an ordered contiguous
array. Each entry contains unsigned 16-bit type/subtype/cost-value fields,
unsigned 8-bit table/parameter fields, and a string tag. Reads copy one
snapshot in O(property count); writes validate and replace the complete batch
without partial mutation. JSON rejects missing fields, signed values, and
overflow. Legacy GFF import drops malformed property rows and imports every
well-formed row in source order.

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
    Resref hold_animation;
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
[[native]] fn set_visual_hold_animation(obj: object, animation: ResRef): bool;
[[native]] fn add_visual_model_row(
    obj: object,
    model: ResRef,
    plt_texture: ResRef,
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
| GC integration | Direct heap fields are tracked as roots; unmanaged arrays are destroyed with the object row | Component-specific C++ ownership |
| Access from script | `get_propset!` intrinsic | Bridge functions only |
| Runtime C++ policy access | Avoid; use Smalls or native rows | Direct C++ component API |
| Serialization | Generic propset JSON plus legacy GFF import/export policies | Fixed component JSON/GFF sections |
| Optimization target | Fast single-object fetch | Explicit C++ data protocol and optional component storage |

---

## Current Implementation

- `PropsetPoolManager` — slab pool, slot management, dirty tracking, unmanaged array support
- `[[propset(ObjectType)]]` annotation — parsed and validated by `TypeResolver`
- Propset field validation — allows supported primitives, strings, object handles, native values, value structs, fixed arrays, and unmanaged arrays
- `get_propset!` intrinsic — declared in `core/prelude.smalls`
- Object-type propset registration at runtime startup
- GFF import/export through `PropsetGffImporter` and `PropsetGffExporter`
- Generic component/propset JSON through `object_to_component_propset_json` and `object_from_component_propset_json`
- Durable propset JSON sections keyed by qualified Smalls type name
- `[[transient]]` skip policy for durable JSON output
- Object component JSON sections for native components such as spatial, local data, geometry, inventory, visuals, and ability loadout
- Script-side rules for combat, modifiers, item property processing, spell slot/known-spell logic, creature sizing, and visual row resolution
- Visual asset protocol from Smalls resolvers into `ObjectVisualState`

All 16 persistent and transient NWN1 propsets live in the single
`nwn1.propsets` schema module. Shared `core.*` modules contain no propsets.

**Key constraint**: while a C++ mirror still has runtime consumers, remove or
redirect those consumers before deleting the mirror. Do not add new runtime
policy reads from propsets in C++; use profile Smalls functions, native
components, or explicit row protocols as the boundary. Generic reflection,
serialization, and profile format conversion are data transforms and may
inspect registered schemas without owning their policy.

---

## Refactoring Procedure

Apply this procedure to one object domain at a time:

1. **Observe the data.** Record the source representation, representative
   counts and ranges, all runtime readers and writers, mutation frequency,
   serialization keys, and required side effects.
2. **Choose one owner per datum.** Classify every field as native component,
   native ABI value/fact, profile propset/rules data, or toolset projection.
   A source column or object field cannot have two authoritative runtime
   representations.
3. **Define the boundary batch.** Native reads return copied values or
   contiguous value batches. Native mutations accept homogeneous batches,
   validate the complete batch, and either apply every row or reject every
   row.
4. **Move policy before storage.** Redirect runtime policy consumers to the
   profile API before deleting a C++ mirror. Redirect native subsystem
   consumers to an explicit resolved row/component before deleting their
   source bridge.
5. **Move persistence in the same slice.** Update exact qualified propset
   names, native component sections, legacy import/export policy, fixtures,
   and round-trip tests together. Do not add compatibility aliases.
6. **Remove the old path.** Delete duplicate storage, fallback reads, old
   qualified names, editor mirrors, and policy branches. A migration is not
   complete while both representations can load.
7. **Verify the common path.** Test load, live mutation, save/reload,
   invalid/stale input rejection, and any renderer or ownership side effects.
   Measure only when the change makes a performance claim.

For every proposed native operation, write its concrete input and output and
the invariant it preserves. If the only output is presentation state or a
formatted editor row, the operation belongs in toolset Smalls instead.

For every proposed propset field, identify the profile policy that reads or
writes it. If only a native subsystem consumes it, the datum belongs in an
explicit native component or fact protocol instead.

### Refactoring Completion Test

A domain migration is complete when:

- each persisted datum has one authoritative section;
- all propsets are declared and loaded from the selected profile's single
  `<root>.propsets` module;
- `core.*` contains only native ABI and reusable profile-neutral implementations
  for that domain, with no profile propsets, constants, or imports;
- native components are mutated only through operations that preserve their
  invariants;
- runtime C++ contains no profile-policy propset reads;
- profile Smalls contains no renderer, RmlUi, or native storage knowledge;
- toolset SmallS/RML can replace the editor presentation without changing the
  data owner; and
- old JSON fails explicitly rather than selecting a compatibility path.

---

## Deferred / Out of Scope (v1)

- Spell preparation/loadout in propsets — slot-per-level-per-class runtime rows
  stay in the native `ObjectAbilityLoadout` component for now; NWN1 `SpellBook`
  remains only a legacy GFF list adapter.
- Full `LevelHistory` in propsets — player-character compatibility detail; only the class-slot projection is currently in `CreatureLevels`
- Persistent profile use of general `object` handle fields — the language
  supports immediate object values, but durable identity and stale-reference
  policy remain unresolved
- Area/module conversion to propsets — left mostly C++/format-owned until their object/subresource boundaries are clearer
- Schema migration policy beyond zero/default initialization for newly added fields
- Query system (iterate all objects with propset X) — not needed at current scale; revisit if simulation scope grows
