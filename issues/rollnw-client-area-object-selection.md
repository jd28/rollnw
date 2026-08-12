# rollnw | client Area Object Selection

Status: resolved.

## Problem

The area viewer needs pointer selection for both live objects and tiles.
Bounds-only picking selected empty space inside large bounds. A combined
nearest-hit query also made normal object selection depend on whether tile rows
were encountered first. Triggers and encounters were not selectable even though
they are visible authoring data.

The required output is one flat selection result used by `ViewerSession`:

```text
AreaObjectSelection {
    record_index: uint32
    object: ObjectHandle
    position: vec3
    distance: float
    tile_x: int16
    tile_y: int16
    kind: AreaRenderRecordKind
    source: none | area_record | debug_shape
    status: hit | miss | invalid_input
}
```

A tile hit has an invalid object handle and identifies its tile coordinates. A
creature, door, item, placeable, trigger, or encounter hit carries the live
generational handle consumed by the property workbench.

## Real Platform and Data

The platform is one rollnw client UI thread and a CPU click query over data
already prepared for the nw::gfx renderer. Selection is cold; rendering remains
the per-frame hot path.

The first input shape is the existing structure of arrays in `AreaRenderScene`:
record bounds, flags, kinds, tile coordinates, object handles, model kinds,
model indices, instance handles, root transforms, and model references. Tile and
mesh-backed object records both refer to indexed render geometry. One object can
produce several records.

The second input shape is trigger and encounter debug geometry. The render path
already stores contiguous debug vertices, indices, and category ranges. A cold
selection sidecar adds contiguous world-space polygon points and one range per
live trigger or encounter. Polygon encounters and triggers use their authored
footprint. Encounter spawn markers are render-only and occupy a separate debug
range, so they cannot enlarge or displace the encounter's selection bounds.

Observed area-record shapes used by the benchmark are 5 records/1 mesh-backed
object (`pl_farroes_shop`), 256/80 (`pl_farroes_city`), and 573/61
(`pvp_area_3`). The real graphics integration fixture contains 256 tiles, four
mesh-backed object records, two triggers, and one encounter.

The 604 Awakening area JSON files contain 3,512 encounters. Of the 3,511 with at
least three footprint points, 2,204 have a Z span greater than 0.001 m and the
largest span is 15.455858 m. Footprints most commonly contain four points
(2,062 encounters), followed by five (561) and three (350). No encounter in the
sample has spawn points without footprint geometry. Treating only perfectly
planar footprints as polygons therefore rejects the common real input.

The same areas contain 14,467 placed objects across the nine stored `Area`
member arrays. The median area has 15 placed objects, the 95th percentile has
74, and the maximum has 206. This bounds a direct, non-virtualized right-panel
list to hundreds of rows, not an unbounded data set.

Valid coordinates and indices must fit their stored integer widths. Non-finite
rays, bounds, points, or transforms are rejected. Mismatched area-record columns
make the whole batch `invalid_input`. Disabled, stale, malformed, or hidden
debug-category rows are dropped from the candidate scan. No geometric hit is a
`miss` and clears the active selection.

## Decision and Transform

Carry one explicit selection target from the client mouse event into the batch
query. A normal click targets objects; Ctrl-click targets tiles:

```text
viewport click + Ctrl state
    -> object target | tile target
    -> finite normalized world ray
    -> scan enabled records matching the target
       -> AABB rejection
       -> actual indexed model triangles
    -> for object targets, scan enabled trigger and encounter selection ranges
       -> AABB rejection
       -> representative footprint plane + point-in-polygon
    -> nearest AreaObjectSelection
    -> ViewerSession active selection
```

Object and tile rows never compete in one reduction. Object targets scan
mesh-backed objects and visible trigger/encounter shapes while ignoring tiles.
Tile targets scan only valid tile records and skip the debug sidecar. Lower
indices win exact-distance ties within the selected target.

