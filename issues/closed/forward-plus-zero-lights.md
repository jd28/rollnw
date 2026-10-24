# Exclude zero-color lights from Forward+ preparation

Status: closed, 2026-10-03. Implementation commit: `75a9a00f5`.

Cluster: [Rendering performance overhaul](../rendering-performance-overhaul.md).

Exact-zero filtering implemented and measured; focused checks and rendering
controls passed with the image limits below.

Tier 1. Frame this as one measured change: avoid clustering/uploading/shading
lights that cannot contribute because RGB is exactly zero. Limit: no light
selection redesign or new cache; reject the change if images differ or repeated
timings show a material regression. Plan B is to keep the baseline and investigate
the measured dominant phase with the existing profiler.

## Patterns & Conventions Found

- `lib/nw/render/forward_plus_renderer.cpp:430` prepares a batch from a borrowed
  `LocalLight` span, optionally selected by indices. `append_light` at line 495
  is shared by CPU clustering and GPU gather uploads. Reuse it.
- `tests/render_forward_plus.cpp:129,210` covers indexed selection and invalid
  lights; extend these existing fixtures and naming conventions.
- `tools/mudl/viewer_runtime.cpp` provides the fixed-step benchmark, per-frame
  host/GPU timings and screenshots added during the preceding baseline.

## Architecture Decision

Inputs: scene-local lights in a contiguous array, stable scene indices and a
per-frame candidate index sequence. Output: packed GPU light rows and cluster
indices valid for this frame. The original array and shadow slots are borrowed
and preserved; the frame owns its existing vectors and reuses their capacity.

Observed city data: 550 scene lights, 446 packed lights, but only five above the
existing 1e-4 colored-light diagnostic threshold. Scene/model inputs are stable;
colors/intensities can change with live lighting and animation, so evaluate the
predicate each frame. ASSUMPTION: most other city colors are exactly zero —
affects the number of eliminated lights; verify from the post-change counts.
The library accepts finite signed colors, even though viewer-imported colors
are clamped to [0,1]. Keep every nonzero value, including dim/subtractive lights.

Reject exactly (0,0,0), including signed zero, before either CPU bounds work or
GPU upload. Existing checks reject nonfinite fields, tiny/invalid radius and
intensity; invalid explicit indices are ignored. Do not substitute an epsilon
or a contribution-priority threshold for the exact-zero test.

## Component Design / Implementation Map

- Modify `lib/nw/render/forward_plus_renderer.cpp`: one early return in the
  existing append lambda, evaluated once per candidate. This operates on the
  current indexed batch; no singular transform or owning pointer is introduced.
- Modify `tests/render_forward_plus.cpp`: zero-only batch disables lighting,
  mixed/indexed input preserves packed shadow bindings and very dim/signed
  colors, and changing zero to nonzero becomes visible on the next frame.
- Create this issue and compact evidence after running the existing harness.

## Data Flow

Candidate indices -> exact-zero rejection -> existing depth/bounds filtering ->
packed light rows -> existing CPU cluster lists or GPU gather -> shaders. Keep
the existing source-count overflow decision for this experiment: changing GPU
backend selection at the same time would confound the measurement.

## Build Sequence

1. Preserve the current mudl executable, implement the predicate and tests.
2. Build mudl/tests; run the Forward+ suite and relevant render checks.
3. Alternate saved/current binaries across three advancing city runs and the
   other three baseline areas; compare frozen day/night captures and exercise
   explicit CPU/GPU gather configurations. Report host and GPU time separately.

## Critical Details / Simplification Pass

Platform: Ryzen AI 9 HX 370 / Radeon 890M, RADV Vulkan, Release, Tracy OFF.
The CPU and GPU overlap; pixel work is repeated across a 1920x1080 target.
Cost: three scalar comparisons and a branch per candidate on the CPU; no new
allocation, memory layout, synchronization or state. Access remains indexed
reads over the existing candidate order and linear writes to packed vectors.
The branch is expected to be mostly zero-color in this city; other workloads
are measured separately. Latency/throughput impact is unverified until A/B runs.

Simplification removes the work entirely for zero-color lights. No precomputed
table or cache is needed because the predicate is cheap and values can change.
No approximation or name-based policy. The common contributing-light path
continues through existing code; source ownership and lifetimes are unchanged.

Done: unit boundary/transition checks pass, selected before/after images match,
frame/counter evidence is complete, and measured results justify retention.
The existing rejected city/outdoor assets and lack of crowded gameplay coverage
remain limitations of the benchmark workload, as recorded in the baseline.

