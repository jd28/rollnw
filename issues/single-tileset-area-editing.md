# Single-tileset area painting

## Tier, frame, and limit

This is a Tier 2 client subsystem change. The editor must expose the familiar
NWN operation: choose an action from the tileset palette and paint the area.
Tile IDs, rotations, corner terrain, edge crossers, and transition tile choice
are implementation data, not author-facing controls.

The useful boundary is an existing, fixed-size, single-SET area. This work does
not add resizing, multiple tilesets, native terrain, or a general constraint
solver. If a SET has no tile matching the requested local topology, the entire
gesture is rejected and the existing area remains unchanged.

Plan B applies if measured brush construction is too slow at 512 cells: build a
sorted signature table once per loaded SET and binary-search it. That index is
not added without the measurement.

## Actual input and output data

The inputs are:

- the area's contiguous row-major `AreaTile` rows (`id`, `orientation`, base
  `height`, animation bytes, and light bytes);
- one resolved SET containing 12,304 tile rows across the 34 base resources,
  including four corner terrain names and relative heights plus four crosser
  edges per tile;
- the corresponding `*palstd.itp` tree, whose category 2 leaves resolve to SET
  terrain/crosser actions or the special eraser/raise-lower actions, and whose
  other leaves resolve through a group's first tile model;
- one ordered sequence of unique area-cell indices for terrain, crosser, and
  group gestures, or unique row-major corner-lattice indices for Raise / Lower,
  plus one explicit random seed and group orientation in `[0, 3]`;
- one ordered sequence of unique placed-cell indices for deterministic tile
  variation cycling.

The output is one sorted `AreaTileEditBatch` containing complete before/after
rows. It is applied once through the existing structural mutation, undo, scene
rebuild, navigation invalidation, door-hook validation, dirty-state, and save
boundaries.

Observed values and distributions:

- The Awakening contains 604 CAF areas and 48,471 tile rows. The common area is
  8x8; the observed maximum is 32x16, or 512 rows.
- Tile orientations are `[0, 3]`. Observed base heights are `[0, 16]`, with
  45,340 rows at zero.
- The base SETs contain 130 terrain types, 111 crosser types, and 1,876 groups.
  Of those groups, 1,062 are one cell; the largest observed group is 72 cells.
- All valid base-SET relative corner heights are zero or one. Runtime storage
  remains signed 32-bit and is not narrowed to that observed range.
- Positive `AreaTile.orientation` rotates counterclockwise. This mapping
  produced four corner mismatches among 97,638 observed shared corner pairs;
  the opposite mapping produced 26,152 mismatches.
- A default-flat raise has a fitting result in 11 of 12 base SETs that advertise
  height transitions. `tti01` has no matching result for the measured test
  shape and must reject it.

SET and area resources are stable between resource-generation or mutation-epoch
changes. Palette filtering and stroke cells are volatile UI data. Painting
reads no object propsets and changes no placed-object rows.

## Platform, cost, and access

The platform is the desktop client. UI input and live-area mutation run on the
main thread. Areas and parsed SET rows are contiguous, and the renderer borrows
the registry-owned SET for the loaded resource generation.

SET parsing is one linear pass over named types, tile topology, and flat group
cells at resource load. A brush decodes `T` area rows into four contiguous
lattice arrays, modifies the requested corners or edges once, and scans at most
`4*S` oriented SET tile candidates for each affected cell. Its cost is
`O(T + A*S)` time and `O(T)` temporary memory, where `A` is affected cells and
`S` is SET tile definitions. The common single-cell terrain operation has at
most nine affected cells. One height corner has at most four incident cells.
The worst observed full-area gesture has 512 rows. This cost is measured below.

Measured in the Release benchmark on the local 24-thread x86 desktop, brush
construction took 8.90 us for one cell in a 64-cell area, 88.8 us for all 64
cells, 12.4 us for one cell in a 512-cell area, and 0.707 ms for all 512 cells.
The height-corner transform took 2.90 us for one corner in a 64-cell area,
170 us for all 81 corners, 6.33 us for one corner in a 512-cell area, and
1.35 ms for all 561 corners.
CPU scaling was enabled, so these are a noisy local baseline. The measured
512-cell result does not activate the sorted-signature-table plan B.

