# Area frame CPU cost

Status: closed, 2026-10-03. Implementation commit: `ea37a3c80`.

Cluster: [Rendering performance overhaul](../rendering-performance-overhaul.md).

Light-statistics and shadow-visibility reuse verified by tests, paired counters
and validation; image limits remain recorded below.

Tier 1: reduce measured steady-state CPU work in the shared game/tool viewer.
This is separate from area loading and shader execution.

## Patterns & Conventions Found

- `viewer/session.cpp:423` recounts selected lights once per cached surface in
  each of four passes. Existing `add_saturating` preserves uint32 counters.
- `viewer/area_render_scene.cpp:4094` already produces visible record indices;
  `prepared_model_surface_draws_for_record` provides validated contiguous ranges.
  The four pass streams partition those ranges; dynamic records have no cached
  surfaces. Reuse these arrays instead of constructing another histogram.
- `tests/render_viewer_lighting.cpp:83` provides small real scene/cache fixtures;
  follow these Google Test and local clang-format conventions.

## Architecture Decision

Input is a prepared area frame, its scene-owned surface/light index ranges, and
the current local-light values. Output is the same six selected-light counters
in `ViewerFrameStats`. Count each visible record's surfaces from its range size,
then inspect that record's light list once and weight totals by surface count.
No surface payload, rendering command, shader, or light selection changes.

Observed workload: synthetic `max_city` repeats the real 16x16 ms_4city four
times, with 1,024 tiles, 1,680 models, 600 particle systems and 2,200 lights
(20 nonzero). A representative frame has 1,245 visible records and 19,295 cached
visible surfaces. Most meshes share a record's light list. Geometry is stable;
visibility and light values may change every frame and are always read fresh.

Platform: Ryzen AI 9 HX 370 / Radeon 890M, Linux RADV, Release, Tracy disabled.
CPU submission and GPU execution overlap and share memory bandwidth. The
requirement is whole-host-frame latency, including waits, not draw throughput
alone. A 199 Hz user-cycle profile (3,159 samples, zero lost) attributes 5.09%
of self cycles to the old statistics reduction; this includes some report
serialization and is a hotspot ranking, not an isolated timing.

ASSUMPTION: this repeated city plus the original city/outdoor are useful initial
rendering workloads — affects benchmark coverage, not a shipping-game promise.
They do not represent combat, AI, pathfinding, camera motion, or NPC crowds.

## Component Design

- Extend the existing session statistics reducer into one batch function over
  a borrowed `AreaRenderFrame`. The scene and frame must refer to the same
  prepared data, as at the existing render call site. No retained state.
- Record and light indices stay uint32; invalid record ranges are empty and
  invalid light indices are skipped. Products use uint64 and totals saturate
  to uint32. Empty, culled, suppressed and dynamic-only ranges contribute zero.
- Reuse existing visibility records, including the cached-stream visibility
  filter semantics. Lights with zero color still count in selection totals;
  colored totals retain the existing > 1e-4 threshold.

## Implementation Map

- Modify `lib/nw/render/viewer/session.{hpp,cpp}`: declare the narrow batch stats
  reducer, replace the per-surface helpers and four calls with one record pass.
- Modify `tests/render_viewer_lighting.cpp`: compare against an independent
  surface-by-surface reference across material passes, visibility, suppression,
  current light changes, invalid light indices and counter saturation.
- Maintain this issue with measured results; raw reports remain under
  `/tmp/rollnw-frame-cost`, following the current evidence cleanup.

## Data Flow

Prepared visible record indices -> validated cached surface range length ->
one light-list reduction per contributing record -> weighted saturating frame
totals. Everything remains on the calling render thread; borrowed references
only identify batch owners. Hot traversal uses indices and contiguous spans.

## Build Sequence

1. Implement the reducer and parity tests, then build mudl and rollnw_test.
2. Run focused correctness checks and alternating before/after hardware runs.
3. Inspect complete host frames, individual CPU phases and unchanged draw/light
   counters. Keep the change only if correctness and repeatable timing support it.

## Critical Details

Cost changes from O(sum(surfaces per record * lights per record)) to O(visible
records + their light indices), with no new retained or per-frame allocation.
Index traversal is mostly linear; light data access follows existing index
lists. Empty-range/invalid-index checks are predictable on prepared input.
The expected latency improvement is a hypothesis until paired runs complete.

Simplification: the existing surface ranges remove even the proposed scratch
histogram and surface counting pass. No approximation, cache, invalidation,
extra parameter, profiler, or new subsystem is needed. Preserve diagnostics
rather than disabling them for a benchmark-only improvement.

Done: exact counter agreement with the surface reference, existing lighting
checks pass, identical non-timing frame output in paired workloads, and lower
whole-frame latency in repeated runs. A local-timer-only win or inconsistent
whole-frame result does not prove a runtime gain. Plan B is to discard this
implementation and retain the profile to select another measured hotspot.

## Second measured transform: shadow visibility

The same zero-loss profile attributes 4.13% of self cycles to
`bounds_intersects_shadow_clip`. Inspection shows every candidate is tested
once for counters, again for submission, and sometimes a third time in scalar
fallbacks. Both directional cascades and local shadow slots do this.

