# rollnw | client Area Object Placement

Status: initial Creature and Placeable slice implemented; interactive viewport
and Tracy verification remain.

## Problem

rollnw client can select, transform, duplicate, and delete live Creature and
Placeable instances in an area, but the project tree cannot place a blueprint
into that area. Opening the blueprint in another tab is not an authoring
operation. The required interaction is a placement gesture: drag a blueprint,
see a translucent live preview, move it, cancel it, or commit it as one undoable
area membership change.

## Real Platform and Data

The platform is one UI thread, one live module resource manager, one object
manager, and one viewer session. Project-tree blueprint leaves resolve to
`Resource { Resref, ResourceType }`; the module resource manager owns the bytes.
The supported first-slice resources are UTC and UTP. The output is a live,
instantiated Creature or Placeable with one spatial component and, after commit,
one pointer in the corresponding live Area vector.

The common batch has one resource because the current pointer gesture carries
one project row. The APIs remain plural so multi-row sources do not require a
second load, validation, or membership path. Unsupported resource types, absent
module resources, stale area handles, duplicate handles, non-finite transforms,
out-of-bounds positions, and objects already attached to an area reject the
complete batch.

Area render scenes cache upward-facing triangles from the same visible tile
meshes used by the viewport. The cache is partitioned by tile model bounds and
the placement gesture consumes the nearest ray hit as a full XYZ position.
It does not parse NWN WOK data or constrain the authoring surface to NWN's
two-dimensional walkmesh rules.

## Decision

Use one state machine:

```text
idle -> armed -> ghost_valid | ghost_invalid -> committed | cancelled
```

Crossing the drag threshold does not load anything until the pointer reaches the
active area viewport at a valid position. Loading resolves the project resource
through the mounted module, instantiates a detached live object, sets its area
and initial spatial state, and appends its visual rows to the current preview
scene once. Only those appended rows are changed to the transparent pass and
excluded from shadows and authored lights.

Pointer motion traces the stable tile surface cache and writes one preview
`ObjectSpatialState` row. It does not rebuild assets or deserialize the
blueprint. Drop writes the final spatial component and transfers the same handle
into the existing area membership transaction.
Undo detaches it and owns it; redo reattaches it. Escape, right click, invalid
drop, tab change, and shutdown destroy an uncommitted object and rebuild the
live area without it.

## Batch Contracts

### Blueprint Load

- Input: contiguous `AreaObjectBlueprintPlacement` rows containing a valid UTC
  or UTP resource and a finite spatial state inside one live area.
- Output: contiguous generational handles for detached instantiated objects in
  input order.
- Ownership: `ObjectManager` owns storage; the caller owns each detached handle
  until commit transfers it to the membership undo action.
- Errors: reject the full batch and destroy any successfully loaded prefix.

### Scene Preview Append

- Input: one live area scene, a contiguous span of detached Creature/Placeable
  handles, and opacity in `(0, 1)`.
- Output: appended render records tagged with the exact object handles.
- Ownership: the preview scene owns render rows; the object manager continues to
  own gameplay objects.
- Errors: validate and build the complete source batch before appending; invalid
  input leaves the destination scene unchanged.

### Membership Commit

- Input: one live area and a unique batch of detached handles whose spatial area
  matches it.
- Output: pointers appended to the type-partitioned Area vectors plus one undo
  action owning the membership state.
- Errors: stale counts, stale handles, wrong area, existing membership, and
  allocation failure reject before the first insertion.

### Area Surface Trace

- Input: contiguous finite viewport rays, contiguous tile surface ranges, and
  contiguous prevalidated upward-facing world-space triangles.
- Output: one hit row per ray containing full XYZ position, normal, distance,
  range index, and explicit hit/miss/invalid status.
- Ownership: `AreaRenderScene` owns ranges and triangles for the scene lifetime;
  trace results are caller-owned values and allocate no memory.
- Errors: mismatched output count, non-finite or zero-length rays, malformed
  ranges, and out-of-range triangle spans reject explicitly. Non-finite,
  degenerate, downward-facing, and out-of-range source triangles are dropped
  once while the cache is built.

## Cost

Blueprint deserialization and visual asset resolution occur once when the ghost
is first materialized. Appending the ghost rebuilds the flat area render cache
once. That rebuild extracts visible tile triangles into contiguous ranges;
pointer motion tests range bounds and scans triangles only for intersected
ranges before calling the existing spatial batch. Commit or cancel performs one
normal live-area rebuild to remove the transient visual rows.

The 16x16 `test_area` fixture produces 256 ranges and 144,155 triangles using
5,197,772 bytes. This is a measured memory cost, not a latency claim. The cache
stores 36 bytes per retained triangle plus one bounds/range row per tile.

The persistent gesture state is one resource, two object handles, one spatial
row, one pointer position, and flags. The committed state reuses the existing
membership rows and undo ownership. No editor scene graph, object mirror,
placement database, or per-frame blueprint parsing is added.

Interactive latency is unverified. The verification measurement is pointer
motion CPU time and begin/cancel/commit frame time in Tracy on a representative
small area and the largest routinely edited module area. A failure is visible
drag latency or a placement frame that materially interrupts viewport input.

## Simplification Pass

- Reuse the live object, scene append, spatial batch, and membership transaction;
  do not create an editor-only object model.
- Load only after the drag threshold and first valid viewport position.
- Support only Creature and Placeable because they already have structural edit
  and spatial preview contracts.
- Keep one active ghost; do not add multi-place, snapping, palettes, or rotation
  controls to this slice.
- Reuse visible tile meshes and per-tile bounds; do not parse WOK, add an editor
  scene graph, or build navigation data as part of object placement.

## Done Criteria

- Dragging a UTC or UTP project row into an active area creates the exact live
  detached object and shows it translucent at the nearest rendered surface hit.
- Sloped and vertically stacked surfaces preserve the returned Z coordinate;
  misses leave the ghost invalid rather than falling back to a scalar plane.
- Pointer motion performs no blueprint reload or full scene rebuild.
- Escape, right click, invalid drop, tab change, and shutdown leave no live
  detached object and no ghost render rows.
- A valid drop attaches that exact handle, selects it, marks the area dirty, and
  records one undo action; undo and redo preserve ownership and membership.
- Saving and reopening the area preserves the committed instance data.
- Unit tests cover batch cleanup, unsupported resources, detached membership,
  wrong-area rejection, and undo/redo.

Evidence against the design is measured gesture latency failing interactive use
or visible floors missing from the render-mesh cache in normal authoring. The
first response is to profile range and triangle traversal, then add a spatial
index or change the source geometry based on that data, not to add a second
object representation.
