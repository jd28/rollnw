# Isolate the NWN profile from the native core

Status: open. Tier 2 architecture migration.

## Problem and scope

Native engine consumers still interpret NWN schemas or call NWN profile code.
The goal is a native core that builds, initializes, and exercises its systems
without linking the NWN profile or loading NWN data, while preserving the NWN
import, authoring, and runtime workflows.

This issue captures the architecture review of the `server` branch on
2026-10-28. Some cited server paths and contracts are ahead of `main`; reconcile
the inventory against the implementation branch before beginning work.

ASSUMPTION: isolation means removing profile interpretation and dependencies
from native consumers, not providing arbitrary object types, equipment layouts,
or terrain systems. This determines the migration boundary.

## Observed data and platform

- Existing C++ engine, owner-thread mutation, process-wide kernel, and Smalls VM.
- Inputs are legacy GFF documents, native component/propset archives, selected
  profile configuration, and live object identities. Outputs are native owned
  component rows, profile-owned propsets, and copied consumer projections.
- The common supported case remains NWN content. Object batches vary in size;
  exact lifecycle batch distributions have not been measured. No throughput or
  latency improvement is claimed by this migration.
- Profile schemas and source interpretation change independently of consumer
  layouts. Object state changes during loading, editing, and gameplay; published
  native facts must have explicit invalidation and refresh boundaries.
- Read source archives and profile state; write runtime components, requested
  views, and explicitly requested archive exports. Runtime preparation must not
  write source documents or implicitly traverse unrelated inventories.

## Current boundary violations

- `objects/Item.cpp`: lifecycle and deserialization call
  `nwn1::materialize_item_native_components` directly.
- `objects/*.cpp`: NWN GFF interpretation, compatibility fields, name previews,
  inventory conversion, and player history are embedded in native object code.
- `gameplay/actions.cpp`: action commits call NWN equipment policy and bridge
  notifications.
- `nav/NavGeometry.cpp`: door obstacle state is read from the named
  `nwn1.propsets.DoorState` schema.
- `server/workspace_views.cpp`: display capture interprets NWN propsets and
  invokes NWN ground-rotation and toolset projection helpers.
- `server/view_data.hpp`: shared view rows include a profile-namespaced sound
  projection type.
- `smalls/native/core_creature.cpp`: the core ABI calls NWN body-part assembly.
- `kernel/Rules.hpp`: native fact tables coexist with profile-shaped catalogs
  and rule-domain limits; classify by actual consumers before moving them.
- `smalls/runtime.hpp`: blueprint initialization includes race, class, and
  base-item inputs. Audit this contract as well as explicitly named dependencies.
- `lib/nw/CMakeLists.txt`: core and NWN adapter sources share the library target.

## Target ownership and dependency contract

Application composition selects the profile and adapters. NWN profile and source
adapters may depend on the native core; the core must not depend on them.

| Data or transform | Owner |
|---|---|
| NWN GFF object interpretation and export | NWN compatibility adapter |
| Rules, eligibility, progression, appearance resolution | Selected profile |
| Ownership, occupancy, spatial invariants, resolved visuals | Native systems |
| NWN model and walkmesh interpretation | Explicit source adapters |
| Wire encoding, rendering, path queries | Profile-independent consumers |

Batch boundary: live object identity rows plus loaded profile state produce
validated native component rows and parallel status rows. Input borrows end at
return; accepted components own their data. No VM pointers or propset references
cross into retained native storage. Stale identities, malformed batches, invalid
ranges, and missing required hooks reject explicitly. Each boundary documents
whether atomicity is per object or whole batch; no implicit partial success.

Profile selection is a true process-lifetime configuration singleton. Data
transforms remain plural; singular entry points use a batch of size one.
Existing object pointers may be borrowed during lifecycle/conversion calls
because current APIs require them, but must not become retained consumer state.
Document any remaining pointer-heavy hot path and why indexed rows are insufficient.

## Migration milestones

### 1. Inventory and guard the dependency boundary

Trace current readers, writers, initialization order, and error behavior for
each dependency. Record its destination and enforcement rule. Add checks for
forbidden includes, calls, and hardcoded schema/module names in designated core
paths. Checks supplement compilation and semantic review; name matching alone
does not establish isolation.

Gate: every observed dependency has an owner and migration destination.
Cost: inspection and build-check maintenance; small relative to later stages.

### 2. Consolidate native preparation behind selected-profile hooks

Start with items. Reuse the runtime's validated profile-hook mechanism for a
batch preparation stage after propset loading and before dependent native
instantiation. Inventory layout must exist before inventory contents instantiate;
moving preparation to the existing post-instantiation callback is insufficient.