Reuse the existing frame-local `ShadowRenderModelCandidate` array: store a
visibility bit per shadow pass in an unused padding byte. The candidates and
matrices are unchanged during submission; compute each candidate/pass result
once in the existing statistics loop, then read that bit for every submission
path. Preserve the exact eight-corner test, padding, draw order and batching.
This is a batch of candidate rows and at most three cascades/four local slots,
not a persistent cache. No allocation or candidate-size increase is needed on
this build. Reject invalid pass counts before indexing the fixed matrix arrays.

Modify only `lib/nw/render/viewer/scene_shadow.cpp` for this transform. Existing
shadow correctness tests and complete before/after command/counter comparisons
will exercise the shared path; hardware validation and image comparisons cover
submission behavior. New tests will cover out-of-range pass counts. Separate
the light-only binary before building and measuring this second change.

Simplification removes duplicate bounds transformations rather than changing
the culling math or introducing another spatial structure. The cost becomes
one existing clipping test per candidate/pass plus bit writes/reads; the
expected host-frame latency effect remains unverified until paired runs.

## Results — 2026-10-02

Both transforms are implemented. Release/Tracy OFF on the same Ryzen AI 9 HX
370 / Radeon 890M. Each timed run uses the gameplay camera, 1920x1080, 60 warmup
frames and 360 measured frames advancing simulation by 16 ms. Before/after
order alternates between repetitions. No concurrent builds or tests. Loading,
final queue drain and JSON serialization are outside host-frame samples.

Values below are medians of per-run medians, in milliseconds. The max scene has
five paired repetitions; the original city and outdoor have three each.

| Workload | Host frame before | Host frame after | Change |
| --- | ---: | ---: | ---: |
| Synthetic 32x32 populated city | 8.232 | 7.569 | -8.1% |
| Original 16x16 city | 2.571 | 2.553 | Inconclusive in run variability |
| Original 32x16 outdoor | 1.191 | 1.070 | -10.2% |

Every paired max-city median decreased, by 0.364–1.108 ms. Per-run medians ranged
7.977–8.628 ms before and 7.520–8.026 ms after. Max-city p95 medians were 9.894
and 9.624 ms, but individual p95 runs varied substantially and did not improve
consistently; no tail-latency improvement is claimed.

Max-city CPU phase medians: setup 1.116 -> 0.691 ms; directional shadows 1.968
-> 1.838 ms; local shadows 0.608 -> 0.516 ms; total render 5.839 -> 5.245 ms.
These overlapping/nested timers are not additive with whole-frame or GPU time.

The light-only comparison was measured before adding shadow reuse: setup
1.192 -> 0.721 ms, but complete frames were noisy (three paired 720-frame runs,
one regression). It did not independently establish a reliable whole-frame
gain. Shadow-only comparisons against the saved light-only executable also
overlapped in whole-frame latency. The final combined workload comparison above
is the evidence for the reported runtime gain, not a sum of local estimates.

Correctness and verification:

- All non-timing JSON fields match exactly for 3,960 paired advancing frames
  across the three areas, including selected-light counts, shadow visibility,
  batching, draw submissions and resource command counts.
- 44 focused lighting/area/shadow tests and four hardware rendering tests pass,
  with no skips. The new reference test covers all four material passes, full
  cached streams, cached streams with visibility filtering, sparse visibility,
  suppression, disabled/dynamic rows, empty frames, stale light indices, current
  light changes and saturating totals. Invalid shadow pass counts are rejected.
- Twelve validation-enabled captures report zero Vulkan errors and warnings.
- Outdoor captures are pixel-identical. City captures differ in 30–57 pixels
  out of 2,073,600, max channel delta 31; max-city captures differ in 76–85
  pixels, max delta 27. Unchanged executable repeats also differ (city 20–27,
  max-city 47–70 pixels). Visual inspection found no broad scene/culling change,
  but exact city image equality and the source of those differences remain
  unverified. Do not describe all images as identical.
- Build succeeds without new warnings; clang-format and `git diff --check` pass.

Raw runs, scripts, profiler capture, XML tests, screenshots and the compact
summary are in `/tmp/rollnw-frame-cost`; they are temporary evidence, not tracked
artifacts. Preserved executables are `build/tools/mudl/mudl-before-frame-cost`
and `mudl-after-light-stats`. The current executable contains both changes.
The earlier high-overhead profile lost 30% of samples and is excluded; the
zero-loss 199 Hz capture is the hotspot reference.

Final self-check: the common path reads existing index arrays, no approximation
or persistent cache was added, and the new shadow field is frame-local. No new
buffer or pointer lookup is needed. Error cases and lifetime contracts are
explicit. The simplification removed repeated work rather than quality. Frame
cost is reduced on the populated max workload, but 7.57 ms is still substantial;
this does not establish a negligible 32x32 cost or full gameplay performance.
The remaining measured hotspots include shadow batch construction, static node
transform publication, animation sampling, and draw/descriptor submission.