Observed `ttf02.set` height fields describe terrain topology or rendering, not a
selection volume: `[GENERAL] Transition` and `HasHeightTransition`, per-tile
corner heights, and `[GRASS] Height`. The renderer uses `Transition` as the
elevation step in `tile.height * transition`. Tile queries therefore use actual
indexed tile geometry. The selection result retains the exact world-space hit.
The viewport draws the selected 10×10 m tile cell as a shallow box rooted at
the record's authored elevation, so its placement is stable across clicks and
does not derive its height from the clicked surface or model AABB.

The persistent trigger/encounter mapping stays out of the hot `DebugShapeRange`.
`PreviewScene::debug_shape_selection_ranges` and
`debug_shape_selection_points` are read only on clicks, selection-outline
rendering, and selection restoration after a live-area rebuild.

Nonplanar footprints remain one authored XY polygon. Their selection plane is
the arithmetic mean of the finite authored Z values, while their rejection
bounds retain the authored minimum and maximum Z. This makes the whole footprint
a click target without inventing a vertical prism or including spawn markers.
Fewer than three footprint points are rejected as a viewport polygon.

`ViewerSession` keeps the complete flat selection. Mesh-backed objects publish
their existing live handle. Trigger and encounter hits publish their sidecar
handle. Tile hits clear the property-tree object while retaining tile bounds for
the viewport outline. Programmatic selection and live-area rebuilds accept and
restore both mesh-backed and debug-shape handles.

When no area object is selected, the existing right workbench shows one flat row
per valid object in the area's stored creature, door, encounter, item,
placeable, sound, store, trigger, and waypoint arrays. A row submits its copied
generational handle to the same programmatic selection path used by viewport
selection, so it opens the same object workbench. A back button clears that
selection and restores the list. Placed kinds without render or debug geometry
are accepted only when their spatial component names the active area; no fake
viewport bound is manufactured for them.

## Batch Transform Contract

### Input

- A contiguous span of finite rays. A pointer click is a count-one wrapper over
  this batch path.
- One validated `AreaRenderScene`; all parallel record columns have equal length
  and at most `uint32_t` rows.
- One borrowed `PreviewScene` owning indexed model geometry and the cold debug
  selection sidecar.
- One target value: `object` or `tile`. Other values are not representable.
- Category options indicating whether triggers and encounters are visible.

### Output

- One `AreaObjectSelection` per input ray.
- `hit` carries a finite world-space position, a non-negative distance, and an
  index into the source named by `source`.
- `area_record` tile hits carry valid non-negative tile coordinates and an
  invalid object handle.
- Mesh-backed `area_record` and `debug_shape` hits carry a currently valid live
  object handle.
- `miss` carries no source; malformed batch input produces `invalid_input`.

### Ownership and Lifetime

- `PreviewScene` owns the live root area and its selection data.
- `ObjectManager` owns live objects; copied handles are non-owning generational
  values.
- Model buffers and all spans are borrowed only for the query.
- Selection indices are stable until the scene is rebuilt. Rebuild resolves a
  selected handle against the replacement scene before retaining it.

### Access and Branch Behavior

- Area record columns are scanned linearly. Record eligibility and AABB rejection
  are predictable for most rows; model buffer reads occur only for AABB
  candidates.
- Debug selection ranges are scanned linearly only for object targets. Their
  point spans or rendered index spans are contiguous.
- The common query allocates only the legacy-model prepared-draw scratch already
  required by that model path. It performs no per-frame work.

## Cost

The dominant click cost is linear in area records plus debug selection ranges,
with triangle work only for bounds candidates. Polygon footprint work is linear
in the selected range's point count. This is CPU latency on the client UI thread.

Persistent area-record handle storage is eight bytes per record: 40 bytes,
2,048 bytes, and 4,584 bytes for the three measured shapes. Debug selection adds
`sizeof(DebugShapeSelectionRange)` per trigger/encounter plus 12 bytes per
authored footprint point. No full object state or duplicate mesh is stored.