On this 64-bit ABI, the cold authoring metadata adds 85 fixed bytes per SET
tile: a 32-byte image-map string object, a 52-byte topology row, and one group
membership byte. That is 1,045,840 bytes across the 12,304 base tiles, plus
30,016 bytes for group rows, flat group IDs, catalog string objects, and their
owned text. The client pays this storage once per loaded SET generation.

All hot iteration is linear over indices. Registry ownership already makes the
existing `Tileset*` stable for the resource generation; that one borrowed
pointer is retained at the cold boundary. No pointer-heavy painter data is
introduced.

Hover preview consumes the computed flat after-row batch. Mouse motion retains
only the latest point and processes it once per rendered frame. The first batch
hides its original tile rows and appends exact dynamic model rows under a
viewport-owned lease. A batch with the same ordered models repositions those
instances and changes the hidden originals without rebuilding cached tile
surfaces; a different model layout performs one area render-record rebuild.
The retained path costs `O(R + P)` CPU work for `R` area records and `P` preview
rows, with no mesh-buffer remap. This latency is unverified; measure
cell-to-cell preview updates in the observed 512-tile area before adding any
further cache.

## Data contracts and transforms

### SET authoring data

`Tileset::tile_topologies` is parallel to `Tileset::tiles`. Each row has four
terrain catalog indices in TL/TR/BL/BR order, four signed relative heights, four
crosser indices in T/R/B/L order (`-1` means none), and a validity bit. Groups
store dimensions and an offset/count into one flat signed tile-ID array; `-1`
is a random-fit cell. A byte array marks tiles referenced by groups so normal
terrain fitting does not select feature geometry.

Negative or excessive counts are rejected. An unknown corner terrain, unknown
nonempty crosser, or missing topology field invalidates that tile for smart
painting while exact rendering remains available. A malformed group is dropped
whole. Group tile IDs accept `-1` or an in-range tile ID. Every allocation and
dimension multiplication is checked.

### Palette transform

Load `<tileset>palstd.itp` from the active resource stack. Preserve leaf order.
Resolve names from TLK, then inline name, then resref. Category 2 leaves become
terrain, crosser, eraser, or Raise / Lower Terrain actions by matching
actual SET catalogs. Other leaves map to a group only when their resref matches
the model of that group's first tile. Unknown leaves are omitted.

The UI shows the action name, with a feature thumbnail when the group’s first
tile has `ImageMap2D`. It exposes no tile ID, manual orientation, or isolated
row-height commands for fitted terrain. `R` rotates a selected feature group in
quarter turns. Shift + left-click resolves an already placed tile into either
one cell or the complete rotated rectangular footprint of its verified SET
group. The selection outline covers every resolved cell and the palette shows
the source tile data. Shift + right-click selects the tile under the pointer and cycles
a non-group tile through valid fitted variations in SET order; right-drag
retains camera orbit. Placed groups reject variation cycling because the SET has
no interchangeable-group mapping.
Raise / Lower appears as one selectable action only when `HasHeightTransition`
is true; left paint raises and right paint lowers when no tile is selected.

The palette preserves the ITP tree as flat indexed rows plus contiguous child
index ranges. The root shows its immediate folders, selecting a folder shows its
immediate children, and Back moves to the parent; the area’s top-level tabs leave
the tile editor. Search scans usable action descendants of the current folder. The
source ITP pointer tree is cold parser data and is transformed once; interaction
and rendering retain no source pointers.

Each explicit ITP feature and group leaf remains an action. The palette does not
expose the SET's individual topology rows: each named terrain or crosser is one
action, and its compatible tile variation is selected by the fitter. For
`ttr01`, this is 76 usable ITP actions over 1,605 SET tile rows. The ITP has no
variation-family field, so separately authored leaves cannot be merged safely
from their labels or topology alone.

### Shared topology decode

Decode current rows into:

- `(width + 1) * (height + 1)` corner terrain indices;
- the same number of signed 64-bit absolute corner heights;
- `width * (height + 1)` horizontal crosser edges;
- `(width + 1) * height` vertical crosser edges.

Every shared corner and edge must agree after orientation. Invalid IDs,
orientations, topology rows, dimensions, overflow, or seam disagreement reject
the gesture with zero writes.

### Brush transforms

- Terrain sets the four shared corners of every visited cell to the selected
  terrain and clears its four crosser edges. Each lattice entry changes once.
- Eraser replaces cells with the SET's default terrain and chooses the first
  fitting non-group tile deterministically. When a target belongs to a verified
  placed group, the target expands to the group's complete rotated rectangular
  footprint, including random-fit cells. Hover outlines that footprint without
  replacing models. Incomplete or ambiguously matched groups reject with zero writes.
