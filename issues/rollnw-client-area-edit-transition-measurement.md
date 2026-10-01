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

## Desktop edit trace investigation (2026-10-01, Tier 1)

### Patterns & Conventions Found

- `tools/client/client_application_frame.cpp:418` owns the single UI/frame stream
  and already measures renderer work with monotonic counters.
- `tools/client/client_application_workspace.cpp:48` reconciles mutation epochs;
  `:191` refreshes workbench snapshots after object edits.
- `tools/client/object_workbench_view.cpp:2324` rebuilds creature details, classes,
  feats, spells and inventory; the cost of each needs measurement.
- `tools/client/client_metrics.hpp` holds HUD metrics, but neither it nor the
  renderer benchmark records the edit publication interval. Reuse existing
  command/history and renderer paths, JSON output and Release build conventions.

### Architecture Decision

Measure the missing client boundary before optimizing. Linux desktop, one UI
thread, RmlUi and asynchronous Vulkan are the real platform. Keep the renderer
benchmark intact; an opt-in JSONL trace records inclusive phase times in the
actual client. The timing endpoint is CPU return from presentation, not physical
display visibility; display latency is explicitly unavailable.

Inputs are live document mutations: tile batches of 1 and 64, class-level,
equipment and item appearance edits, plus exact undo. Use real imported fixtures
and disposable project copies. ASSUMPTION: isolated edits are the common workload
— affects replay scheduling; no user action-frequency distribution is available.
Document membership and catalogs are stable during each edit/undo pair; mutation
epochs, UI snapshots and affected visual rows change. Existing command bounds and
rejection policy remain authoritative. Trace output errors fail explicitly;
skipped frames have unavailable presentation measurements.

### Component Design / Implementation Map

- `client_edit_trace.hpp/.cpp`: fixed contiguous timing/count columns; one UI
  thread owns the recorder, nested scopes borrow its current frame. JSONL is
  written only after measured work. The frame stream is a true singleton;
  the measurement report is a batch of independent frame rows.
- `client_application_frame.cpp`, `client_application_workspace.cpp`,
  `object_workbench_view.cpp`, `creature_workbench_view.cpp`,
  `inventory_workbench_view.cpp`, `toolset_backend.cpp`, `renderer.cpp`: scope
  existing boundaries without changing their behavior for the baseline.
- Tests and this issue: trace correctness, exact editing/undo/save state,
  repeatable workload and before/after evidence. Keep any temporary replay
  instrumentation as evidence rather than adding a general automation protocol.

### Data Flow / Cost

Input dispatch -> command/history -> mutation synchronization -> workbench and
scene refresh -> layout/UI rendering -> viewport -> presentation return -> JSONL.
Phase times are inclusive; nested values must not be summed. Disabled scopes do
one thread-local pointer check with no clocks or allocation. Enabled tracing
uses a fixed array and two clock reads per scope; report allocation/I/O occurs
after the timed interval. Existing SDK/object pointers are synchronous API
borrows, not a new pointer-based hot data path. No throughput claim is intended;
the requirement is interactive latency and attribution.

### Build Sequence / Critical Details

1. Record repeated Release baselines with warmup and explicit input identity.
2. Change one demonstrated source of redundant work, or stop if shell cost is
   negligible. No timing threshold has been supplied; report absolute costs.
3. Repeat matched workloads, verify exact state restoration and visual/selection
   behavior, run affected regressions and clang-format, and record limitations.

Simplification: reuse the live client loop and command transactions, avoid a
second renderer, worker, per-object journal or general replay framework. There is
no need for approximation, lookup tables or producer/consumer buffering here.
Done requires real shell timings and evidence for any optimization; a stale UI,
different saved state, invalid undo, or unmatched timing workload disproves it.
Plan B is to retain the trace and measurements without a speculative change.

## Original remaining measurement contract

Instrument the remaining client publication boundary around command history,
RmlUi work and the call into `ViewerSession::refresh_live_area_tiles`. Add that
interval to the existing renderer report or a client-driven companion report;
do not duplicate the real-area renderer scenarios.

Invalid Area resources, out-of-range tile batches and unavailable GPU timing
must fail or report an explicit unavailable field; they must not emit zero as a
measurement. The harness must restore the Area exactly and write no project
resource.

## Done criteria

- [x] Rerun the harness against the current varied 32x32 initialization;
      the varied authored-area run must still avoid modifying The Awakening.
- [x] Results include the client publication and RmlUi interval.
- [x] Results separate cold import, live scene refresh and the first rendered
      frame.
- [x] One-tile and 64-tile commit/undo samples have warmup, repeated
      measurements and machine-readable output.
- [x] One-tile and 64-tile retained hover-preview samples report update,
      frame preparation, CPU/GPU render, draw counts and cached-list use.
- [x] The renderer optimization was selected from the measured dominant phase.


## Desktop results (2026-10-01)

The complete client measurement is now available with
`ROLLNW_CLIENT_EDIT_TRACE=/absolute/path/edits.jsonl`. It records changed frames
only. Times cover dispatch through **CPU return from presentation**, including
commands, deferred UI callbacks, queries, scene refresh, RmlUi and rendering.
They exclude time waiting in the OS input queue and physical display latency.
Phase columns are inclusive; never sum nested columns. Missing presentation is
`null`, and physical display latency is always explicitly unavailable.

The measured change retains the existing Item editor template on non-inventory
mutations. Its data model already reads the changed Item; rebuilding the complete
workspace repeated DOM construction and layout. Existing hydration still updates
navigation/header state. Inventory mutations on the inventory surface retain the
existing rebuild path. No command, mutation, undo, serialization, renderer or
shader semantics changed.