Opening or refreshing the placed-object list walks the nine contiguous area
member arrays once and allocates one handle and one display-name string per valid
row. It performs no per-frame renderer work. This cold UI transform has not been
benchmarked; the observed maximum is 206 rows, so measurement rather than list
virtualization or indexing is the next step if interaction latency is observed.

Release benchmark medians on the local development CPU, three repetitions with
a 0.02-second minimum per case, are:

| Shape | Hit | Miss |
| --- | ---: | ---: |
| 5 records / 1 object | 1.749 us | 0.037 us |
| 256 records / 80 objects | 3.156 us | 1.695 us |
| 573 records / 61 objects | 3.157 us | 1.249 us |

These object-target measurements scan the complete area-record rows but only
read indexed geometry for eligible objects. They do not measure tile-target
queries or trigger/encounter polygons. A representative-area benchmark including
observed footprint point distributions is the measurement needed before making
a performance claim about that sidecar.

The largest measured area-record median remains below the existing 0.25 ms click
target, so a BVH, GPU ID buffer, or other persistent acceleration structure is
not justified by current data.

## Simplification Pass

- Reused render-model and legacy-model indexed geometry; no parallel collision
  mesh or object proxy was added.
- Reused authored footprint points for the common trigger/encounter case; no
  debug-outline triangulation or name-based policy was added.
- Split encounter footprint and spawn rendering once during scene construction;
  spawn points add no selection state or click work.
- Kept trigger/encounter selection in a cold sidecar instead of adding fields to
  every per-frame debug range.
- Reused the live area's nine stored object arrays, existing handles, and the
  existing object workbench; no second scene tree, search state, or virtualized
  list was added for the observed row counts.
- Removed cross-category ranking. The click modifier selects one target before
  the scan, so each query needs only one nearest-hit result.
- Did not add spatial acceleration because the measured area-record scan meets
  the stated latency target.

## Done Criteria and Verification

- Actual indexed geometry rejects bounds-only hits.
- Normal click selects mesh-backed objects while ignoring tile records.
- Ctrl-click selects tiles while ignoring object and debug-shape records.
- Tile selection draws one shallow cell-sized box rooted at the selected
  record's authored elevation; it does not depend on the clicked surface or
  model bound.
- A visible trigger or encounter footprint is an object target and publishes
  its live handle.
- Encounter selection bounds contain only footprint points. Nonplanar
  footprints are selectable throughout their XY area; spawn markers are not
  selection targets.
- Hiding a trigger/encounter category removes it from selection.
- Programmatic trigger/encounter selection works and survives a live-area
  rebuild.
- With no selected area object, the right workbench lists every valid placed
  object. Selecting a row opens the same workbench as a viewport click, and its
  back button clears selection and restores the list.
- Placed objects without viewport geometry remain inspectable from the list only
  when they belong to the active area, and that selection survives a live-area
  rebuild.
- Empty space clears selection; invalid and stale inputs are rejected.
- The client, test, and benchmark targets build in Release.

Focused verification passes eleven tests across `RenderViewerAreaSelection`,
`ClientObjectEdits`, and
the real-area `RenderViewerPreparedDraws` integration case. The integration case
loads 256 tiles, mesh-backed objects, two triggers, and one encounter; it checks
tile viewport selection, live debug-object publication, and selection retention
through a scene rebuild.

Evidence against this design would be a reproducible geometric mismatch on real
area data or a measured click latency above 0.25 ms. The next step would be to
fix the specific input transform or index these same flat rows, not add a second
scene/object model.

## Not In This Slice

- Editing or saving `ARE`/`GIT`/`GIC` resources.
- Moving, rotating, adding, or deleting area objects.
- Multi-selection or marquee selection.
- Search, filtering, grouping, or list virtualization for placed objects.
- GPU picking or a persistent spatial acceleration structure.
