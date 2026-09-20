# Area eraser and persistent placement diagnostics

Status: closed, 2026-09-18. Tier 1 behavior correction.

## Framing and observed data

The tile eraser must erase the tile under the pointer or the complete placed SET
group containing it. It must never widen that selection to a neighboring group.
A Door hit identifies its owning SET-hook tile; it is never a separate eraser
target. Failed candidate tile previews disappear immediately, while attempted
edits keep their diagnostic in Output. Hover preview picks consume the newest
queued pointer coordinates so they identify the same tile as button input. An
invalid target retains a red cell indicator without substituting a candidate
tile.

The real platform is the Linux SDL3/RmlUi desktop editor. Tile edits run on the
UI thread, Area tiles are a row-major array, SET groups and door slots are stable
within one resource generation, and Area membership stores ObjectManager-owned
pointers. Windows uses the same source but is unavailable here.

Observed inputs:

- Strokes provide unique row-major tile indices below `width * height`.
  Malformed dimensions, duplicates and stale handles reject.
- Tiles contain SET id, orientation 0..3, height and lighting/animation fields.
  Group identity comes from complete SET footprints, including random cells.
- Door ownership comes from current SET hook positions and live Area Door
  handles. Missing or cross-tile ownership rejects a Door hit.
- The real `.Loading.` area is a 4x4 TDR01 grid. Its center cells 5, 6, 9 and 10
  are four adjacent one-cell SET groups. Painting default terrain through every
  shared corner made one click depend on all four groups; recursively widening
  the target then made a local erase remove a large connected region.
- Production frequency and distribution are unmeasured.

ASSUMPTION: a valid placed Door resolves to one tile across its occupied hook
records — affects rejection of missing or ambiguous hook ownership.

## Transform and contracts

1. Resolve each picked cell to its complete verified group footprint. The sorted
   result contains only explicitly intersected groups; neighboring groups are
   fixed boundaries.
2. Paint default terrain and clear crossers at selected corners/edges unless the
   topology element touches an unselected placed group. Fit the selected group
   cells to canonical non-group tiles and refit only incident ordinary
   transition tiles. If no local fit exists, reject without mutation.
3. Collect Door handles only from changed rows inside the resolved target.
   Validate current and candidate hook snapshots while omitting exactly those
   Doors; another occupant or a Door on a neighboring transition tile rejects.
4. Apply Door membership removal and tile rows as one command, publish once, and
   retain exact tile rows plus original Door order for undo/redo. A batch without
   a tile row is rejected defensively.
5. For pointer input, test visible Doors first, map a hit to its unique hook tile,
   and send that tile through the same transform. No object selection or
   door-specific stroke state is created.
6. Build and validate hover edits before rendering. Failure restores any
   substituted tile and renders only red indicators for the input cells. Hover
   does not write Output; the click/release attempt writes one warning or error
   and leaves palette geometry unchanged.
7. Coalesce pointer motion to one preview update per frame while retaining the
   newest motion-event coordinates. Camera drag and wheel refreshes update the
   same queued point; press and release continue to pick from their event point.

Ownership: builders own output vectors; Area/ObjectManager own live data; command
history owns detached Doors until undo history releases them. Borrowed spans last
only for each synchronous call. All mutation and replay remain on the UI thread.

Out-of-range policy is reject-and-clear. Missing/incomplete/ambiguous groups,
invalid hooks, stale rows, allocation failure and container overflow produce no
partial writes. A valid no-change erase is empty and cannot delete a Door alone.

## Cost and simplification

Existing fitting uses O(area cells) scratch arrays and a linear row pass. Erase
adds two byte masks for selected and placed-group cells, scans the Area tile rows,
resolves each discovered footprint with the existing SET group lookup, and
examines four corners and four edges per selected cell. Hook snapshots scan
tiles/slots and Area Doors. Undo retains O(changed tiles + removed Doors) data.
These costs are paid synchronously on the desktop UI thread. Pointer motion
remains one coalesced pick per frame. No latency measurement or performance
improvement is claimed.

The simplification pass removed recursive neighbor closure, direct-Door inputs,
Door stroke storage, door-only undo, object highlighting and its workbench
suppression. One tile pipeline handles ordinary tiles, multi-cell groups and Door
hits. Existing group resolution, fitting, membership ownership, command history
and Output are reused. No cache, generalized transaction layer, approximate
group inference or hover log buffer was added.

## Done and contrary evidence

Done means one click resolves to one tile or one complete group; adjacent groups
never enter the target or edit rows; every `.Loading.` cell builds a local erase;
a Door hit targets its hook tile; attached Doors disappear only with selected
changed tiles; and one undo/redo restores exact tiles and Door order. Invalid
placement shows no candidate tile and retains a red cell indicator. Attempted
failures persist in Output without moving the palette.

Contrary evidence is a neighboring grouped row in an erase batch, an occupied
hook rejection for the selected Door, a door-only edit, an unrelated Door
deletion, partial writes, hover Output spam, or a nonempty candidate tile preview
after validation failure.

## Evidence

- The real 4x4 TDR01 fixture resolves each of its 16 clicks to exactly that cell,
  builds every local erase, and never includes another center group in its edit
  rows.
- Existing tests cover rotated/random multi-cell groups, attached Door removal,
  exact undo/redo, detached lifetime, stale and door-only rejection, another
  co-located hook occupant, Door-to-hook-tile resolution, real TTR01 group
  orientations, persistent Output and stable palette geometry.
- The Release client and test targets compile after the correction.
- The complete Release test binary ran 2,270 tests: 2,228 passed and 42 were
  skipped by their existing conditions.
- Eight directly changed eraser/group/Door-hook cases passed under ASan/UBSan
  with leak detection disabled.
- The runnable client and panel stylesheet in `bin/` match the Release build.
  Desktop pointer latency and Windows behavior remain manual verification items.

## Final self-check

- Inputs, outputs, ranges, ownership, lifetime, volatility and costs are stated;
  the unobserved Door invariant and production distribution are labeled.
- The common path is one selected tile/group batch. Ordinary transitions refit
  locally; errors remain outside mutation and preview rendering.
- The selection boundary and hook behavior are explicit. No speculative option
  or extension point was introduced. Batch transforms remain plural; the
  displayed pointer query is the existing singleton.
- Formatting and diff checks pass. Release and sanitizer results are recorded;
  desktop pointer latency and Windows behavior were not exercised here.
