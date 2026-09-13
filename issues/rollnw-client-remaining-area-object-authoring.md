# rollnw Client Remaining Area Object Authoring

Status: implemented with automated verification (2026-09-11). A manual desktop
interaction pass and sanitizer run remain unverified in this environment.

Tier 2. This plan completes area-instance authoring for Door, Waypoint, Sound,
Store, Trigger, and Encounter. Creature, Placeable, and Item already use the
area placement, transform, membership, undo, renderer, and CAF-save path recorded
in [rollnw-client-area-object-placement.md](rollnw-client-area-object-placement.md).

## Frame

The project can create and edit all nine NWN blueprint types, and CAF already
stores all nine instance arrays. Only Creature, Placeable, and Item blueprints
can currently be dropped into an Area, moved, duplicated, deleted, undone, and
saved through the structural authoring path. The result is an incomplete loop:
the project tree can create the other six resources but cannot author their Area
instances.

The required output is NWN-compatible Area instance data:

- Waypoint is a freely placed point object with its existing model preview.
- Store is a freely placed point object represented by a toolset-only waypoint
  marker.
- Sound is a freely placed point object represented by a selectable toolset
  marker and its debug distance dome.
- Door is a model placed only at a compatible tileset door hook when created,
  moved, or redone.
- Trigger is a user-authored polygon with a root location and local vertices.
- Encounter is a user-authored polygon plus independently authored spawn points.

For all six types, the complete structural loop is placement, selection,
whole-object movement, duplicate, delete, undo/redo, native CAF save, and reload.
Trigger and Encounter placement includes drawing the initial polygon. Encounter
also includes add, move, and delete spawn-point commands. Per-vertex reshaping of
an already committed polygon is a later operation because its interaction and
undo data differ from whole-object placement and movement.

The useful limit is one active Area and one active pointer gesture. Runtime
gameplay placement, inventory/world transfers, tile painting, new-Area creation,
multi-object transforms, and arbitrary property editing are outside this work.
If one spatial class blocks the release, the completed classes can ship in the
phase order below because they share file and membership contracts without
sharing gesture state.

## User decisions

- NWN compatibility controls serialized identity and placement behavior.
- Doors snap to tileset door hooks. Existing imported off-hook Doors remain
  readable and saveable; a new placement or move must resolve a valid hook.
- Sounds use the NWToolset-style debug dome.
- Stores use the same toolset point-marker presentation as Waypoints.
- C++ does not inspect or mutate profile propsets. Any profile-owned values
  required for a visual or placement decision cross through an existing or
  narrow typed profile operation.
- No `std::map` is added. Fixed tables use `std::array`; indexed variable data
  uses contiguous vectors and offsets. An Abseil container is used only if an
  observed lookup cannot be served by those layouts.

## Real platform and fixed constraints

The platform is the desktop client on the kernel/UI thread with one live
`Area`, one `ObjectManager`, one `ViewerSession`, and one active pointer gesture.
The renderer already owns model records for Door and Waypoint and debug-shape
ranges for Trigger and Encounter. CAF serialization already owns separate typed
vectors for every Area object kind.

Three fixed properties constrain the design:

1. Live object and propset mutation stays on the kernel/UI thread. Renderer and
   undo data may copy flat rows, but they do not retain object pointers.
2. Area tiles are 10 m squares with a tileset-defined height step and one of four
   authored orientations. Door hooks originate in each tile's SET definition and
   must pass through the same tile world transform as the tile model.
3. Blueprint Trigger and Encounter files deliberately omit instance geometry.
   Their polygon and Encounter spawn points are created at Area placement time
   and live in native object components.

No background process or parallel worker is planned. These are interactive
single-Area transforms, and the observed per-Area counts are small. Pointer
queries must complete on the UI thread; scene rebuilding occurs once after a
commit, undo, or redo. Measurements below decide whether any later indexing or
off-thread preparation is warranted.

## Observed data