Consolidate repeated preparation across load/instantiate into one defined stage.
Retain explicit refresh operations for subsequent edits. Required preparation
failure prevents successful instantiation and cleans up the failed load.

Gate: native item lifecycle has no NWN calls; GFF and native archive inputs yield
equivalent native components. Test ordering, repeated calls, and failed preparation.
Cost: moderate lifecycle integration; ordering and partial-failure regressions
are the principal risks.

### 3. Remove profile interpretation from actions and navigation

Keep timing, storage validation, and ownership commits native. Selected-profile
operations supply equipment eligibility and post-commit rule updates. Reuse
existing equipment prepare/apply boundaries. Preserve explicit committed status
when a notification fails after mutation; do not imply rollback.

Have profile/adapters supply resolved door obstacle-state rows. Navigation must
consume prepared geometry and state without loading profile modules or looking
up propset fields.

Gate: action and navigation consumers contain no NWN policy calls or propset
interpretation; ownership, timing, rejection, and door-transition checks pass.
Cost: moderate cross-system integration and regression tests.

### 4. Make display capture consume prepared projections

Move ground rotation, marker resolution, sound display facts, and requested-name
interpretation into profile projection producers. Consumers receive copied,
owned rows. Move shared types only after checking that their fields express real
consumer needs; renaming a profile type is not isolation.

Gate: capture uses prepared inputs, retains its lifetime and bounds guarantees,
and does not instantiate objects, traverse stock, or implicitly load documents.
Preserve wire meaning or deliberately version changed contracts.
Cost: moderate producer/caller migration and protocol compatibility checking.

### 5. Extract compatibility conversion from native objects

Move NWN object conversion bodies into adapter translation units. Generic GFF
parsing can remain reusable without interpreting NWN object fields. Dispatch
formats at loading boundaries:

```text
NWN archive -> NWN adapter -> native components + profile propsets
native archive -> component/propset reader -> the same runtime stores
```

Compatibility-facing convenience entry points may remain while callers migrate,
but must not force the core target to link NWN converters.

Gate: existing object-family round trips and propset parity pass; native archive
loading does not require NWN converters.
Cost: broad code movement and substantial regression checking; likely the largest
stage by touched surface.

### 6. Audit semantic dependencies and split build targets

Classify Rules catalogs, native ABI helpers, blueprint defaults, and rule limits
from their actual consumers. Keep native addressing/geometry facts native; move
NWN interpretation, body assembly, and profile-only limits to their owners.
Do not duplicate runtime ownership to preserve old access patterns.

Separate core, NWN profile, and compatibility/source-adapter targets once the
dependency direction permits it. Application targets compose required features.

Gate: a small test profile supplies different policy and prepared visual data
without NWN catalogs. Core builds and links with NWN targets excluded.
Cost: depends on the consumer inventory; potentially larger than explicit-call
cleanup. Estimate after milestone 1 rather than inventing a schedule.

## Simplification and exclusions

Reuse existing hooks, native mutation APIs, and row protocols. Resolve policy
once at publication/preparation boundaries instead of making consumers repeat
schema lookups. Do not introduce a general plugin framework, property bag,
replacement object system, or speculative capability matrix.

Retain current object categories, 18-slot equipment storage, tiled areas, and
single-world kernel ownership for this migration. They remain documented engine
constraints, not proof of a fully game-neutral engine. Changing them requires
separate workload evidence and migration plans. Offline asset compilation is
also separate work.

Keep intermediate stages buildable. If extraction requires broad unrelated
rewrites, retain an explicit adapter boundary temporarily and record the remaining
dependency here rather than silently broadening the migration.

## Final verification and acceptance

- [ ] Core builds and links with NWN profile and compatibility targets excluded.
- [ ] A minimal test profile boots without NWN assets and exercises object
      preparation, inventory/actions, navigation, visuals, and native serialization.
- [ ] NWN import/export parity and representative module workflows remain intact.
- [ ] Invalid rows, stale identities, missing hooks, and preparation failures have
      explicit tested outcomes; post-commit failures preserve truthful status.
- [ ] Native consumers do not interpret profile schemas or source-format policy.
- [ ] Batch ownership, lifetime, range, atomicity, and invalidation contracts exist.
- [ ] Remaining pointer-heavy hot paths have written justifications.
- [ ] No speculative generality or unmeasured performance claims were introduced.
- [ ] Remaining design questions are recorded as local issues or explicit open
      questions here before implementation proceeds past their boundary.

This is a plan, not implementation evidence. Baseline and final verification
must identify the exact source revision, build configuration, tests, and any
unverified integration paths.
