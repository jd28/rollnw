# Ability use pipeline: spells, feats, and truthful telegraphs

Tier 1. Implementation design, 2026-11-23. Covers the runtime path for *using*
an ability. Spellbook storage, slot authoring and legacy conversion are already
owned by [spellbook storage cutover](server-spellbook-storage-cutover.md); this
plan consumes that store and does not change it.

## Frame and actual boundaries

There is no runtime casting today. Everything around it exists: spell data
lookups, the full spellbook store, slot accounting, metamagic, saving throws,
effects, and the modifier framework. What is missing is the verb — nothing
validates a use, spends a slot, resolves it against targets, and applies the
result.

In scope: a profile-agnostic "use ability" action in core, a dispatch registry
in the nwn1 profile, and the contract between them. Out of scope: implementing
individual spells, concentration/interruption rules, and the spatial query that
area resolution needs (own tier, see below).

Plan B if the action layer proves the wrong host: the same profile contract can
be driven from a server intent without the tick state machine, at the cost of
losing cast timing. The contract below is deliberately independent of who calls
it.

## Patterns and conventions found

- `nw::ObjectAbilityLoadoutEntry` (`objects/ObjectComponentSystem.hpp:199`) is
  already profile-agnostic: `source / tier / slot / ability / modifier / flags`.
  The nwn1 profile reads those as `class_id / spell_level / slot / spell_id /
  metamagic`. Core stores and validates abilities without knowing what a spell
  is. **This is the "use ability of <type>, type defined by the profile" shape
  already built — it needs a verb, not new storage.**
- `gameplay/actions.hpp` is a 60 Hz tick state machine over `ActionKind` /
  `ActionPhase` / `ActorActionState`, carrying `target` and `destination`
  per row, and already has `ActionStatus::profile_failed`. It is the natural
  host for cast time, interruption and recovery.
- `profiles/nwn1/scriptbridge.hpp` is the core-to-profile seam:
  `call_nwn1_module_{int,bool,void,value}(module, fn, args)`, resolve-by-name.
- `nwn1/on_hit.smalls` is the existing dispatch pattern: a handler array keyed
  on type/subtype, a `payload: fn(...)` taking **every input as an argument**,
  and partial results merged by the caller. It is already the shape this plan
  wants; ability dispatch should look like it rather than invent a second idiom.
- Already implemented and reusable, not to be rewritten: `get_spell_dc`
  (`creature.smalls:2128`, including all three Spell Focus tiers via master
  feats), `get_caster_level` (`:2144`), `get_available_spell_uses` (`:2182`),
  `effective_spell_level` (`:2206`), `remove_memorized_spell` (`:2557`),
  `saving_throws.resolve_saving_throw` (`:91`), and the native
  `core.object.apply_effect`.
- `Const.mod_type_spell_resistance` (17) and `mod_type_spell_immunity` (16) are
  declared and consumed by nothing. This matters: in this codebase declared
  rules stay dead until a consumer exists — Spell Focus sat registered in
  `init.smalls` doing nothing until `get_spell_dc` read it. Spell resistance
  should land *in* this pipeline, not before it, or it becomes a second dead
  declaration with no way to exercise it.

## Architecture decision

### The context is passed, never ambient

A handler is a pure function of an explicit context struct. There is no
`GetSpellDC()`, no `GetSpellTargetObject()`, no `GetSpellCastItem()` — if a
handler needs a value, it is a field on the input.

This is a deliberate rejection of the NWScript model, for three reasons: hidden
per-invocation globals make handlers untestable without a live world; they make
behaviour order-dependent on an invisible stack; and they force every new input
to become another global accessor. A struct makes the full input surface of an
ability visible in one place and lets handlers be unit-tested as data in, data
out.

### Handlers describe, the pipeline applies

Handlers return effects and presentation requests; they do not call
`apply_effect` or spend slots themselves. The pipeline owns application. This
keeps interruption uniform (nothing to unwind), keeps handlers side-effect
free, and means a dry run — "what would this do?" — costs nothing extra, which
the toolset wants for preview.

### Two phases: cast snapshot, impact resolution

Caster-derived values are snapshotted **at cast**; the affected set is resolved
**at impact**. These are different ticks for anything with travel or delay, and
conflating them is wrong in both directions: the caster can be buffed, drained
or killed between the two, and targets move.

- Cast snapshot: `caster_level`, `save_dc`, `effective_level`, `source`,
  `tier`, `modifier`. Fixed at the moment of casting, per tabletop semantics.
- Impact resolution: the affected object set, and the per-target saving throws
  and resistance checks that follow from it.

For instantaneous self and single-target abilities both phases collapse into
one tick, which is why those can ship before the spatial query exists.

## Component design and data flow

```
ActionKind::use_ability                          core C++, actions.cpp
   │ requested → casting → impact → recovering
   │ owns timing, interruption, the loadout row, durable admission
   │ knows nothing about spells
   ├─ can_use(owner, row, target)        ─┐
   ├─ snapshot(owner, row)                ├─ scriptbridge, resolve-by-name
   └─ resolve_impact(snapshot, targets)  ─┘
                                                  nwn1/ability.smalls   (new)
                                                   │ builds context
                                                   │ dispatches on (domain, ability)
                                                   ▼
                                      handler: fn(AbilityUse): AbilityUseResult
```

Profile types, following `on_hit.smalls`:

```smalls
[[value_type]]
type AbilityUse {
    domain: int; ability: int;        // profile-defined meaning
    source: int; tier: int; modifier: int;
    caster: Creature;
    target: object;                   // invalid for point/area
    has_point: bool;
    point_x: float; point_y: float; point_z: float;
    affected: array!(object);         // filled by the pipeline at impact
    caster_level: int;                // cast-time snapshot
    effective_level: int;
    save_dc: int;
    is_hostile: bool;
};

[[value_type]]
type AbilityUseResult {
    handled: bool;
    consume_use: bool;                     // pipeline spends the slot
    effects: array!(EffectApplication);    // { target, effect }
    vfx: array!(VfxRequest);
};

type AbilityHandler {
    tag: string; domain: int; ability: int;   // ability -1 = any in domain
    payload: fn(AbilityUse): AbilityUseResult;
};

fn register_handler(tag, domain, ability, payload);
fn resolve_use(use: AbilityUse): AbilityUseResult;
```

`domain` is an opaque `int32_t` to core, exactly as `source`/`tier` already
are. Core never learns that 0 means spell.

## Targeting and the spatial query boundary

Target collection, not casting, is what the missing spatial query blocks:

| Mode | Target set known from | Blocked |
| --- | --- | --- |
| self | the actor | no |
| single target | `ActorActionRequest.target` | no |
| point / ground | must be discovered **at impact** | yes |
| area around caster or target | must be discovered **at impact** | yes |

Point targeting is *not* unblocked by the point arriving in `destination`. The
point is only an origin; resolving the ability still means asking what is near
it at the moment of impact. Anything whose effect reaches objects it was not
handed must query.

No spatial query exists anywhere today. The `radius` fields in `nw/nav` are
navmesh agent-erosion classes, and the `area_objects` collections in the
renderer and workspace graphs are scene and authoring lists, not a gameplay
index. Positions exist (`ObjectSpatialState`, `location()`); the index and the
area-scoped iteration do not.

That query is its own tier and its own design: a broadphase structure, area
scoping, invalidation as objects move, and — separately — line-of-sight and
wall clipping, which is nav/geometry work rather than indexing. Do not design
the area context here. Adding `affected: array!(object)` later, populated by the
pipeline, costs nothing: handlers that iterate a target list do not care how it
was filled.

## Telegraphs and indicators

Area abilities need the player to see what they are about to hit. The bar to
clear is NWN:EE's area indicators, which are extruded translucent primitives
drawn over the scene: they float rather than meeting the floor, pass straight
through walls and furniture so the drawn region implies reach the spell does not
have, z-fight and obscure what is underneath, become unreadable where two
shapes overlap, and — the real failure — communicate a *shape* while never
showing *which creatures are actually affected*, which is the only thing the
player needs to decide.

The architectural requirement that prevents this class of problem:

**One shape definition drives both the telegraph and the impact query.** The
ability's shape (kind, dimensions, origin, orientation) is authored once in the
profile, sent to the client for preview, and consumed by the server to collect
targets. If the preview geometry and the resolution geometry are separate code
paths they will diverge, and an indicator that lies is worse than none.

Consequences for the renderer, to be designed with the spatial query rather
than now:

- Project the region onto the ground as a decal that conforms to the floor,
  rather than intersecting an extruded mesh with the world.
- Clip the drawn region by the same walkmesh and line-of-sight the resolution
  uses, so what is drawn is what is hit.
- Highlight the affected creatures, not only the region. That is the decision
  information; the shape is context for it.
- Keep edges soft and fills light enough to read the scene underneath, and
  make overlapping telegraphs legible.

The toolset wants the same preview for authoring, so the indicator is not a
play-mode-only concern.

## Implementation map and build order

1. **Pipeline, self and single-target only.** `ActionKind::use_ability` plus
   phases; `nwn1/ability.smalls` with the types, registry and `resolve_use`;
   two scriptbridge entry points; stub handlers proving dispatch end to end.
   Everything it needs already exists.
2. **Spell resistance and immunity, inside the pipeline.** Gives
   `mod_type_spell_resistance` its first consumer, and places the check once
   rather than in every handler. Requires choosing where a creature's SR comes
   from — no racial, effect or item-property source exists yet.
3. **Spatial query.** Own design, own tier. Blocks everything below.
4. **Point and area targeting**, and the telegraph that shares their shape
   definition.

## Cost and simplification

Genuinely new code is three things: one `ActionKind` and its phases, one smalls
module, two bridge entry points. Everything else — DC, caster level, slot
accounting, metamagic, saves, effects, modifiers — is reused as-is. If this
plan grows a fourth new subsystem, it has drifted.

## Open decisions

- **Who spends the use.** Recommend the pipeline, on `consume_use`, so an
  interrupted cast needs no per-handler unwind.
- **Where spell resistance sits.** Recommend the pipeline before dispatch.
- **Where a creature's SR comes from** — racial, effect, item property. None
  exist; one must be built with step 2.
- **Whether `domain` is an enum in the profile or a bare int everywhere.**
  Core must stay opaque either way.

## What this plan deliberately does not do

- It does not implement any individual spell. The first handlers exist to prove
  dispatch, not to be content.
- It does not design the spatial query or the area context. Guessing at that
  interface before the query exists would bake the guess into the context
  struct.
- It does not touch spellbook storage, slot authoring, or legacy conversion.