The read-only corpus is The Awakening's 604 native CAF files. Counts describe
instances, not blueprints:

| Kind | Instances | Areas containing kind | Maximum in one Area | Stored spatial payload |
| --- | ---: | ---: | ---: | --- |
| Door | 1,791 | 453 | 67 | location/orientation plus Door state |
| Waypoint | 1,331 | 398 | 36 | location/orientation plus appearance |
| Sound | 340 | 68 | 48 | point location plus sound distance fields |
| Store | 198 | 26 | 69 | point location plus potentially large nested inventory |
| Trigger | 550 | 254 | 13 | root location, local polygon, highlight height |
| Encounter | 3,512 | 427 | 51 | root location, local polygon, absolute spawn points |

Trigger polygons contain 3--49 vertices with a median of 4. Encounter geometry
contains 1--34 vertices with a median of 4; imported rows with fewer than three
vertices are therefore real input and must remain loadable. Encounter spawn-point
counts are 0--5 with a median of 1. The largest observed combined Trigger and
Encounter polygon payload in one Area is 217 vertices.

All 340 observed Sound orientations are zero. Sound `distance_min` is finite in
the range 0.2--5 m with a median of 1 m; `distance_max` is 2--50 m with a median
of 10 m. Of those rows, 191 are positional, 98 are non-positional, and 51 are
positional with random positioning enabled.

Blueprint samples for all six kinds contain object/profile state and no Area
location. Trigger and Encounter blueprints contain neither polygon nor spawn
points. Store blueprints can contain nested inventory graphs, which placement
must retain without visiting or copying each item in the pointer loop.

Stable data for a gesture is the active Area identity, tile rows, tileset SET
data, blueprint bytes, loaded object identity, model resources, and the Area
mutation epoch. Pointer positions and a region's uncommitted vertices are
volatile. Sound distances and object appearance are stable for one gesture and
may change through a separate editor command afterward.

The solution reads project-tree resource identity, live Area/tile data,
tileset definitions, native object components, and narrow profile projections.
It writes live typed Area membership, spatial/geometry components, undo rows,
renderer rows, and the existing dirty flag. Only ordinary Area save writes CAF;
dragging and undo do not write project files.

## Assumptions and evidence gates

ASSUMPTION: Store uses the same toolset point-marker geometry and scale as the
Waypoint fallback marker, with a distinct selection category/color — affects
presentation and picking only; Store serialization remains unchanged.

ASSUMPTION: the Sound dome represents `distance_max` around a positional Sound,
with `distance_min` shown as an inner ring; non-positional Sounds show only the
point marker — affects the debug geometry transform. Before implementation, one
classic NWToolset observation or an authoritative format/runtime source must
confirm the role of `elevation`, `random_x`, and `random_y`. Unknown semantics are
displayed as a diagnostic and are not guessed from field names.

ASSUMPTION: SET door sections use the ordinary per-tile slot fields for type,
position, and orientation — affects `TilesetRegistry` parsing. The first phase
must demand actual SET bytes, record the exact keys/ranges, and compare computed
world hooks with real Door locations before the placement API is enabled.

ASSUMPTION: Trigger/Encounter creation follows the NWToolset interaction of
clicking polygon vertices and completing the polygon with a double click —
affects UI event routing only. The stored output remains a root location plus
local vertices regardless of the completion gesture.

Encounter rows with zero spawn points are valid because they occur in the real
corpus. Placement never fabricates a spawn point. The author may add spawn
points explicitly before or after committing the Encounter.

## Batch contracts

### Tileset door slots

Extend the cold tileset data with one contiguous door-slot array per tile:

```text
TileDoorSlot {
    local_position: float3
    local_orientation_degrees: float
    door_type: int32
}

Tile {
    existing model/path fields
    door_slots: TileDoorSlot[]
}
```

`TilesetRegistry` owns these rows for the tileset lifetime. SET counts and field
values must be present, finite, and representable. A malformed slot is rejected
with its tileset/tile/slot identity; it is never zero-filled into a plausible
hook. A malformed tile's hook list does not invalidate its existing render model.

