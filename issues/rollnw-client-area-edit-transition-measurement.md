# rollnw client area edit transition measurement

## Problem

Area editing now has synthetic regression coverage and a real-area renderer
transition harness. The remaining gap is the desktop-shell interval from an
input event through command publication and RmlUi work before the renderer's
live-area refresh begins.

## Observed data

Measurements used a 1280x720 viewport on an AMD Radeon 890M. `fda` is the
then-current 32x32 new-area initialization: 1,024 placements of one repeated
tile, not an authored area distribution. The 0.2.0 development line now
initializes new areas from a deterministic varied batch of compatible flat
default-terrain tile IDs and orientations. The `fda` measurements below are
therefore historical input-specific results, not measurements of the current
new-area distribution. `pvp_area_2` is a varied 32x16 authored area with 512
tiles, 119 tile ids, 7,522 prepared draws and 1,020 local lights.

The old `fda` selection cache materialized 422,912 world-space triangles and
used 15,257,600 bytes. Its 64-tile orientation refresh median was 63.03 ms,
including 62.98 ms of render-record and surface-cache work.

The area cache now stores one model-local triangle array per unique live model
and one dense placement row per scene model. Initial `fda` load stores one
geometry, 1,024 placed instances and 413 triangles in 174,664 bytes. After the
benchmark's model edit/undo cycle it retains the most recently displaced
geometry for redo: two geometries and 590 triangles in 181,088 bytes. Older
inactive geometry is removed on the next edit batch.

`pvp_area_2` initially stores 119 geometries, 512 placed instances and 25,694
triangles in 1,021,340 bytes. After edit/undo it retains one displaced geometry:
120 geometries and 25,872 triangles in 1,027,800 bytes. Preserving those shared
geometries across a draw-row fallback reduced its measured 64-tile model edit
from 63.36 ms to 1.57 ms and its undo from 116.87 ms to 1.61 ms.

Static opaque/cutout surfaces now batch repeated unskinned primitives through
one flat world/normal-matrix instance stream. The color pass consumes the
area cache's stable indexed surface rows directly; only dynamic and animated
records rebuild transient prepared draws per frame. The default `fda` frame
therefore submits 21 draw calls for all 21,504 logical surfaces (26 calls after
the benchmark's alternate model is present). The previous path exhausted the
16 MiB uniform ring at 10,922 individual draws and silently omitted the
remaining surfaces, so its GPU result was not a complete-frame baseline.

Vulkan pass timers now place both timestamps at `ALL_COMMANDS`. The old
top-of-pipe/bottom-of-pipe pair made later empty pass timers include preceding
opaque work; the edit benchmark no longer sums those cumulative intervals.

Tile hover is now an explicit benchmark scenario. It moves an already-retained
preview batch between two disjoint cell ranges without changing `AreaTile`
rows. Before the preview protocol changed, hiding one authored tile made the
32x32 `fda` frame abandon cached draw lists and rebuild a visible surface-index
stream from 21,504 prepared surfaces. The hover update itself was already
small: 0.0029 ms for one tile and 0.018 ms for 64 tiles.

The preview now stores only a batch of suppressed authored record indices.
Authored `ModelInstance` rows remain unchanged, preview rows stay dynamic, and
the renderer filters the cached instanced root stream by the area frame's
record marks. The extra retained storage is one `uint32_t` per covered preview
tile; the suppression state uses one previously unused bit in each existing
record flag. Invalid model-to-record mappings reject the replacement batch.

On `fda`, median area-frame preparation fell from 0.067 ms to 0.026 ms for a
one-tile hover and to 0.028 ms for a 64-tile hover. Thirty-sample complete-frame
medians were 1.85 ms and 1.94 ms; both cases retained cached draw lists. On the
varied `pvp_area_2`, 20-sample complete-frame medians were 1.88 ms and 2.25 ms,
also with cached draw lists. GPU medians stayed within 0.84--0.86 ms on `fda`
and 0.60--0.61 ms on `pvp_area_2`; the removed work was CPU surface-list
construction.

Final 10-sample medians were:

| Area and operation | Tiles | Edit refresh | Edit presented | Undo refresh | Undo presented |
|---|---:|---:|---:|---:|---:|
| `fda` orientation | 1 | 0.08 ms | 2.61 ms | 0.09 ms | 3.01 ms |
| `fda` orientation | 64 | 0.76 ms | 3.90 ms | 0.75 ms | 4.01 ms |
| `fda` model replacement | 1 | 1.44 ms | 4.45 ms | 1.30 ms | 4.25 ms |
| `fda` model replacement | 64 | 1.97 ms | 5.08 ms | 2.00 ms | 5.04 ms |
| `pvp_area_2` orientation | 1 | 0.05 ms | 2.52 ms | 0.06 ms | 2.31 ms |
| `pvp_area_2` orientation | 64 | 0.52 ms | 3.46 ms | 0.50 ms | 3.36 ms |
| `pvp_area_2` model replacement | 1 | 0.53 ms | 3.57 ms | 0.56 ms | 3.86 ms |
| `pvp_area_2` model replacement | 64 | 1.75 ms | 4.86 ms | 1.82 ms | 4.95 ms |

For the 64-tile model replacement, `fda` CPU render time fell from 5.10 ms
to 1.00 ms and presented time from 13.29 ms to 5.08 ms while rendering the
complete surface set. `pvp_area_2` CPU render time fell from 2.77 ms to
1.32 ms and presented time from 6.63 ms to 4.86 ms. Corrected final GPU
medians were 1.87 ms and 1.24 ms respectively.

The Release synthetic 32x32 medians were 0.040 us and 2.98 us to update one
and 64 dense placement rows. A ray tested against all 1,024 placement bounds
and 413 shared triangles in 23.1 us for a hit and 19.2 us for a miss. The
aggregate tile commit path, including stable light and record rows, measured
76.5 us for one tile and 658 us for 64 tiles.

## Required measurement

Instrument the remaining client publication boundary around command history,
RmlUi work and the call into `ViewerSession::refresh_live_area_tiles`. Add that
interval to the existing renderer report or a client-driven companion report;
do not duplicate the real-area renderer scenarios.

Invalid Area resources, out-of-range tile batches and unavailable GPU timing
must fail or report an explicit unavailable field; they must not emit zero as a
measurement. The harness must restore the Area exactly and write no project
resource.

## Done criteria

- [ ] Rerun the harness against the current randomized 32x32 initialization;
      the varied authored-area run must still avoid modifying The Awakening.
- [ ] Results include the client publication and RmlUi interval.
- [x] Results separate cold import, live scene refresh and the first rendered
      frame.
- [x] One-tile and 64-tile commit/undo samples have warmup, repeated
      measurements and machine-readable output.
- [x] One-tile and 64-tile retained hover-preview samples report update,
      frame preparation, CPU/GPU render, draw counts and cached-list use.
- [x] The renderer optimization was selected from the measured dominant phase.
