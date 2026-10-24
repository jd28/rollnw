# Retained model poses

Status: closed, 2026-10-03. Implementation commit: `d064ff1ca`.

Cluster: [Rendering performance overhaul](../rendering-performance-overhaul.md).

Pose reuse and invalidation verified. The later combined pass in [retained
shadow groups](retained-shadow-groups.md) completes the image/validation checks
left open in this isolated checkpoint.

Tier 1: stop repeating pose publication when the bound asset and pose inputs
are unchanged. Measure this before changing draw submission.

## Patterns & Conventions Found

- `model_instance_animation.cpp:173` samples a batch of borrowed instance/model
  pairs; even disabled instances republish every static node. Keep the existing
  forced batch and singular wrapper as the reference path.
- `viewer/preview_model_animation.cpp:815` collects scene-owned pairs;
  `viewer/session.cpp:1431` currently samples all of them and synchronizes every
  enabled root. Existing spans, flat structs and reusable vectors are the fit.
- `viewer/preview_scene.cpp:5285` recreates instances on tile asset replacement.
  Placement edits explicitly publish static transforms. Animation rebuilds reset
  the entire state; door-preview restoration swaps pose outputs explicitly.
- `tests/render_preview_model_animation.cpp:219` and
  `tests/render_model_instance.cpp:387` provide static, shared-asset, attachment,
  animation and edit tests. Follow these and repository clang-format.

## Architecture Decision

The input is a batch of instance/model bindings, root matrices, enabled flags,
clip indices, times, looping flags and fallback policy. The output is existing
instance-owned local/model poses, skin matrices and node-world arrays plus
per-input sampled/updated status. Retain successful outputs while those inputs
are equal; failed samples must retry. No extra pose copy or new allocation.

Real workload: max_city has 1,208 disabled-animation instances, 472 enabled
instances and 600 particle systems. Every instance currently passes through
sampling. Static node publication was 4.30% of a zero-loss cycle profile;
backend sampling and model-matrix construction also appeared prominently.
Clip/time/root change frequency varies by gameplay; held door clips explicitly
use playback_rate=0. The current area setup may also select looping hold clips;
we will measure actual reuse rather than assume all doors are frozen.

Platform: Ryzen AI 9 HX 370 / Radeon 890M, Linux Vulkan, Release/Tracy OFF. CPU
and GPU overlap and share memory; measure whole-host-frame latency as well as
pre-render time. The previous matched max-city median was 7.569 ms after the
light-statistics and shadow-visibility changes, which remain in both binaries.

ASSUMPTION: the synthetic repeated city and original city/outdoor are useful
initial workloads — affects benchmark coverage, not a full-game guarantee.

## Component Design

- Add an instance-owned flat input snapshot (approximately 80 bytes), reset
  with animation state. Root matrix, scene model index, enabled, clip, time,
  looping and fallback policy identify a reusable result. Disabled instances
  ignore irrelevant clip/time fields. Exact matrix/float comparison avoids
  approximate animation; NaN inputs cannot hit the cache.
- Add a changed-input batch sampler beside the existing forced batch. The
  cached path requires a stable asset binding: replacing or modifying the
  bound model/backend/pose data requires resetting snapshot validity or forced
  sampling. Source asset replacement already recreates instances. Explicit
  static publication, forced sampling, backend creation, runtime state transfer
  and door-preview restoration invalidate the snapshot. Normal root/time/clip
  writes are detected without extra setter APIs.
- A successful cached result preserves existing sampled/disabled counts and
  reports reuse separately. Per-input `updated` allows the session to skip
  runtime bounds/attachment/light sync for unchanged animated roots.
- Null inputs retain existing failure counts; invalid clips retain modulo
  semantics; invalid skeletons and backend failures never become reusable.

## Implementation Map

- Modify `lib/nw/render/model.hpp`, `model_instance_animation.{hpp,cpp}` for the
  flat snapshot, updated/reused outputs and changed-input batch transform.
- Modify `viewer/preview_model_animation.cpp`, `preview_scene.cpp`, `session.cpp`
  for the shared frame path and audited mutation boundaries.
- Modify `tools/mudl/viewer_runtime.cpp` to expose reuse in the existing benchmark JSON.
- Modify `tests/render_model_instance.cpp` and
  `tests/render_preview_model_animation.cpp` for reference parity across input
  changes, failure/recovery, static publication, door leases and attachments.