The cold Area transform builds:

```text
AreaDoorHookSnapshot {
    tile_offsets: uint32[area_tile_count + 1]
    hooks: AreaDoorHookRow[]
}

AreaDoorHookRow {
    world_position: float3
    world_orientation: float3
    tile_index: uint32
    slot_index: uint16
    door_type: int32
    occupied: uint8
}
```

Rows are sorted by tile index and slot index. `tile_offsets` provides a direct
range for the pointer's tile and adjacent tiles. The gesture owns the snapshot
until the Area structure/tile epoch changes. Invalid tile IDs, orientations,
slot indices, transforms, non-finite coordinates, and count overflow reject the
snapshot. Existing Doors are joined to hooks by spatial tolerance only while
building the occupancy rows; their names and model names are never consulted.

This is a batch transform from all Area tile rows plus all selected tile door
slots to flat hook rows. The single pointer query is a batch of one over a small
contiguous tile range. No Door, tile, or model pointer crosses the snapshot
boundary.

### Placement payload

Extend the existing placement batch rather than creating six singular loaders:

```text
AreaObjectPlacementRow {
    resource: Resource
    transform: ObjectTransformState
    geometry_begin/count: uint32
    spawn_begin/count: uint32
}

AreaObjectPlacementBatch {
    rows: AreaObjectPlacementRow[]
    geometry_points: float3[]
    spawn_points: ObjectSpawnPoint[]
}
```

Point/model objects use zero geometry/spawn counts. Trigger uses geometry only.
Encounter may use both. All spans are borrowed for the synchronous load call.
The output remains an equally ordered array of detached live handles owned by
the caller until membership commit. Loading or component initialization failure
destroys the complete loaded prefix.

Resource type selects one of the fixed nine native object types. Geometry ranges
must be ordered, non-overlapping, and within their backing arrays. Newly authored
Trigger/Encounter polygons require at least three finite, non-adjacent-duplicate
points, nonzero XY area, no self-intersections, and coordinates that remain inside
the Area after transformation. Imported shorter polygons remain readable but do
not pass a new placement or reshape operation. Encounter spawn points must be
finite and within Area XY bounds. Out-of-range input rejects the complete batch
before Area membership changes.

The authored polygon path is capped at 1,024 points. This is more than twenty
times the largest observed Trigger polygon (49 points) and bounds the pairwise
intersection pass to 523,776 edge-pair candidates. Imported data still loads for
inspection, but a new placement or transform above the cap rejects explicitly.
The cost is a fixed compatibility limit in the client; if a real module contains
a larger polygon that must remain editable, that observed input should replace
the cap.

### Membership and undo

Expand `AreaObjectMembershipState` from three typed counts to the fixed nine Area
member arrays. One object-kind index table defines the stable count order. Typed
load, clone, insert, erase, and membership lookup remain explicit switches because
the destination vectors contain different pointer types.

Validation sorts membership rows by `(kind index, member index)`, rejects duplicate
handles and stale Area/object generations, verifies the complete nine-count
snapshot, reserves all destination vectors, and applies one homogeneous attach or
detach pass. Undo owns detached roots; redo revalidates before reattachment. Store
inventory and region components move with their root handle and are never flattened
into the membership protocol.

### Toolset visual rows

Viewer input is partitioned once by visual class:

```text
model rows:       Creature, Door, Item, Placeable, Waypoint
point markers:    Store, Sound
distance dome:    positional Sound
region outlines:  Trigger, Encounter
spawn markers:    Encounter
```

The Store/Sound marker and Sound dome are toolset-only debug geometry. They do not
enter object components, CAF, the gameplay renderer, or model/resource lookup.
Sound visual parameters come from one typed profile projection over a Sound batch;
C++ receives flat finite values and does not read `SoundState`. Invalid projected
radii drop the affected dome, retain the selectable point marker, and emit a
concrete diagnostic.