Matched runs used Release, Tracy off, uncapped rendering, a 3841x2028 client
framebuffer, Ryzen AI 9 HX 370 / Radeon 890M (RADV, Mesa 26.2.2-arch1.1). Each
process ran three warmup edit/undo pairs and 20 measured pairs per scenario;
order was baseline, retained editor, retained editor, baseline. Each table cell
therefore has 40 measured samples. p95 uses nearest rank. Native SDL mouse events
exercised class, equipment and armor controls; undo used Ctrl+Z. Area edits
called the backend tile batch API from the dispatch interval, so they do not
measure native brush hit testing. Event injection itself is included.

The current generated 32x32 input has 1,024 tiles, 12 tile IDs and four
orientations. The copied authored area has 512 tiles, 119 IDs, two creatures
and 18 placeables. Equipment swaps two belt instances on `pl_agent_001`; it is
not a chest-armor geometry benchmark. Class edits use `wizard_pm`. Armor edits
set global Cloth 1 on `cloth028` from 7 to 1, then undo. Hashes, distributions,
raw traces, action labels, repeat instructions and screenshots are in
[the evidence directory](client-edit-transition-evidence/README.md).

| Operation | Before edit median / p95 (ms) | After edit median / p95 (ms) | Before undo median (ms) | After undo median (ms) |
|---|---:|---:|---:|---:|
| Current 32x32, one tile | 4.80 / 5.88 | 4.83 / 5.76 | 5.72 | 5.83 |
| Current 32x32, 64 tiles | 5.69 / 7.54 | 5.61 / 6.74 | 6.43 | 6.74 |
| Authored area, one tile | 5.29 / 7.44 | 4.95 / 6.29 | 6.42 | 6.42 |
| Authored area, 64 tiles | 5.39 / 7.44 | 5.57 / 6.87 | 7.42 | 7.27 |
| Class level | 9.32 / 18.81 | 9.15 / 17.72 | 8.92 | 8.60 |
| Belt swap | 25.50 / 38.54 | 26.43 / 38.19 | 26.25 | 26.95 |
| Armor color | 28.76 / 50.74 | 24.14 / 35.47 | 29.74 | 23.46 |

Armor color edits dropped 4.61 ms (16%) in the pooled median. Baseline per-run
medians were 26.94 and 33.66 ms; retained-editor medians were 24.47 and 23.40 ms.
This desktop has noticeable run-to-run variance. No improvement is claimed for
the unchanged area, class or equipment paths.

The armor trace proves both the removed work and the remaining bottleneck:

| Inclusive CPU phase | Before median (ms) | After median (ms) |
|---|---:|---:|
| Complete workspace rebuild | 2.24 | 0.00 (zero calls) |
| RmlUi Update | 4.48 | 2.52 |
| Construct item mannequin and icon | 0.92 | 0.91 |
| Load mannequin models into preview session | 13.46 | 13.32 |

**Color decoding already happens in the shader.**
`render_nwn_static.ps.hlsl::sample_plt` reads the selected palette rows from
surface constants. `model_renderer.cpp::apply_render_model_material_plt_constants`
copies those values into each draw. The current editor nevertheless resets the
mannequin on every mutation epoch, serializes/deserializes its equipped Item,
and calls `load_live_object` on the changed preview revision. The 24 ms is not a
shader measurement. The measured next target is retaining models and updating
palette constants; tracked in
[the palette refresh issue](rollnw-client-item-preview-palette-refresh.md).

Validation: 103 focused tests passed, including item edits/undo/serialization,
inventory navigation, preview ownership, native appearance controls and trace
contracts. All 1,288 replay actions (including warmup) completed. Every edit/undo
pair restored the serialized root exactly; all four runs began each scenario
with identical serialized data and preserved viewport selection. The candidate
also asserted that the Item template survived every edit/undo. Both native
screenshots were inspected: mannequin, icon, selected color and editor layout
remain present. Temporary replay and screenshot hooks were removed from product
sources; their patch is retained solely for reproducing the evidence.

Limitations: initial exploratory runs from the repository root missed UI PNGs;
those runs (including the earlier ~40 -> 23 ms comparison) are excluded from
this table. Corrected runs load all UI textures. The class fixture still reports
missing optional equipment model rows; its result covers the rendered fixture,
not a complete representative creature wardrobe. Logs retain those diagnostics.
GPU duration, compositor/display latency, Windows and cold first-edit latency
were not measured. Serialization equality verifies undo state; this experiment
does not time disk saves.

Final self-check: the common color-edit case now avoids template replacement;
no new production replay API, worker, lookup table or cache was introduced.
The simplification removed repeated DOM construction, keeping existing error
and data-model contracts. Diagnostics use a fixed contiguous frame row and a
single UI-thread owner; no new pointer-heavy traversal was introduced. Measured
success criteria, retained UI, exact undo and build/test checks passed. The
remaining mannequin/model work is explicitly unresolved, not an achieved
performance claim.


## Follow-up: palette retention completed

The subsequent [retained palette update](rollnw-client-item-preview-palette-refresh.md#results-and-final-self-check)
removed mannequin model reloads on color edits. A fresh matched comparison with
both variants retaining the Item DOM measured 21.72 -> 8.60 ms median native
color edits, and 21.11 -> 8.16 ms undo. Every measured color edit/undo now makes
zero mannequin-model reload calls. These are CPU presentation-return intervals;
shader duration and physical display latency remain unmeasured.