- Raise / Lower snaps the pointer to the nearest height-lattice corner and adds
  or subtracts one from each unique visited corner. It preserves crosser edges
  and refits only the at-most-four incident cells. The fitter supplies SET
  transition tiles. If the topology would replace or vertically move one part
  of a placed group, the complete gesture is rejected with zero writes.
- A crosser stroke sets the shared edges traversed by consecutive cells. A
  single-cell crosser has no direction and is explicitly rejected.
- A group click rotates the group’s complete row-major cell grid by the selected
  quarter turn and writes its fixed cells at the anchor’s base height. `-1`
  cells use normal fitting. A group that is out of bounds or internally
  inconsistent is rejected whole. Overlapping placement replaces complete
  intersected groups and refits their uncovered cells to the default terrain.
- A selection click returns one ordinary cell or every row-major cell in a
  verified placed group footprint. Incomplete or ambiguous groups reject with
  an empty selection; selection never owns pointers into the area or SET.

For every affected nonfixed cell, test the current row first when the cell was
only incidentally affected. Otherwise scan ungrouped SET tiles in four
orientations. A match has identical world-order terrains and crossers and one
signed base height that satisfies all four absolute corner heights. One pass
counts matches and the gesture seed selects an ordinal. A second pass selects
the row without a candidate allocation. If alternatives exist for a directly
painted cell, omit its current variant so repainting can visibly choose another.
An affected existing group-only tile must remain the exact same ID, orientation,
and base height or the whole gesture is rejected, except for cells in complete
group footprints expanded by erasing or overlapping placement. Placed-tile variation cycling separately scans
fitting non-group rows in SET order and never changes shared topology.

The result rows are sorted by cell index because affected cells are scanned in
row order. Complete rows preserve animation and light bytes. Apply, undo, and
redo all use the existing plural transaction path.

## Simplification pass

- Removed the exact tile-definition list, manual orientation for fitted terrain,
  and isolated row raise/lower from the normal UI. Only authored feature groups
  retain explicit quarter-turn rotation.
- Parse SET topology once instead of reparsing strings during every gesture.
- Use one shared lattice so a seam is changed once instead of coordinating
  independent neighboring rows.
- Target height corners directly, removing the cell-height brush and its
  unnecessary 3x3 refit footprint.
- Retile only incident cells and retain matching current rows, avoiding a full
  area rewrite.
- Coalesce hover points to one update per frame and retain equal-layout preview
  rows, avoiding event-rate area reconstruction while moving a feature.
- Use two linear candidate scans and no candidate buffer; no hash index,
  backtracking, worker, general constraint framework, or `std::map` is needed
  at observed volumes.
- Keep fixed-size single-SET editing as the constraint that removes cross-SET
  seam and resize states.

## Done and falsification criteria

Done requires:

- the real `ttr01` ITP produces terrain, crosser, feature, eraser, and one
  Raise / Lower action with stable keys and usable labels;
- terrain painting selects only fitting non-group candidates, keeps every seam
  valid, and is deterministic for a supplied seed;
- Raise / Lower modifies shared height topology and creates valid transitions;
- placed-tile variation cycling visits valid fitting choices and wraps in SET
  order;
- Shift + left-click selects ordinary tiles and complete rotated groups as one
  persistent highlighted selection;
- Shift + right-click selects the pointed tile and cycles an ordinary tile's
  compatible SET variation;
- Raise / Lower cannot split placed group-only tiles, and Eraser removes a
  verified placed group as one atomic batch;
- crosser strokes connect ordered cells and group clicks place all fixed cells;
- malformed seams, unavailable topology, no-fit requests, invalid groups, and
  out-of-bounds groups write zero rows;
- commit, undo, redo, save/reload, scene rebuild, navigation invalidation, and
  occupied door-hook rejection preserve their existing contracts;
- the normal full suite and focused ASan/UBSan tile and SET tests pass; brush
  construction is benchmarked for the observed 64- and 512-cell areas.

Evidence against the design is a valid base-game gesture that requires
backtracking beyond incident cells, measured interactive latency at 512 cells,
or a valid ITP leaf that cannot be resolved from its SET data. The first case
requires observing and encoding that missing rule; the second activates the
sorted signature-table plan B; the third requires a documented ITP mapping,
not model-name policy.