Extend debug-shape categories and selection ranges so every marker, dome, region,
and spawn point carries an exact `ObjectHandle`. Renderer records use indices and
flat vertex/index spans. The scene owns those spans until rebuild. During a gesture,
one small transient vertex/index buffer holds the ghost; commit/undo/redo rebuilds
the persistent Area scene once.

The existing Waypoint model remains authoritative when it resolves. Its toolset
point marker is the fallback/picking representation. Store uses that same marker
primitive without pretending to be a Waypoint or selecting an asset by name.

## Transform sequence

### 1. Arm and classify

Project-tree pointer down resolves the resource once through the fixed blueprint
type table. It captures the active tab, Area handle, mutation epoch, and one of
three gesture payloads:

```text
PointPlacementState   -> Waypoint, Store, Sound
DoorPlacementState    -> Door plus AreaDoorHookSnapshot
RegionPlacementState  -> Trigger, Encounter plus vertex/spawn buffers
```

This is a tagged flat state, not a virtual gesture hierarchy. Classification does
not recur on pointer motion. A tab, viewport, resource-generation, Area-identity,
or Area-mutation change cancels and destroys the detached preview root.

### 2. Point placement

Surface hit produces finite XYZ inside Area bounds. The detached object receives
that location; Sound orientation remains its compatible zero value. Waypoint uses
its existing model ghost. Store and Sound use transient marker/debug rows. Drop
commits the same handle through the common membership batch.

Whole-object movement uses the existing five-pixel drag threshold and exact
before/after transform edit. Invalid surface hits leave the ghost invalid and do
not mutate live state. Duplicate copies the complete root graph, offsets through
the same placement admission path, and receives a fresh UUID.

### 3. Door placement and snapping

Demand and parse all SET door slots before exposing Door placement. Build the Area
hook snapshot once for the gesture. Pointer motion derives the hit tile and visits
only that tile plus its eight neighbors through `tile_offsets`. It filters occupied
and incompatible slots, then chooses the smallest finite squared XY distance with
tile/slot order as the deterministic tie break.

Generic Doors and tileset-specific Doors use compatibility rules confirmed by the
phase-0 SET/CAF join. A nonzero tileset Door appearance must not be placed into an
unrelated hook type. The existing typed Door model resolver supplies the semantic
door selector; C++ does not inspect Door propsets or match a model/resource name.

The valid ghost copies the hook's exact world position and orientation with unit
scale. No compatible unoccupied hook produces an invalid ghost and a visible
diagnostic. New placement, move, duplicate, and redo all use this same admission
function. Imported off-hook Doors remain untouched until the author moves them.
Undo restores their exact previous transform even when it is off-hook.

Door commit invalidates Area navigation and rebuilds its obstacle/link data once.
Failed navigation reconstruction rolls the structural edit back through the same
transaction; it does not leave a visible Door without matching navigation state.

### 4. Region placement

The first accepted surface click becomes the root location and local point zero.
Subsequent accepted clicks append `world - root` points. A double click closes the
polygon when validation succeeds. Escape cancels and destroys the detached root.
The transient debug buffer displays the open outline and closing edge without
changing object components.

Trigger commit writes local geometry and the profile-initialized highlight height.
Encounter commit writes local geometry and the explicit spawn-point array. Zero
spawn points are accepted. Spawn-point add/move/delete is a separate bounded batch
edit over the selected Encounter's native geometry component; each command stores
complete before/after rows for exact undo.

Moving a Trigger changes only its root transform because its vertices are local.
Moving an Encounter applies the same world delta to its absolute spawn points in
the same transaction as the root transform. Duplication deep-copies geometry and
spawn arrays, assigns a fresh UUID, and moves the copy through the same admission
path. Delete/undo retains all component arrays with the detached root.

### 5. Publish once