## Results — 2026-10-01

Retained the three-line exact-zero check in the shared `append_light` path. No
new allocation, public API, shader change or persistent state. The original
executable was saved before rebuilding, so both binaries include the same
benchmark instrumentation and preceding palette fixes.

On the same Ryzen/Radeon/RADV machine as the baseline: Release, Tracy OFF,
1920x1080, gameplay camera, 16 ms simulation step, 30 warmup + 240 measured
frames. Three paired repetitions per area; the second ran after/before to
reverse order. No build or other benchmark ran concurrently. All runs used
hardware GPU access outside the sandbox. Values are the median of the three
per-run percentiles, not percentiles of averages.

| Area | Packed lights before → after | Host p50 before → after (ms) | p50 reduction | Host p99 before → after (ms) |
|---|---:|---:|---:|---:|
| ms_4city | 446 → 5 | 3.039 → 2.551 | 16.0% | 5.214 → 3.963 |
| pvp_area_2 | 823 → 4 | 2.304 → 1.323 | 42.6% | 4.013 → 2.341 |
| pl_drowcity_005 | 109 → 23 | 1.805 → 0.756 | 58.1% | 2.751 → 1.591 |
| pl_icemount_007 | 124 → 0 | 1.538 → 0.458 | 70.2% | 2.436 → 0.852 |

Opaque GPU pass medians changed from 2.258 → 1.266 ms (city), 2.030 → 0.851 ms
(outdoor), 1.705 → 0.697 ms (cavern), and 1.408 → 0.409 ms (mountain).
These overlap host work and must not be added to host frame time. The city
before medians ranged 2.980–3.074 ms; after medians ranged 2.393–2.587 ms.
Every after median was below every before median for each tested area.

The removed rows confirm that the original small-color diagnostic was pointing
to exactly-black lights, not merely faint ones. This benefits the shared renderer
in gameplay as well as previews; it does not depend on editing an object.
Percentages describe these four environment workloads, not crowded gameplay.

## Correctness evidence and capture limitation

- Built mudl, rollnw-client (dependency) and rollnw_test. All 13 Forward+ tests
  and three viewer lighting/weather/animation checks passed (16 total).
- New tests cover zero-only batches, live zero/nonzero/zero changes, signed
  zero, tiny nonzero color, negative color, invalid color/index rejection, packed
  cluster indices, ambient classification and preserved shadow-slot payloads.
- 24 complete A/B reports contain 5,760 measured frames with finite nonnegative
  timings, no dropped draws and no failed animation samples.
- Day (11.25 seconds) and night (33.75 seconds) captures used both explicit CPU
  and GPU clustering at `64,8,1024`; the raised cap lets the city exercise GPU
  gather without the normal overflow-risk CPU fallback. Reported GPU mode was
  checked in each run. All 16 initial/control captures had Vulkan validation
  enabled and reported zero warnings and errors.
- Initial before/after screenshots differed at 29–68 of 2,073,600 pixels, with
  a maximum channel difference of 31/255. Repeating each identical binary/path
  differed at 16–78 pixels, also up to 31/255. Thus the original exact-equality
  criterion is unavailable for this capture fixture. Every initial A/B changed
  pixel lies in the union of locations that vary across repeated captures and
  CPU/GPU captures of the unchanged binaries. Combining those two controls is
  explicit: this is not proof of bit-exact output or a diagnosis of the source
  of variation. No broader lighting change was observed; exact image parity
  remains unverified. Retain the optimization with that limitation recorded.

Machine-readable frame times, timing distributions, counters and image/control
statistics are in `/tmp/rollnw-zero-lights/results.json`.
Full reports, logs, capture PNGs and the comparison/control scripts remain under
`/tmp/rollnw-zero-lights`. The baseline binary is
`build/tools/mudl/mudl-before-zero-lights` (an ignored build artifact).

## Final self-check

The measured workload, ownership, valid values and platform costs are documented.
The batch path adds one rejection predicate and removes all later light work
for exact-zero RGB. Stable indices, payload meaning and existing error handling
are preserved; no cache, approximation, speculative option or pointer-heavy path
was added. Formatting and diff checks passed. Unit, frame, CPU/GPU and validation
criteria passed; exact screenshot equality is explicitly limited by the measured
capture variation above. No unmeasured speedup is asserted.