- Preserve a baseline executable and raw evidence under
  `/tmp/rollnw-retained-poses`; record results here.

## Data Flow

Scene index arrays -> borrowed batch pairs -> compare flat input snapshots ->
retain unchanged outputs or run the existing sampler -> record successful
inputs -> synchronize only updated animated roots. The force-sampling API
always recomputes and invalidates prior reuse state. No singleton exception.

The existing pairs use pointers because assets and instances have independent
owners/stores and the common sampler is also used outside scenes. They are
borrowed for one batch and never retained; replacing these bindings requires a
separate store/interface refactor. The new snapshot contains only values and a
scene index, not owning or retained pointers. All operations remain on the
existing render thread with no new synchronization.

## Build Sequence

1. Implement retained inputs and audit all production pose writers; build and
   compare cached results against the forced sampler through transitions.
2. Run animation/attachment/edit tests, then alternating hardware benchmarks.
3. Check unchanged render commands, validation and images; record actual reuse,
   frame latency, CPU phase time and memory cost before selecting further work.

## Critical Details

Cost: about 131 KiB for 1,680 snapshots, plus one boolean in existing sample
record padding and one aggregate counter. The common pass still visits each
instance, reading its small snapshot/root with predictable unchanged/animated
branches. Hits avoid all node/skin writes and backend work. Actual speedup is
unverified until paired measurements; no numeric target is inferred from cycle
percentages. Maintenance cost is an explicit invalidation obligation for rare
asset/backend/output mutation, covered at the inspected boundaries.

Simplification: reuse existing output buffers and force sampler; retain once
instead of adding a second pose store, dirty queue, setter framework, asset
hashing pass or animation approximation. Root comparison avoids imposing new
setter requirements on game/editor placements. Draw preparation is measured
after this change rather than combined speculatively.

Done: reference-equivalent node/skin/pose outputs after changes, no repeated
sampling for unchanged static/held inputs, passing edit/attachment tests, clean
Vulkan validation, and repeatable whole-frame improvement. Any stale pose or
inconsistent binding invalidation disproves correctness; revert the cache if
its maintenance or runtime cost exceeds the measured benefit.

## Results — 2026-10-03

Release / Tracy OFF, Ryzen AI 9 HX 370 / Radeon 890M Vulkan. Three alternating
pairs per scene: 1920x1080 gameplay camera, 60 warmup and 360 measured frames,
fixed 16 ms simulation step. Medians of the three per-run medians:

| Scene | Host frame before -> after (ms) | Pre-render CPU before -> after (ms) |
| --- | ---: | ---: |
| Repeated populated city, 32x32 | 7.077 -> 6.924 | 1.313 -> 1.033 |
| Original city, 16x16 | 2.210 -> 2.121 | 0.204 -> 0.126 |
| Outdoor, 32x16 | 1.045 -> 1.014 | 0.072 -> 0.028 |

Max-city reuses all 1,208 static instances; its 472 enabled instances continue
advancing. Pre-render savings were repeatable; the modest whole-frame change
included one slower pair. Held-animation reuse is tested separately, not
inferred from these advancing frames. Do not add nested CPU and GPU timers.

All 121 focused tests passed without skips, including forced/retained pose
parity, shared assets, time/clip/root/looping changes, failures and recovery,
static publication, binding-index changes and door-preview restoration.
All 3,240 paired frames have identical existing non-timing counters; the
new reuse count is excluded. Build and formatting checks pass.

Raw runs, scripts, XML results and parity analysis are under
`/tmp/rollnw-retained-poses` (`pose-*`, `tests.xml`, `pose-parity.json`).
Preserved binaries are `mudl-before-retained-poses` and
`mudl-after-retained-poses` under `build/tools/mudl`. This isolated step does
not yet establish image/validation parity; subsequent combined checks must
cover that requirement before treating the complete runtime pass as verified.

Final self-check: input ranges, common case, ownership, borrowed-pointer
justification and failure behavior are documented above. The simplification
reuses existing output buffers and sampling logic without approximation or
new GPU resources. The required maintenance cost is explicit invalidation at
rare asset/backend/output mutation boundaries. Draw submission remains
unaddressed; this is not a negligible full-game 32x32 cost.