Successful placement, transform, duplicate, delete, undo, and redo publish one
Area structure/mutation epoch, one dirty-state change, one active selection, and
one renderer/navigation rebuild. Save serializes the existing live Area once
through native CAF and regenerates the existing Area map path. No command writes
JSON patches or separately updates a project-side object mirror.

## Cost

These are algorithmic and implementation-scope estimates, not measured latency
claims.

- SET parsing is paid once per loaded tileset and retains one small flat row per
  declared door slot. Area hook construction is `O(area tiles + hooks + doors)`
  with `O(area tiles + hooks)` retained gesture memory.
- Door pointer admission reads at most the hook spans for nine neighboring tiles.
  Its access is contiguous with predictable validity/type branches after the
  visual class was selected once. The exact hook counts and pointer time must be
  recorded on the target machine before claiming the lookup is cheap.
- Point placement is constant-sized command work plus the existing cold object
  load and visual construction.
- Sound debug construction is linear in visible Sounds times a fixed dome segment
  count. The observed largest Area has 48 Sounds and the largest radius is 50 m.
  Segment count is selected by a captured visual test and recorded with vertex/
  index totals; it is not made user-configurable.
- Region validation is `O(V^2)` for self-intersection on the one interactively
  drawn polygon. The observed maxima are 49 Trigger and 34 Encounter vertices.
  Persistent debug construction is `O(total region vertices)`; the largest
  observed Area contains 217 combined vertices.
- Membership validation and application remain linear in edited rows, with nine
  fixed destination counts. Store inventory graphs are retained by root ownership
  and do not enter that loop.
- Scene and navigation rebuilds are cold commit-time costs. Pointer motion updates
  only the existing model transform or transient debug buffer. Measure maximum UI
  event-loop gap, hook snapshot time, pointer admission time, debug vertex/index
  counts, scene rebuild, navigation rebuild, save, and reload on the largest
  observed Areas before release.

No pointer-heavy hot path is introduced. Existing cold typed Area vectors retain
object pointers because `Area` and `ObjectManager` already own that storage; every
new query/render protocol flattens them into handles, indices, offsets, and copied
rows before repeated traversal.

## Build sequence

### Phase 0: prove source semantics

- Demand representative base-game and CEP SET resources and record the exact Door
  slot keys, count ranges, local coordinate convention, orientation units, and
  type meaning.
- Extend the read-only corpus audit to join computed hooks with existing Door
  transforms. Record unmatched, multiply matched, occupied, generic, and
  tileset-specific rows. Imported mismatches remain valid input.
- Observe one classic NWToolset Sound, Store, Trigger, and Encounter presentation
  or use an authoritative implementation/source. Resolve the Sound elevation and
  random-position questions and confirm the Store marker presentation.
- Add compact synthetic SET/CAF fixtures that encode only the observed shapes.
  Do not copy private module content into tests.

Gate: no Door placement code lands until one real SET hook transforms to the
matching real CAF Door location/orientation. Sound geometry does not interpret an
unconfirmed field.

### Phase 1: nine-kind membership foundation

- Expand typed member lookup/count/reserve/insert/erase and detached ownership to
  all nine Area vectors.
- Extend blueprint load and clone switches to UTD, UTE, UTS, UTM, UTT, and UTW.
- Add flat geometry/spawn payload ranges to the placement batch.
- Extend delete, duplicate, undo/redo, dirty state, selection reconciliation, and
  CAF round-trip tests with one mixed nine-kind batch.
- Retain exact Store inventories, Trigger geometry, Encounter geometry/spawns,
  UUID rules, and object order.

Gate: a mixed batch can attach, detach, undo, redo, save, and reload byte-equivalent
semantic content without rendering or pointer interaction.

### Phase 2: Waypoint and Store point placement

- Enable UTW and UTM project-tree Area drag classification.
- Reuse Waypoint model ghosts and add the shared toolset point marker for Store.
- Add Store/Waypoint selection records, spatial sync, move, duplicate, delete,
  undo/redo, and scene rebuild.
- Verify marker visibility and hit targets at representative camera distances.

Gate: both kinds complete the fresh blueprint -> drop -> move -> save -> reopen
workflow and preserve camera/selection behavior.

### Phase 3: Sound placement and dome

- Add the narrow batch profile projection for finite Sound toolset visual rows.
- Add Sound marker, dome/ring generation, debug category, picking, transient ghost,
  and persistent scene rows.
- Enable UTS drop, move, duplicate, delete, undo/redo, save, and reload.
- Cover positional, non-positional, random-position, invalid radius, and 48-Sound
  Area inputs. Invalid dome data drops only the dome and reports the reason.

Gate: the visual matches the confirmed NWToolset semantics and pointer selection
identifies the exact Sound rather than the containing Area surface.

### Phase 4: Door hooks and snapped placement

- Extend `Tile`/`TilesetRegistry` with validated door slots and add tile-orientation
  transform tests for all four rotations and height offsets.
- Build/invalidate `AreaDoorHookSnapshot` from live tiles and existing Doors.
- Add typed Door compatibility and deterministic nearest-hook admission.
- Extend area preview append/spatial sync for Door ghosts and preserve authored
  animation state at commit.
- Enable UTD drop, snapped move/duplicate, delete, undo/redo, navigation rebuild,
  save, and reload.

Gate: new and moved Doors occupy exact unoccupied compatible hooks, undo restores
exact prior data, and existing unsnapped Doors load unchanged.

### Phase 5: Trigger and Encounter regions

- Add polygon gesture state, transient outline, validation, close/cancel behavior,
  and instance component materialization.
- Enable UTT/UTE drop, debug-shape selection, whole-region movement, duplication,
  delete, undo/redo, save, and reload.
- Add Encounter spawn-point add/move/delete commands with exact undo and selection.
- Rebuild persistent debug-shape/selection rows once per committed edit.
- Verify concave polygons, boundary vertices, degenerate/self-intersecting rejection,
  imported short geometry, zero spawn points, and spawn translation on movement.

Gate: both region kinds survive GFF-compatible CAF round trips with the same world
footprint and Encounter spawn positions.

### Phase 6: integration and measurement

- Run the full client/object/render/navigation/resource regression suites and
  sanitizer checks for new native ownership paths.
- Record hook snapshot/query, transient debug update, scene/navigation rebuild,
  save/reload, retained rows/bytes, and maximum UI event-loop gap on the largest
  observed Areas.
- Perform a fresh-process desktop pass for each kind: create blueprint, drop,
  cancel, commit, select, move, duplicate, delete, undo/redo, Save All, reopen.
- Check Linux and Windows filesystem/build behavior; spatial protocols themselves
  contain no platform-specific paths.
- Update `tools/client/README.md`, the completed placement issue, and the release
  walkthrough with the demonstrated commit and remaining limits.

## Expected file map

- `lib/nw/formats/Tileset.hpp` and `lib/nw/kernel/TilesetRegistry.cpp`: retained
  SET door slots.
- `tools/client/area_door_hooks.{hpp,cpp}`, `area_navigation.{hpp,cpp}`, and
  `area_regions.{hpp,cpp}`: SET-hook snapshots, spatial admission and nav
  projection, and bounded polygon validation. These boundaries own no UI
  document or live object.
- `tools/client/object_edits.{hpp,cpp}`: nine-kind load, clone, membership,
  transform/geometry/spawn transactions, and undo.
- `tools/client/main.cpp`: gesture event routing and command dispatch only.
- `lib/nw/render/viewer/{preview_scene,scene_debug,area_render_scene,session}` and
  client renderer facades: model ghosts, markers, domes, persistent/transient
  debug rows, picking, and rebuild.
- Active profile Sound/toolset module: one narrow batch projection for visual
  values; no C++ propset access or new general reflection API.
- `tests/kernel_tilesets.cpp`, `tests/rollnw_client_object_edits.cpp`,
  `tests/rollnw_client_area_navigation.cpp`, and renderer/client interaction tests:
  contracts, ownership, geometry, input, rendering, and round trips.

## Simplification pass

1. **Do nothing:** rejected because six blueprint kinds cannot complete the Area
   authoring loop.
2. **Do once:** parse SET slots once per tileset, build hooks once per stable Area
   epoch, classify the gesture once, load the detached root once, and rebuild the
   persistent scene once per commit.
3. **Do fewer times:** point, Door, and region cases each have one straight-line
   gesture path. Membership and save remain one shared batch instead of six
   command implementations.
4. **Approximate:** Sound domes and point markers may use fixed-segment debug
   geometry because they are tool visualization. Door transforms, polygons,
   spawn points, ownership, and saved data are exact.
5. **Small lookup table:** one fixed nine-kind table maps resource type, object
   type, Area member kind, label, and spatial class. Typed vector operations stay
   explicit.
6. **Large lookup table:** `tile_offsets` is the only dense table; it directly
   indexes Area door hooks. No global spatial tree or persistent reverse index is
   added.
7. **Small buffer:** the existing transient debug buffer carries one uncommitted
   marker/dome/polygon. It decouples pointer updates from persistent scene rebuilds.
8. **Constrain further:** one Area, one gesture, six fixed NWN kinds, whole-object
   region movement, explicit Encounter spawn edits, and native CAF only. Runtime
   placement and general geometry editing remain separate.

This removes a universal gizmo hierarchy, per-frame profile calls, unrestricted
tileset-Door placement, model-name inference, a background worker, a physics
query, a second Area object mirror, and six separate membership/undo
implementations. Generic Doors remain freely placed because NWN represents them
without a SET-hook type.

## Done criteria

- Every one of the nine blueprint types can be dragged from the project tree into
  an Area through its explicit spatial-class interaction.
- All nine Area member arrays participate in typed attach/detach, duplicate,
  delete, undo/redo, dirty state, selection reconciliation, save, and reload.
- New/moved/redone tileset Doors resolve an unoccupied compatible SET hook and
  store its exact world transform. Generic Doors remain freely placed. No-hook,
  malformed-hook, incompatible-type, occupied, stale-Area, and changed-tile
  cases reject without mutation.
- Existing imported Doors that do not join a hook remain loadable, selectable,
  saveable, and exactly restorable by undo.
- Waypoint and Store point markers remain legible and selectable without changing
  serialized object data. Store nested inventory survives every structural edit.
- Positional Sounds show the confirmed debug dome; non-positional and malformed
  rows follow the documented marker/drop-dome behavior. Visual geometry never
  enters CAF or gameplay rendering.
- New Trigger/Encounter polygons validate before commit and serialize as root plus
  local points. Concave valid polygons work; duplicate, zero-area,
  self-intersecting, non-finite, and out-of-bounds inputs reject.
- Encounter spawn points can be added, moved, deleted, translated with the whole
  Encounter, duplicated, undone/redone, and reloaded. Zero points remains valid.
- Cancel and failure destroy every detached preview root and leave Area arrays,
  dirty state, undo, renderer, navigation, selection, and files unchanged.
- Successful batches publish one mutation/structure epoch and rebuild renderer,
  navigation, and derived Area map at their existing boundaries only.
- Tests cover count/order/UUID/component ownership, mixed-type batches, stale
  handles, allocation/validation failures, renderer hit targets, gesture input,
  exact undo, and CAF/GFF-compatible round trips.
- Measured results report the target machine, corpus Area, row/byte counts, and
  UI event-loop gaps. No performance conclusion is made from algorithm shape.
- `clang-format`, `git diff --check`, relevant builds/tests, sanitizer ownership
  checks, and the no-new-`std::map` scan pass.

Evidence against the plan includes SET slot data that cannot reproduce real Door
placements, a Door compatibility relationship absent from retained resources,
Sound visualization semantics that require data outside the object/profile,
region geometry that cannot round-trip through the native component contract, or
measured pointer/commit stalls beyond the interactive budget. Record the concrete
input and revise only the affected spatial class rather than weakening every
placement path.

## Open questions retained in this issue

1. Confirm exact SET Door slot keys, coordinate basis, orientation units, and type
   relationship using demanded base-game and CEP resources.
2. Confirm Sound `elevation`, `random_x`, and `random_y` presentation in the
   classic toolset. The marker remains useful if only the outer dome is confirmed.
3. Confirm whether the Store marker should share Waypoint color as well as shape.
   This changes only debug presentation, so it is cheap to revise.
4. Check real modules for Trigger or Encounter polygons above the 1,024-point
   authoring cap. None occurred in the inspected 604-Area corpus.

## Plan self-check

The common case is one point-object placement and uses one resource classification,
one detached root, one surface query, one membership row, and one scene rebuild.
Door and region cases are partitioned before their pointer loops. Inputs, outputs,
owners, lifetimes, ranges, rejection/drop behavior, costs, stable/volatile data,
and touched systems are explicit. All transforms have plural batch contracts;
the active gesture is a documented singleton. Repeated traversal uses indices and
offsets. Existing typed Area pointers remain only in cold ownership vectors. No
speculative option, persistent index, background process, model-name rule, or
unmeasured performance claim is included. The remaining presentation and
compatibility questions above are isolated to their affected data paths.

## Implementation and verification record

All nine native Area member arrays now share the structural place, duplicate,
delete, undo/redo, and selection-reconciliation path. The renderer uses
`ObjectType` directly rather than maintaining a parallel render-kind enum. Door
slots are retained from SET data and transformed into one indexed Area snapshot;
generic Doors remain free while tileset Doors claim an exact compatible hook.
Sound state crosses the profile boundary through one typed SmallS projection and
replacement operation. Store, Sound, and Waypoint tool markers, Sound domes,
Trigger/Encounter outlines, and Encounter spawn markers remain renderer-only
data.

New Trigger and Encounter points project from viewport rays onto the cached
walkable navigation surface. The open path and completed polygon reject
non-finite or out-of-bounds points, adjacent duplicates, self-intersections,
zero-area closures, and inputs above 1,024 points. Valid concave polygons remain
unchanged. No automatic polygon rewrite was added: there is no observed tolerance
for discarding intentional vertices, so invalid shapes fail with a diagnostic
instead of silently serializing different geometry. The validation cost is a
pairwise edge pass bounded by 523,776 candidates at the authoring cap and paid on
the UI thread; observed polygons have median four vertices and maximum 49 in the
inspected corpus. Interactive latency at the cap is unverified.

Update References preserves the placed polygon and Encounter spawn rows in both
the live native replacement path and the closed-document JSON patch path. These
instance-owned rows are not reclassified as newly authored geometry, which keeps
legacy imported polygons with fewer than three points updateable. Blueprint-owned
region values still come from the replacement blueprint.

Verification on the local Linux desktop build:

- `rollnw_test`, `rollnw-client`, and `rollnw_benchmark` compile.
- A complete 2,012-test run found one integration failure in blueprint-reference
  replacement; 1,976 tests passed and 35 graphics-dependent tests skipped. The
  failure was fixed, and its regression test passes with nonempty Trigger and
  Encounter geometry plus Encounter spawn points.
- The post-fix affected-suite run executed 200 tests: 170 passed and 30 Vulkan
  tests skipped because the graphics context is unavailable.
- `git diff --check` passes. The changed lines add no `std::map`, no replacement
  render-kind enum, and no redundant SmallS object cast before `O.invalid`.

The manual viewport gestures, visual presentation on a Vulkan-capable desktop,
and sanitizer ownership checks were not run here. No interactive performance
conclusion is claimed; measuring pointer-event time for an observed four-point
polygon and the 1,024-point limit would establish that result.
