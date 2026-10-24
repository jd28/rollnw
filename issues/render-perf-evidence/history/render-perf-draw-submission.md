# RP-05: Draw submission

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: open. [Parent](../../rendering-performance-overhaul.md). Depends on
[RP-02](render-perf-submission-profile.md) and [RP-04](render-perf-uniform-descriptors.md).

## 2026-10-21 diverse-content comparison — rejected and source removed

The packed/indexed/indirect prototype is rejected. Seven glTF cases complete
21 correctness starts and 126 balanced timing starts: 14 exact RGBA comparisons,
294 matching capture rows and 50,400 matching timed sample pairs after declared
command deltas. All GPU scene samples are available. Source and runtime hashes
match before/after; no failed work or Vulkan validation messages.

The distinct-mesh target is faster in only one of six pairs. Textured, cutout and
mixed-blend cases are slower in all six. The textured control passes baseline
stability and records host median/p95 0.115823/0.147070 → 0.123312/0.156276 ms;
GPU median 0.098617 → 0.109288 ms. Its opaque CPU phase falls 0.010124 → 0.003867
ms, but the whole frame worsens. The other cases' variability is preserved in
the result, not treated as proven regressions. All four predeclared model gates
fail; the earlier area cohort remains inconclusive. No retry or new threshold.

The experiment's geometry ownership, indexed tables/shaders and indirect model
submission have been removed from source. The common benchmark, independent
buffer-pool cleanup and arena boundary guards/tests remain. The rebuild succeeds;
174 selected tests pass without skips. Textured glTF and area32 restoration images
match exactly, and all 42 non-timing sample pairs match without counter exceptions.
Both capture processes exit zero with complete GPU samples and no Vulkan validation
messages. These checks do not close the separate culling appearance issue.
Frozen evidence and full model/load/RSS results are under
`build/render-indirect-models-2026-10-21/`, especially model-comparison-result.md,
model-timing-results.json and retirement-plan.md. No new speedup is accepted.

## 2026-10-21 indexed draw data and indirect submission — unaccepted prototype

Historical checkpoint; this prototype is subsequently rejected and removed as
recorded above. The packed geometry experiment included flat 288-byte indexed draw rows,
20-byte indirect commands and ordered rigid opaque/cutout batches. Four thin
shader wrappers share the ordinary shading bodies. Optional device features and
storage/indirect limits are queried; unsupported capability or recoverable
command/table allocation failure uses the direct path. Existing transparent,
skinned, shadow, water and mesh-particle paths retain their draw behavior.

The final matrix completes 44 starts: original, packed direct, indexed direct
and indirect across eleven scenes. All 33 whole-RGBA comparisons are exact.
All 252 paired area samples match every non-timing field except the declared
binding/allocation/indirect-call counters, including per-pass checks. Sixteen
area reports have zero Vulkan errors/warnings; all 44 logs have no Vulkan
validation messages. The seven glTF controls use active IBL and fixed poses.
The final affected test cohort passes 156 tests without skips.

| View | Index binds, original → indirect | Resource binds | Indirect calls |
| --- | ---: | ---: | ---: |
| Local 32 | 2,245 → 184 | 2,276 → 1,510 | 64 |
| Local 96 | 2,245 → 184 | 2,308 → 1,542 | 64 |
| Overview 32 | 3,835 → 597 | 4,276 → 2,059 | 182 |
| Overview 96 | 13,127 → 3,965 | 17,140 → 14,027 | 502 |

Pipeline binds remain 40/40/21/85 respectively. An earlier capture exposed an
unnecessary metadata-only deformer exclusion that raised overview96 pipeline
binds to 815. Following the actual current rigid shader contract removes that
split; primitive-world deformer rejection is preserved and tested. The test
also covers distinct textures/materials, nonzero geometry/instance ranges,
repeated frame slots, invalid resource recovery and allocation-failure fallback.

Cost remains material: per-frame draw/command traffic, GPU table reads, an extra
storage binding, four shader variants, command arenas and a temporary CPU range
vector. Removed API calls are not a frame-time improvement by themselves.

The four-control area timing cohort completes 64 starts, each with 180 warm-up
and 600 measured frames. All 28,800 paired samples preserve non-timing data
except the declared command deltas. Medians of per-run medians/p95s, in ms:

| View | Host median, original → indirect | Host p95 | GPU scene median | Faster pairs |
| --- | ---: | ---: | ---: | ---: |
| Local 32 | 3.276 → 3.427 | 7.365 → 7.509 | 1.530 → 1.529 | 1/4 |
| Local 96 | 8.183 → 8.043 | 11.008 → 11.428 | 1.661 → 1.630 | 2/4 |
| Overview 32 | 3.534 → 3.352 | 5.662 → 5.732 | 0.911 → 0.908 | 3/4 |
| Overview 96 | 27.953 → 26.642 | 36.468 → 31.941 | 4.599 → 4.178 | 4/4 |

**Inconclusive; not accepted.** Point guards and the overview target pass, but
the predeclared baseline-stability gate fails. Baseline host-median max/min
spread is 51.10% local32, 55.32% overview32 and 16.49% overview96 (limit 10%);
overview p95 spreads also exceed their 15% limit. Local96 passes stability.
Overview96 median paired change is -6.47% versus original and -4.50% versus
packed direct, all four pairs faster for both comparisons. Local32 is slower
in three of four original pairs and all four packed-direct pairs; do not hide
that result behind the large-view gain. No timing-based retry was performed.

Overview96 opaque CPU median is 2.414 → 1.854 ms; local32 is 0.202 → 0.108 ms.
These phase observations do not override whole-frame uncertainty. Overview96
load median is 2.388 → 2.457 s and peak RSS is 887,996 → 886,220 KiB; local32
load is 0.516 → 0.568 s and RSS is 520,916 → 519,258 KiB. GPU allocation and
edit costs remain separate outstanding measurements. Timings are offscreen
host/GPU work, not presented gameplay FPS.

The indexed-direct diagnostic rereads its mapped command buffer on the CPU,
while the allocation requests sequential-write access. Its large CPU cost
without a comparable GPU increase is confounded by those reads; it is not an
isolated shader-table-cost result. Actual memory properties and a cached-command
control need checking before causal attribution. The indirect path does not
reread commands, and its original/packed-direct comparisons remain usable with
the stated noise limits.

The runtime contract is source-independent and works with distinct glTF meshes
and materials. The existing glTF shared-mesh control also exposes 64 uploaded
copies of one referenced mesh; preserving geometry identity separately from
placement remains an offline-compiler direction, not an implemented dedup fix.

At this checkpoint acceptance still required reliable whole-frame/GPU timing and tails, full GPU allocation
and edit cost, and diverse-content steady-state timing beyond fixed glTF images.
Texture diversity, animated skinning and transparency scaling are independent
dimensions. Existing color-culling appearance and full-client acceptance remain
open. This prototype does not establish DA:I/Ryse capacity. Its later source
removal and outstanding runtime-restoration checks are recorded above.

The source-diverse measurement gap now has a concrete tool:
`mudl model-benchmark <resref|path>` emits versioned fixed-step frame samples using
the common ViewerSession, with explicit GPU availability and separate load/IBL
setup times. All 22 CLI tests pass. Ten runtime checks verify textured/animated/
distinct-mesh glTF, exact helmet and area output, unchanged area counters, and
failure on missing requested inputs/resources or failed output writes. A final
version-field/output check also passes. The subsequent matched glTF timing
comparison is recorded above. Existing corpus process duration is not a frame-time baseline.

Evidence is staged in `build/render-indirect-models-2026-10-21/`:
`indexed-plan.md`, `indexed-review.md`, `indexed-final-tests.*`,
`indexed-final-checks.json`, `indexed-final-capture-summary.json`,
`indexed-final-captures/`, `indexed-timing-plan.md` and `indexed-timing/`.
`indexed-timing-results.json` preserves every run, paired change and gate.
Frozen runtime/source manifests identify all four controls; the temporary
RenderService control selection was restored byte-for-byte before the indirect
build. Final indirect mudl SHA256 is
`00abdfe61ffd7b339b85be3c625175d2874cf3d30857f227753f135564ee9daf`.

## 2026-10-21 packed geometry control — prototype in progress

Historical checkpoint; this control is also subsequently removed. It contained
per-model packed geometry and explicit primitive
index/base-vertex ranges, including the CPU selection/navigation readers. Four
layout buckets preserve static/skinned vertices and 16/32-bit local indices.
Source primitive order, shading, geometry and draw ordering are unchanged. The
model owns each packed buffer once; manually constructed primitives retain their
existing ownership. This is the direct-draw control for the indexed/indirect
experiment, not an accepted performance improvement.

Two capture cohorts each complete eleven baseline/control pairs: four NWN views,
three existing glTF fixtures, and four generated geometry/material controls.
Every whole-RGBA image matches exactly. In the final cohort all 84 paired area
rows preserve logical work and command counters except the four declared geometry
binding counters; validation reports have zero errors/warnings and no submission
failures. Last selected frame index-buffer binds change as follows:

| View | Original | Packed direct |
| --- | ---: | ---: |
| Local 32 / 96 | 2,245 | 184 |
| Overview 32 | 3,835 | 597 |
| Overview 96 | 13,127 | 3,965 |

Draw counts and uploaded constants remain unchanged. These are observed removed
bindings, not measured frame-time gains. The captured control executable SHA256
is `c10ca63b95c8d546743ef1398136ed9047b4793a4df1017bd1ef8630dc672c96`.
The full-install test cohort passes 141 tests without skips before the separate
buffer-pool exhaustion regression. The initial dedicated-server asset setup's
failure/four skips and initial test-build failure are preserved, not accepted.

Inspection and a failing regression exposed a pre-existing buffer leak when
Vulkan allocation succeeds but buffer-handle insertion fails. The working tree
adds cleanup on that failure branch. Its verification is recorded separately
from the successful-path image control. Existing fatal Vulkan allocation/map
failure policy is unchanged; no actual GPU-memory exhaustion is induced.
The final build passes all 142 affected tests with zero failures/skips. The
exhaustion regression changes from a measured leaked allocation/Vulkan buffer
to unchanged VMA allocation count/bytes with clean validation. No image cohort
was repeated for this failure-only branch; the captured control is identified
above. Current mudl SHA256 is
`b86d4df7ba92ad9fb1df35938f740761e30553fa5d659b15a125d99e7ae86e0b`.

Cost: one linear load-time packing/copy pass, eight bytes of range metadata per
primitive, and at most eight owned buffer handles per model. Frame submission
adds no geometry copies or new allocator. The next indexed table introduces GPU
reads and command/payload storage; whole-frame median/tails, memory and load/edit
costs still need measurement. Geometry deduplication, indexed draw shaders and
indirect model batching are not implemented by this control.

The offline compiler direction is a shared runtime data contract: geometry
identity separate from placement, explicit material rows and instance transforms,
and bounded draw ranges. The importer already produces common ModelAsset data.
Do not freeze a package format or add a global allocator before the runtime
experiment establishes its costs. Unique geometry/materials, texture diversity,
animated skinning and transparency remain independent workload dimensions;
NWN repetition alone cannot establish DA:I/Ryse capacity.

Open implementation questions: draw-table binding and feature enablement,
batch boundaries and fallback on capacity failure, and frame-slot lifetime.
Existing transparency/skinning/deformer paths must preserve output. SIMD or
parallel preparation requires an observed remaining CPU batch; the rejected
recording-only parallel trial is not evidence against parallelism generally.

Plan, source/runtime snapshots, capture checks and test logs are staged under
`build/render-indirect-models-2026-10-21/`. The prototype remains uncommitted and
unaccepted pending the complete measured experiment; no production restoration
is claimed for the current working tree.

## 2026-10-20 geometry and material diversity inventory

Twenty-two diagnostic starts complete eleven reference/probe pairs with exact
whole images. All 84 paired area samples preserve counts/commands; selected
frames have no submission failures. Both source hooks and all 156 production
runtime files are restored exactly. No new frame-time result or shipping change.

Ordered compatible groups, assuming packed geometry and indexed draw payloads:

| Workload | Eligible rigid opaque/cutout draws | Per-model packing | Cross-model packing |
| --- | ---: | ---: | ---: |
| Local 32 / 96 | 820 | 66 | 14 |
| Overview 32 | 2,347 | 192 | 47 |
| Overview 96 | 3,243 | 576 | 367 |
| 64 distinct meshes and material factors | 64 | 1 | 1 |

These are structural bounds preserving order, not implemented call reductions or
speedups. Per-model packing captures 92.7–93.7% of the possible call reduction
in these NWN views. Test asset-owned geometry ranges and flat indexed draw data
with CPU-produced indirect commands first; measure GPU fetch/upload, memory,
load/edit and whole-frame costs before accepting. A cross-model allocator adds
ownership/retirement complexity for the remaining measured reduction.

This opportunity also exists with unique glTF geometry/materials. The shared-mesh
glTF control separately exposes 64 uploaded geometry copies from 64 references
to one source mesh: geometry identity is currently expanded during import/upload.
The offline compiler boundary should preserve geometry separately from placement.

The largest overview also has 10,440 blended model draws, 972 skinned draws and
376 deformer draws (overlapping categories). Those remain outside this first
opaque slice; it cannot establish final-art capacity or solve the whole workload.
The generated materials vary factors, not texture diversity. Existing PBR/skin
controls use active IBL and a fixed pose; they do not certify animated, lighting
or texture-bandwidth scaling. Raw evidence is indexed in
[performance evidence](../README.md).

## 2026-10-20 source-independent submission direction

The user wants headroom for substantially richer art and warns against optimizing
only repeated NWN assets. Treat the city scaling fixtures as measurements of
repetition, not evidence of final-art capacity. This is a bounded Tier 1 review;
no new submission implementation or speedup is claimed.

The common import boundary already exists: NWN, glTF and native inputs produce
`ModelAsset`. Preserve it. The present limitation is downstream:

- `model_asset.cpp::upload_model_asset_primitive` creates separate vertex/index
  buffers for each primitive, with validated 16/32-bit index selection.
- The uploaded-span overload of
  `model_renderer.cpp::render_prepared_render_model_surface_instances`
  still selects one primitive/material, constructs and allocates draw constants,
  binds that primitive's geometry/resources, then issues its instanced draw.
- Materials already contain bindless texture indices. The gfx API already has
  indexed indirect drawing, GPU draw counts and compute-to-graphics barriers.
  Neither facility removes the separate geometry/data bindings by itself.

The current [profile](render-perf-submission-profile.md#2026-10-17-post-transform-profile-and-rejected-particle-axes-trial)
finds draw construction at 9.01/9.88% self main-thread cycles and particle
rendering at 25.30/24.80% inclusive. Shares overlap and exclude worker/off-CPU time;
they are not additive removable costs. Existing unchanged-code controls also
show greater than 5% p95 variation, limiting conclusions about small tail changes.

The next structural experiment to investigate is compatible shared geometry
ranges plus indexed draw/material/instance data, initially for rigid opaque/cutout
surfaces. Keep exact geometry, shading and ordering; preserve transparent,
skinned and deforming behavior through the existing paths while evaluating that
bounded slice. First compare CPU-produced indirect commands against direct draws;
only then can GPU visibility be judged against the submission work it actually
removes. GPU visibility alone is not the proposed optimization.

Cost on this Ryzen/Radeon shared-memory platform: load-time packing, buffer-range
ownership and fenced retirement, persistent table storage, shader table reads,
changed-row uploads, and edit/reload invalidation. Further GPU visibility adds
bounds/command storage, dispatches and barriers. Counts and bytes must be measured
before choosing the layout; no additional manager/object hierarchy is implied.
Use plain structs, indexed arrays and explicit batch transforms. Invalid ranges
must reject the batch explicitly; capacity failure must not drop draws silently.

Before implementation, inventory actual compatible ranges and binding breaks,
including material overrides and winding. Verify repeated instances and distinct
meshes/materials independently, using the existing common native/glTF path as
well as NWN scenes. Vary geometry, skinning and transparent work separately;
synthetic scaling characterizes costs but does not certify target-art quality.
The direct-draw `draw_index_count` counter currently sums indices per API call
without multiplying by instance count; it cannot measure total geometry work
across an instancing change. Record instance-weighted geometry separately.
Require preserved images/activity, complete work, a measured whole-frame gain,
and stated memory/load/edit costs. Reject the experiment if added fetch/upload
work cancels the CPU saving or diverse-content controls regress.

The [offline compiler](../../offline-model-compiler.md) can eventually emit the validated
geometry ranges and common material/animation records directly. A frozen package
format is not needed to test this runtime boundary. Asset names must not select
batching, culling, lighting or quality policy.

## 2026-10-15 retained static root payloads

Retained exact root/inverse-transpose rows in the existing complete-static
surface batch cache. Rebuilds gather the 128-byte rows once; submission copies
them contiguously into the existing frame allocation. Static/tile generations
and explicit static runtime-state publication invalidate the same cache.
Animated, partial-view and primitive-world batches keep fresh construction.
No new cache lifetime, approximation, GPU allocation or thread ownership.

Six alternating pairs/view, 180 warm-up + 600 measured frames/run, complete
1080p overviews, fixed camera/time/step, production capacities and the preceding
CPU affinity. Medians of run medians/p95s, in milliseconds:

| View | Host median | Host p95 | Faster pairs |
| --- | ---: | ---: | ---: |
| 32 overview | 2.329 → 2.466 | 3.373 → 3.327 | 2/6 |
| 64 overview | 8.548 → 8.424 | 10.046 → 9.954 | 5/6 |
| 96 overview | 19.377 → 18.736 | 22.467 → 20.807 | 6/6 |
| 32 local | 1.440 → 1.437 | 2.123 → 2.183 | 3/6 |
| 96 local | 5.562 → 5.416 | 6.809 → 6.841 | 3/6 |

**Retained after the predeclared control follow-up.** Both large-view gates
pass; median paired changes are -3.17% / -2.67% at 64/96. The initial 32-overview
median rises 5.89%, exceeding its 5% limit. The single fixed follow-up uses two
byte-identical baselines and the unchanged candidate, all six execution orders,
180 warm-up + 1,200 measured frames/run. Baseline A/B/candidate median is
2.531 / 2.476 / 2.496 ms; p95 is 3.458 / 3.512 / 3.489 ms. Baselines agree within
5%, and the candidate is within 5% of both for both metrics. Every other initial
control/p95 gate passes. Background process costs are recorded; variation is
not attributed solely to desktop indexing. No additional timing retries.

The 96 opaque phase median falls 2.255 → 1.659 ms; transparent is
2.64957 → 2.64959 ms and GPU scene time is 5.156 → 5.133 ms. These are matched
phase measurements, not isolated removable-cost estimates. Process CPU time
including startup is 22.995 → 22.155 s/run; median peak RSS is
880,838 → 884,188 KiB. Standalone results do not close full-client acceptance.

Correctness: all 48 targeted tests pass without skips, including exact
fresh/retained images through nonuniform scale, rotation/shear, runtime root
publication, material/asset edits, visibility, clear/rebuild and incomplete
payload fallback. Real tile edits check rebuilt payloads; animated color/depth,
live movement and door tests also pass. All five production image pairs and
five diagnostic image pairs are exact. Non-timing fields match in all 18,000
main frame pairs and 7,200 follow-up triples, with no submission/validation
failures. Temporary diagnostics were removed and production source/executable
restored byte-for-byte before timing.

Live audit: 4,606,784 root/normal rows exactly match the original computation,
with one retained build per unchanged overview and none in either local view.
Retained capacity including additional vector headers is
274,816 / 1,099,264 / 2,473,344 bytes (0.262 / 1.048 / 2.359 MiB), before allocator
overhead, at 32/64/96. Diagnostic producer times are 0.101 / 0.348 / 0.869 ms
from one start each; these are not matched startup/edit latency claims.
Uploads still cost 128 bytes/row/frame. Explicit static publication may rebuild
unchanged rows; direct unannounced mutation remains outside the existing static
record contract. The simplification removes per-frame handle traversal and
inversion without adding a second invalidation mechanism.

Raw plans, source, tests, matrices, captures and matched runs are in
[external evidence](../README.md). The fresh profile is recorded
in [RP-02](render-perf-submission-profile.md). No CPU ceiling or camera restriction
is established by this bounded improvement.

## 2026-10-15 draw-bounds trial rejected; submission reuse inventory

Annotated draw-construction samples led to an exact signed-endpoint replacement
for the eight-corner world-bounds transform. It preserved floating-point grouping,
with the original corner path for nonfinite/overflow and signed-zero cases.
No cache, API or draw ordering change. The arithmetic reduction did not establish
a worthwhile frame-time improvement, so all runtime/test changes were reverted.

Six alternating pairs/view, 180 warm-up + 600 measured frames/run, complete 1080p
overviews, fixed camera/time/step, production capacities and the same CPU affinity
as the preceding trial. Medians of run medians/p95s, in milliseconds:

| View | Host median | Host p95 | Faster pairs |
| --- | ---: | ---: | ---: |
| 32 overview | 2.220 → 2.269 | 3.069 → 2.927 | 3/6 |
| 64 overview | 8.067 → 8.136 | 9.158 → 9.773 | 3/6 |
| 96 overview | 18.339 → 18.302 | 19.747 → 19.138 | 4/6 |
| 32 local | 1.429 → 1.394 | 1.724 → 1.616 | 5/6 |
| 96 local | 4.876 → 4.872 | 5.830 → 6.012 | 4/6 |

The required 64-overview median improvement failed. Median paired changes were
-0.43% / -0.74% at 64/96; these also show little gain. All control gates passed,
so the planned control-tail follow-up did not apply. Desktop file indexing was
active; process costs are recorded without attributing the variation to it or
discarding runs. These are standalone results, not full-client acceptance.

Correctness passed: 45 tests without skips, 131,216 reference bounds cases,
9,935,964 exact live bounds comparisons, five exact production image pairs and
18,000 exact non-timing frame pairs. The interval path handled 99.7% of overview
inputs. Five bounds-audit and five submission-inventory captures also match the
original exactly, with zero validation/submission failures. All 43 applicable
tests pass after restoring production; the frozen original executable is restored
byte-for-byte. No new profile is needed for this unchanged checkpoint.

Submission annotation locates substantial work in instance-root access and
inverse-transpose construction, as well as payload copies. A separate live
inventory counts the retained static batch inputs over 182 rendered frames:

| Complete overview | Root/normal rows per frame | Distinct instance roots | Changed roots |
| --- | ---: | ---: | ---: |
| 32 | 1,808 | 1,208 | 0 |
| 64 | 7,232 | 4,832 | 0 |
| 96 | 16,272 | 10,872 | 0 |

Both local views use fresh preparation: 48 root rows/frame from 40 distinct roots.
The next bounded candidate is retaining exact root/normal payloads with the
existing complete-static batch generations. Payload storage would cost
0.22 / 0.88 / 1.99 MiB before allocator overhead; uploads would still occur each
frame. Verify edit/reload/material/model/visibility invalidation before adding
that state. This inventory does not justify retaining animated primitive-world
payloads. No cache or speedup is claimed; diagnostic timings are excluded.
Plans, rejected source, profiles/annotations, raw comparisons and the inventory
are in [external evidence](../README.md).

## 2026-10-15 shared shadow preparation trial rejected

Directional and local shadows rebuilt the same candidate rows and frame surface
range metadata. The trial shared a frame-owned index batch between those passes,
kept standalone fallback preparation, and stopped collecting unused model handles
when frame payloads were supplied. It added no cross-frame cache or GPU work.
The simplification removed one candidate/range traversal, at the cost of keeping
the batch alive through both consumers and exposing its lifetime contract.

Six alternating pairs/view, 180 warm-up and 600 measured frames/run, matched
camera/time/step/capacities and CPU affinity. Medians of run medians/p95s, in ms:

| View | Host median | Host p95 | Faster pairs |
| --- | ---: | ---: | ---: |
| 32 overview | 2.047 → 2.021 | 2.832 → 2.786 | 5/6 |
| 64 overview | 7.940 → 7.826 | 8.994 → 9.057 | 5/6 |
| 96 overview | 18.354 → 17.999 | 20.254 → 19.406 | 6/6 |
| 32 local | 1.343 → 1.365 | 1.535 → 1.635 | 3/6 |
| 96 local | 4.701 → 4.677 | 5.469 → 5.668 | 3/6 |

The 96 overview median paired change was -2.01%; its local-shadow phase fell
1.195 → 1.005 ms and GPU scene time was 5.151 → 5.149 ms. All 18,000 frame pairs
have identical non-timing fields. Five image pairs are pixel-identical with zero
Vulkan warnings/errors, including active shadows in the local views. Nineteen
targeted tests passed without skips, including 84 exact scalar depth comparisons
through pose/material/visibility changes, malformed ranges, stale instances and
area edits. An additional run of the extended area test passed local-only,
both-disabled and directional-only transitions.

**Not retained:** small-local p95 rose 6.52%, failing the predeclared 5% limit.
One fixed follow-up used two byte-identical baselines and the unchanged candidate,
six runs each, all six execution orders, 180 warm-up and 1,200 measured frames.
All 7,200 frame triples match exactly. Baseline A/B/candidate median was
1.357 / 1.370 / 1.402 ms and p95 was 2.047 / 1.866 / 2.019 ms. The baselines
disagree by 9.67% at p95; the candidate exceeds the faster baseline by 8.20%.
This is inconclusive about a reproducible regression and does not clear the
acceptance gate. No further reruns or new tail-cause claim. Production source
and executable are restored to the committed culling checkpoint; the trial,
tests and all measurements remain in [external evidence](../README.md).
All 17 targeted tests pass again on the restored source.

The preceding profile still applies to the restored renderer: draw construction,
model submission and particle rendering remain larger targets. Shared preparation
has a measured modest overview benefit, but needs a convincing small-view result
before reconsideration. No full-client, GPU-culling or CPU-ceiling claim.

## 2026-10-14 exact shadow-culling simplification

Retained the CPU control before implementing GPU visibility. Actual overview
inventory at 32/64/96 found 78 compatible static groups and 1,423 shadow surfaces
at every size; eligible casters were 956/3,824/8,604 out of 1,404/5,616/12,636.
Both local views had 75 casters, 50 eligible, 29 groups and 428 surfaces. An
unfiltered fixed indirect command per compatible surface across seven views would visit 9,961
surface commands, including empty groups, with the current separate
geometry/uniform bindings. This is a submission cost estimate from actual counts,
not a GPU timing result. No GPU culler, indirect path, camera limit or LOD was added.

The existing culler projected eight AABB corners and divided by W for every
caster/view pair, although generated shadow matrices have constant W=1. The new
batch transform projects signed column intervals with GLM's original floating
point addition grouping. A finite enclosing batch bound can reject whole views.
Perspective, nonfinite and overflowing projections retain the original corner
predicate. Output masks preserve source order; malformed batch shapes reject
before writes. Candidate counts saturate, and an internal shape violation fails
loudly. The simplification removed corner transforms/divides and whole rejected
view scans; a chunk cache, GPU buffers, dispatches and barriers were unnecessary
for this control. New transient storage is 25 bytes/candidate (bounds plus mask),
315,900 bytes at 96, with sequential access and existing frame-local ownership.

Six alternating pairs/view, 180 warm-up and 600 measured frames/run, identical
camera/time/step/capacities, main CPU 2 and particle workers 2/0/1/3. Values are
medians of run medians/p95s in milliseconds; all 18,000 frame pairs have identical
non-timing counters. Every overview pair improved; all predeclared median/p95
control gates passed. The earlier three-pair screen's small-local regression
remains in the evidence; it was not reproduced by the longer planned cohort.

| View | Host median | Host p95 | Faster pairs |
| --- | ---: | ---: | ---: |
| 32 overview | 2.393 → 2.221 | 3.395 → 2.986 | 6/6 |
| 64 overview | 9.158 → 8.422 | 10.263 → 9.507 | 6/6 |
| 96 overview | 21.283 → 19.087 | 23.662 → 22.038 | 6/6 |
| 32 local | 1.395 → 1.400 | 1.777 → 1.640 | 3/6 |
| 96 local | 4.904 → 4.804 | 5.724 → 5.752 | 4/6 |

At 96, directional shadow CPU was 1.633 → 0.846 ms and local shadow CPU was
2.221 → 1.405 ms. GPU scene median was 5.296 → 5.240 ms; whole-process CPU time
(including loading) was 23.798 → 22.457 s and peak RSS 880,832 → 881,098 KiB.
The 32 local median changed by +0.005 ms and its p95 decreased; 96 local p95
increased by 0.028 ms. No full-client latency or hardware ceiling claim.

Verification: 12 targeted tests passed, including over a million reference
visibility decisions, rounded clip boundaries, perspective/mixed-W and malformed
inputs, exact animated shadow depth through material/visibility/shader changes,
and camera/tile-edit integration. Five production image pairs are exact with
zero Vulkan warnings/errors. Local captures submit 145 directional and 24 local
caster/view pairs, so the image gate includes active shadows. A separate live
audit matched all 25,232,844 caster/view decisions across 1,820 calls and five
additional exact image pairs. The production source and measured executable were
restored exactly afterward; audit timings are excluded. Detailed raw
inventory, screen/final cohorts, diagnostic audits, source and hashes are in
[external evidence](../README.md).

A fresh 20-second 199 Hz main-thread user-cycle profile has 3,535 samples, zero
lost, and a complete 3,500-frame workload; profile timings are excluded from the
latency comparison. The complete bounds culler is 2.53% inclusive (interval test
1.95% self), candidate construction 5.30% self, draw-record construction 12.54%
self, model submission 26.34% inclusive and particle rendering 23.68% inclusive.
These shares overlap and exclude workers/off-CPU time. Current evidence favors
removing repeated preparation and improving submission before GPU culling alone.
GPU migration remains open: solve shared draw-data/binding cost, then measure
resident visibility/indirect output against this simpler baseline; GPU edge
parity, invalidation, capacity and readback verification are still required.

## 2026-10-14 parallel recording and GPU shadow review

After particle threading (`57c38888b`), [fresh profiles](render-perf-submission-profile.md#2026-10-14-after-particle-threading)
still show substantial model preparation/submission and particle rendering.
Taskflow is retained with the user-accepted small-local p95 tradeoff. This review
adds no runtime change or speedup claim; detailed design and profiles are in
[external evidence](../README.md).

Main model submission can use coarse ordered secondary command buffers, recorded
by the existing workers and executed in pass/source order. First establish the
serial-secondary control, then enable parallel recording. The current backend
has one primary command buffer/frame, shared uniform/descriptor ring cursors and
stats, shared bone/instance arenas, and resource pools whose vectors can grow.
Each recording lane needs its own command pool/list/binding cache and reserved
upload ranges; resource creation, target transitions, frame fences and queue
submission remain serialized. A lane belongs to one task at a time, regardless
of which Taskflow worker runs it. No per-draw mutex or new pool per phase.

Vulkan 1.3 dynamic rendering requires secondary inheritance and secondary-contents
mode. Current inline particle/debug/transparent work needs ordered secondary
tails or another explicitly correct pass design; splitting a pass must not clear
depth or alter blending. Query/counter reduction and cache invalidation need
tests. Costs include per-frame command pools, scratch capacity, repeated binds,
partition slack and scheduling/secondary-execution overhead. Existing 32 MiB
uniform/2 MiB descriptor capacities must not silently multiply by worker count.
[Khronos recording guidance](https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html).

The user also steered shadow visibility toward GPU execution. Compute, indirect
draw/count, indirect first-instance and compute-to-draw barriers already exist;
the missing transform is GPU bounds → visible instances/counts → shadow draws.
Keep visibility on the GPU, using completed readback only for diagnostics. Reuse
area bounds/cache generations, compatible shadow groups and instanced shaders;
static rows persist, dynamic bounds/poses update each frame. Current primitives
have separate geometry bindings and uniforms, so indirect commands alone cannot
combine arbitrary models. Inventory compatible static groups before selecting
that first slice; preserve unsupported dynamic/skinned cases explicitly.

The fixed 96 overview tests 12,636 casters against three directional views
(37,908 rejections) and four local views (50,544 tests, 30 surviving pairs).
This is a reason to investigate culling/candidate construction, not to assume
3.50 ms of shadow elapsed time is removable. Compare existing chunk bounds as a
conservative broad phase before adding a new spatial structure. A GPU path costs
bounds/instance/index/command buffers, changed-row uploads, compute/barriers and
invalidation machinery on the shared-memory GPU. Test fixed per-group commands
with zero instance counts before adding command compaction when sufficient.
[Khronos GPU culling/indirect example](https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html).

Open implementation questions: supported static/dynamic volume, balanced recording
partitions, allocation reservation failure, indirect maximum-range validation,
CPU/GPU edge/near-plane parity, edit/reload invalidation, and logical count
reconciliation when API draw counts change. Required evidence includes active
shadow-visible and moving-caster views (the overview has no directional survivors),
GPU visibility readbacks, exact images, host race checks/Vulkan validation, and
matched complete 32/64/96 overview plus local median/p95 and GPU-time comparisons.
Neither route establishes a CPU ceiling or requires camera/content limits.

Baseline max-city issues 9,989 draws. Find actual batch breaks by pass,
material, geometry and visibility; tile count/model names are not compatibility.

Provisional [rigid animated batches](render-perf-rigid-instances.md) reuse whole-run
groups: max 7,947 draws, +274,304 transient row bytes; city removes 364 draws,
outdoor zero. Latest max host 6.227 -> 6.025 ms; opaque CPU 1.250 -> 0.960 ms.
The three edge pixels are resolved by the shadow correction below; inconsistent
tails remain unresolved. Prepared
world/normal rows have explicit transform-space/lifetime/limits; skinned,
deforming and transparent surfaces retain scalar handling.

[Static packing audit](../README.md): overview
2,134 eligible static draws have 1,012 material/shadow groups within shared models;
plaza 772/438, outdoor 468/187. Packing remains an unselected candidate. Same-node/
transform grouping saves nothing; any future experiment must preserve identity,
edits, animation and shadows, and justify its cost against agreed frame budgets.
These are structural bounds, not implemented savings or measured speedups.

Provisional [rigid shadow batches](render-perf-rigid-shadows.md) remove another
1,177 max / 329 city / zero outdoor draws. Max now 6,770 draws; latest matched
host 5.567 -> 5.397 ms, shadow CPU 1.172 -> 0.927 ms. Adds 162,176 max / 48,512
city transient world-row bytes; no new shader or retained cache. Exact scalar
depth tests and actual-scene comparisons pass; tails remain unresolved.

Counting cleanup (baseline `420d2cde2`): use the validator's existing material
counts; remove two duplicate counter fields and two counting traversals (frame
rows and edit/rebuild cache rows). Net -12 production lines; internal
`ViewerFrameStats` consumers now read `prepared_model_surface_material_bindings.materials`.
Max has 2,639 frame rows / 26,384 cached rows. Isolated count/validation medians:
1.953 / 17.739 microseconds; outdoor 0.100 / 0.311. Validation remains intact.
Three alternating 180/1,200-frame pairs per scene: host medians max
5.027 -> 5.070 ms, city 1.794 -> 1.713, outdoor 0.933 -> 0.922; mixed pair results,
no whole-frame speedup established. 208 tests, 10,800 timed logical frame pairs,
2,160 capture frame pairs and all six edit scenarios preserve reported counts.
Outdoor captures were exact. The max-city red-channel step (43 -> 42) at
(1407,635) is resolved with the shadow correction below: six new captures using
the hash-verified original before/after binaries and corrected shader are
byte-exact, including repeats. All 1,080 frame pairs preserve non-timing fields;
the pixel is (43,30,24,255) throughout. No additional counting change was needed.
Raw plan, commands, hashes, profiles and checks live outside Git in
`/home/josh/projects/rollnw-evidence/2026-10-06/validation-audit.tar.gz`.

Rejected direct pipeline arguments (baseline `d677fd0ea`): removed the temporary
key and observed stack packing, but legacy low-angle city host median regressed
in all three pairs (5.127 -> 5.177 ms overall). Source restored. 208 tests and
10,800 logical timing frame pairs passed; no general claim about argument APIs.
Raw trial: external `2026-10-06/pipeline-args.tar.gz`. Reassess priorities using
the corrected overview/player views in RP-01 before more submission trials.

## Shadow sampling review, 2026-10-08

The existing shader candidate resolves each nonuniform texture once before
its box/tent loop. Production-profile SPIR-V (`ps_6_0`, Vulkan 1.3, `-O3`)
retains `NonUniform` on all 22 shadow fetches in NWN, PBR and water consumers;
the original retains it on only three. A GPU test with alternating fragments
and two constant-depth maps fails before (2,048 incorrect pixels) and passes
after (zero). All 655 selected regressions pass without skips. No batch layout,
allocation, binding, filter kernel or error-policy change; C++ probes removed.

Three alternating pairs/view, 180 warmup + 1,200 frames, Release/Tracy off,
1920×1080 standalone, Ryzen AI 9 HX 370 / Radeon 890M, Mesa 26.2.2. Same clean
executable, original versus corrected shader. Medians of run statistics, ms:

| View | Host median | Host p95 | GPU scene median |
| --- | ---: | ---: | ---: |
| Full-area overview | 4.496 → 4.233 | 5.664 → 6.758 | 1.614 → 1.612 |
| Elevated plaza | 2.411 → 2.323 | 3.429 → 3.444 | 1.503 → 1.688 |
| Legacy city edge diagnostic | 1.780 → 1.839 | 3.247 → 3.413 | 1.655 → 1.768 |
| Elevated outdoor | 0.576 → 0.595 | 0.887 → 1.054 | 0.538 → 0.554 |

14,400 logical frame pairs preserve every non-timing sample field; reported
upload/staging counts and bytes also match. Correctness requires the fix;
this is not a speedup claim. GPU player-view costs increase; host/tail results
are mixed. 24 separate corrected-shader captures are byte-exact between
batched and split draws across these four views, with zero repeat variability.
Full-client pacing, activity and total memory remain unverified in this pass.
Commands, hashes, binaries, compiler output and captures:
[external evidence](../README.md).

## 2026-10-12 filtered-run fusion rejected

Fresh overview profile places indexed grouping/submission at 20.18% self cycles.
A one-function trial formed source-instance runs during index filtering, retaining
checks/order but removing the second traversal. Six alternating pairs/view
(21,600 frames) regress overview host median in every pair: 12.728 → 12.921 ms
(+1.52%), p95 +2.20%. Small-local p95 +7.13%; large-local median -1.40% does not
satisfy the overview gate. 108 tests and three exact captures pass; all counters
match. Restore source/tests/executable; no optimization retained. Per-row run-end
writes, allocation order and filtered-input reserved capacity also changed; their
individual costs are unisolated. [Full evidence](../README.md).

## 2026-10-13 model-partitioned group search

The complete 64×64 overview checks 415,746 group candidates per frame;
403,648 immediately reject a different shared model. A call-local Abseil lookup
now selects that model's group indices in creation order. Exact space/surface
compatibility, global group order and fallback behavior remain unchanged.
Separate probes verify 415,746 → 12,098 checks, with identical compatibility
counts and group/run distributions. No cross-frame cache or camera restriction.

Six alternating pairs/view, 180 warmup + 600 frames, Release 1080p on the same
Ryzen/Radeon, main thread pinned to CPU 2. Medians of per-run statistics:

| View | Host median ms | Host p95 ms | GPU scene median ms |
| --- | ---: | ---: | ---: |
| 32×32 local | 1.364 → 1.363 | 2.028 → 1.983 | 1.319 → 1.315 |
| 96×96 local | 7.336 → 7.415 | 8.774 → 8.774 | 2.410 → 2.385 |
| 64×64 overview | 12.908 → 12.704 | 14.378 → 14.210 | 3.090 → 3.092 |

Retained: overview median -1.58%, improving in five of six pairs; p95 -1.17%.
Tradeoff: large-local median +1.07%, slower in all six pairs; its cause is not
isolated. This passes the predeclared 5% control tolerance, not a universal gain.
The largest lookup has 4,560 bytes of hash-slot/index capacity, plus container,
control-byte and allocator overhead; adds hashing and small per-model vectors.
No retained storage; existing 32-byte group and 208-byte surface layouts remain.
Shared model pointers are identity keys only; placement indices cannot identify
shared assets. Group/run/surface traversal continues to use indices.

109 tests pass, including mixed-asset/material/space/winding GPU parity. Three
scene captures are byte-exact; all 21,600 measured frames preserve counts and
per-pass counters without failures. Uninstrumented binary restored byte-for-byte.
[Evidence](../README.md) includes probes, storage inventory and
all pairs. Full-client/moving-gameplay acceptance and a CPU ceiling remain open.

## 2026-10-14 validated surface sort reuse

Baseline `3f1ddff90`. The [fresh attribution](render-perf-submission-profile.md#2026-10-14-draw-preparation-attribution)
measures 2.145 ms sorting 32,688 dynamic surface rows at 96×96. The first trial,
sorting compact keys every frame then permuting 208-byte payloads, is rejected:
64 is slower in five of six pairs; 96 median paired gain is only 0.708%. Its
aggregate-median ratio suggests 4.0%, overstating the paired result. Preserve
all 36,000 frames, 154 passing tests and five exact captures from that trial.

The retained version compares every fresh incoming key against the preceding
input batch, reusing sorted indices only when count and all ten fields match.
Changed keys rebuild the permutation. Every surface payload is still fresh;
transforms, bounds, skin tables and instance generations are never retained.
The owning draw list keeps three contiguous arrays: 40-byte keys, 4-byte sorted
indices and 4-byte working permutation per row. No hashes or dirty-event
subscriptions. Empty/single/over-uint32 batches invalidate metadata and use the
original direct sort; allocation failures propagate without pairing stale order
with changed keys. Clear retains capacity, consistent with existing frame arrays.

Six alternating pairs/view, 180 warmup + 600 measured frames, main CPU2 after
load, Release/Tracy off, 1920×1080, Ryzen AI 9 HX 370 / Radeon 890M. Both binaries
use production 32 MiB uniform capacity. Medians of per-start statistics, ms;
paired percentage is the median of the six pairwise median changes:

| View | Host median | Host p95 | Median paired change |
| --- | ---: | ---: | ---: |
| 32×32 overview | 2.327 → 2.190 | 2.614 → 2.451 | −6.42% |
| 64×64 overview | 10.090 → 9.293 | 10.822 → 9.969 | −7.95% |
| 96×96 overview | 23.525 → 21.961 | 24.525 → 23.348 | −7.09% |
| 32×32 local | 1.395 → 1.435 | 1.552 → 1.588 | +3.01% |
| 96×96 local | 6.953 → 6.997 | 7.686 → 7.770 | +0.20% |

Every overview pair improves. Both large overviews pass the predeclared ≥3%
paired-gain and ≥5/6 improving-pairs gates. Local aggregate median/p95 increases
are +2.88/+2.32% and +0.63/+1.09%, within the 5% tolerance; these tradeoffs remain
explicit. GPU scene medians are 0.910→0.906 / 2.960→2.945 / 5.093→5.063 ms for
the overviews and 1.356→1.395 / 2.222→2.226 ms for the local controls.
155 renderer/tile-edit tests pass without skips, including every-key invalidation,
input reorder, count changes and fresh payloads across clear/reuse. Five image
pairs are exact with zero validation errors/warnings. All 36,000 timed frames
preserve every non-timing field and complete all submissions. Existing unresolved
tail gates from earlier changes are not closed by this scoped result.

A separate diagnostic observes exactly one rebuild followed by 779 reuses in
each fixed 32/64/96 overview and 96-local replay. Retained array capacities are
**174,336 / 697,344 / 1,569,024 / 6,384 bytes**, excluding vector headers and
allocator overhead; 96 adds about **1.50 MiB**. Capacity persists across clear.
Single first-sort samples are 0.215 / 1.315 ms at 32/64 and 2.205–2.927 ms in
three 96 starts; they are not edit-latency percentiles or a cold-path speedup claim.
The three-pair 96 diagnostic reports surface-sort means 2.531→0.656 ms, while
draw construction remains 2.488→2.513 ms. Its 1,800 frame pairs preserve counters
and all exclusive ledgers reconcile. These diagnostics overlap the unrelated
updater and cannot substitute for the clean standalone performance cohort.
Temporary probes/replay hooks are removed; restored mudl, client and test binary
hashes exactly match their pre-probe production builds. [Full-client limits](render-perf-gameplay-acceptance.md)
and [all raw evidence](../README.md) remain explicit.

## 2026-10-13 retained complete-view static groups

Provisional checkpoint: retain the existing opaque/cutout index/run/group result
when every static record is visible. The area owns indices only; full rebuilds,
clear and tile generations invalidate it. Hidden, suppressed, stale-handle and
partial-camera views use fresh preparation. Dynamic rows, per-frame GPU uploads,
ordered allocation fallback, materials and draw ordering retain existing behavior.
No camera, content, quality or production buffer-capacity change.

Six alternating pairs/view, 180 warmup + 600 measured frames, Release/Tracy off,
1920×1080, Ryzen AI 9 HX 370 / Radeon 890M; main thread pinned to CPU2 after load.
Identical 64 MiB diagnostic uniform capacity in both variants makes 96×96 complete.
Medians of per-start statistics, ms:

| View | Host median | Host p95 | GPU scene median |
| --- | ---: | ---: | ---: |
| 32×32 overview | 2.752 → 2.370 | 4.139 → 3.400 | 0.971 → 0.912 |
| 64×64 overview | 12.697 → 10.268 | 14.788 → 12.013 | 3.090 → 3.061 |
| 96×96 overview | 29.578 → 24.340 | 32.383 → 27.601 | 5.124 → 5.117 |
| 32×32 local | 1.337 → 1.358 | 1.693 → 1.891 | 1.297 → 1.318 |
| 96×96 local | 7.315 → 7.292 | 8.870 → 9.137 | 2.327 → 2.314 |

Overview medians improve 13.86% / 19.13% / 17.71%, in all six pairs at each size.
Opaque CPU median drops 3.304 → 1.107 ms at 64×64 and 7.320 → 2.113 ms at 96×96.
151 renderer/tile-edit tests pass without skips; five scene captures are exact,
and all 36,000 timed frames preserve non-timing fields and complete submissions.

**Final acceptance remains open.** Initial small-local p95 fails the predeclared
5% control tolerance (+11.69%). One fixed follow-up uses six orders of two
byte-identical baseline controls and the candidate, 180/1,200 frames each.
Candidate p95 is 1.813 ms versus 1.875 / 1.978 ms; medians are 1.330 versus
1.342 / 1.331 ms. The increase does not repeat, but identical baseline p95 differs
5.19%, failing the follow-up stability gate. All 21,600 follow-up frames preserve
non-timing fields. Retain both cohorts: the original cause is unisolated, and the
strict tail gate is inconclusive. Keep this reversible, repeatable overview win
as a checkpoint; do not describe all performance gates as passed.

Retained capacity at 32/64/96 is 790,200 / 3,160,392 / 7,119,256 bytes, including
vector/container capacity but excluding allocator overhead. Active payload is
156,920 / 615,368 / 1,379,448 bytes; existing preparation reserves remain.
A separate probe rebuilds once over 240 unchanged frames/size. First preparation
at 64/96 costs 1.635 / 4.217 ms on CPU2 (one sample each, not an edit-latency
percentile). No retained model/GPU ownership. Full-client pacing/activity,
production 96×96 capacity policy, aggressive CPU targets and a CPU ceiling remain
unverified. [Raw evidence](../README.md).

- [ ] Resolve small-local tail variability before final performance acceptance;
  the candidate is faster than both duplicate controls in the fixed follow-up,
  but their 5.19% p95 spread exceeds the requested measurement tolerance.

- [x] Evaluate existing instancing first; choose packing/indirect submission
  only from measured benefit and RP-04's binding constraints.
- [ ] Audit repeated eligibility/material/geometry validation between grouping
  and submission. Establish the prepared-data lifetime, validate at its real
  boundary, and measure removed work. Keep allocation-failure handling and
  checks for data that can actually change; Vulkan validation covers API use.
- [ ] For a new protocol/store, define batch ranges, ownership, limits, failure
  and fallback behavior before implementation (Tier 2).
- [ ] Preserve geometry, visibility, transparency order, skinning and shadows;
  verify camera movement and edits. Lower draw count alone is not success.
- [ ] Compare CPU/GPU/whole frames, memory and edit/load costs; retain only a win.

Cost: possible geometry/command buffers, GPU indirection and rebuild/upload work.
Done: measured improvement with parity, or a recorded no-change decision.

## 2026-10-16 bounded parallel command-recording trial (Tier 2)

### Patterns & Conventions Found

- `model_renderer.cpp:418,1002` combines serial uploads with graphics binds/draws;
  `model_gpu_backend.hpp:109` pipelines are already initialized, const lookups.
- `preview_scene.cpp:124` owns the existing Taskflow executor; reuse it after
  particle updates join. No new worker pool or external engine conventions.
- `vulkan_internal.hpp:190` command state is local, but descriptor cursors belong
  to the context. `vulkan_context.cpp:1290` owns attachment transitions and clears.
- Existing Vulkan tests, matched mudl overview/local harness, snake_case APIs,
  explicit failure counters and clang-format remain the conventions.

### Architecture Decision

Serial preparation produces contiguous indexed-draw packets; four ordered chunks
record into separate secondary command buffers, using 1/2/4 CPU recording lanes.
All modes use the same four chunks. Remaining water/transparent/particles/debug
commands occupy one serial secondary tail, in the same dynamic render pass.
Primary transitions, resource creation, uploads, query allocation and queue
submission stay serial. Khronos Vulkan 1.3 secondary-contents rendering requires
this tail: no inline commands are valid inside that primary rendering scope.

The platform is Linux Release/O3 on Ryzen AI 9 HX 370 / Radeon 890M UMA, Vulkan
1.3, two frames in flight. CPU frame latency is the target, not draw throughput.
Current complete-96 profile puts 23.15% inclusive cycles in model submission;
this experiment addresses only its bind/descriptor/record portion. It cannot
claim to parallelize all model preparation.

### Component Design

- Batch protocol: fixed-layout `IndexedDrawCommand` rows contain pipeline/buffer
  indices (existing generation handles), uploaded uniform/storage offsets and
  sizes, strides and draw counts. The session owns retained contiguous scratch.
  Resources and mapped uploads remain alive through the frame fence; workers
  borrow immutable rows only until join. Uniform mapped pointers are unused by
  recording; the existing span API is retained to avoid duplicate span types.
- Four exclusive command lists own independent pools, bind caches and descriptor
  cursors. Reserve exact aligned descriptor demand serially from the existing
  2 MiB ring; no multiplication of it or the 32 MiB uniform ring. A fifth list
  records the ordered serial tail using the remaining shared cursor after join.
- Scene executor reuses Taskflow; 2 workers process two chunks apiece, 4 process
  one apiece, serial control runs all four on the calling thread. Lane identity,
  not scheduler thread identity, owns command pools. No per-draw locks.
- Invalid trial worker counts reject explicitly. Invalid batch setup/capacity
  fails loudly in the experiment; individual invalid resource rows retain the
  backend's drop/counter behavior. One trial main pass per frame is enforced.

### Implementation Map

Modify gfx.hpp, vulkan_internal.hpp, vulkan_context.cpp for flat draw batches,
secondary lifetime/descriptor ranges/statistics/query ownership; model render
context/renderer for serial packet preparation; preview_scene.hpp/.cpp for
existing-executor dispatch; session.hpp/.cpp for scratch and ordered integration;
render_gfx_resources.cpp for equivalence and rejection coverage. Trial controls
are temporary and will be removed if rejected; no new generic job system.

### Data Flow

Prepared model groups -> serial instance/bone/uniform uploads -> indexed draw
rows -> exact descriptor reservation and contiguous four-way partition ->
exclusive secondary recording -> join -> ordered primary execution -> serial
tail execution -> end rendering -> ordinary frame submission/fence reuse.

Real inputs are the frozen 32/64/96 complete overviews and 32/96 local views in
`build/perf-draw-recording-2026-10-16`; their baseline exports supply exact volumes.
Common rows are already-instanced opaque/cutout static models. Pipeline/buffer
identity is stable; uniforms and recording are rebuilt every frame. Existing
model validation rejects invalid assets before packet generation. This change
reads uploaded spans and pipeline metadata, writes command/descriptor bytes,
and avoids model traversal or resource-pool mutation on workers.

ASSUMPTION: four contiguous chunks contain enough recording work to amortize
scheduling — affects whether this trial survives, measured against serial chunks.
ASSUMPTION: equal draw counts balance descriptor/driver cost sufficiently —
affects partition policy; do not add weighted scheduling without measurements.

### Build Sequence

1. Freeze shipping executable and collect baseline before production edits.
2. Implement flat packets, serial secondary control, lifecycle/capacity tests.
3. Reuse executor for two/four lanes; validate exact images/counts and run matched
   alternating release timings. Archive rejected code and remove it if gates fail.

### Critical Details

Simplification: keep preparation/uploads serial (avoid per-worker arenas), reuse
executor and existing bind validators, reserve exact descriptor bytes (avoid
partition slack), preserve one pass (avoid extra attachment loads/stores), and
reuse pools across frame fences. No approximations or lookup caches introduced.
Hot access is linear draw rows plus indexed resource-table reads; valid-resource
branches are predictable. Handles are required by existing gfx lifetime checking;
no new pointer graph is introduced. Costs: per-frame five command pools, retained
packet scratch, extra traversal, chunk-boundary binds, scheduling/join and execute
commands. These are measured costs, not an asserted improvement.

Done gates declared before edits: exact rendered images and logical draw/instance/
index counts; no validation or allocation/drop failures; >=5% opaque improvement,
no complete-96 host median/p95 regression, and no >3% median regression in other
views. Physical bind/descriptor/cache counters can differ only for documented
chunk boundaries. Use one serial-secondary control and 2/4-worker candidates;
if all fail, keep shipping code and record the rejection. Race safety requires
exclusive pools/ranges, immutable resource pools while joined work runs, and no
shared allocation/query/counter mutation on workers.

Open questions tracked here: whether staging consumes the parallel savings;
whether scheduling or repeated bindings dominate; whether a broader preparation
split is worth its additional ownership cost after this narrower measurement.

Baseline observations (180 warmup + 600 measured frames/view, frozen shipping
executable SHA256 `b497f543f9550c97b64ff8f8d866d919d3a74721302f2ddf317332fbc8cf4b4f`):

| View | Opaque draws/frame | Opaque median ms | Descriptor peak bytes | Uniform peak bytes |
|---|---:|---:|---:|---:|
| overview 32 | 2507 | 0.550 | 465152 | 6347712 |
| overview 64 | 3287 | 1.057 | 964160 | 13096896 |
| overview 96 | 4587 | 1.583 | 1795840 | 24345536 |
| local 32 | 830 | 0.294 | 253232 | 3468992 |
| local 96 | 830 | 0.213 | 254256 | 3477184 |

Architecture clarification following the user's question: batch count belongs
at the caller, not in the permanent gfx protocol. The trial backend has bounded
capacity but accepts a batch span. A retained implementation should move shared
Taskflow ownership from PreviewScene to RenderService; the experiment reuses the
current owner to isolate recording changes before paying that migration cost.
The intended durable boundary is immutable prepared draws plus exclusive
recording-memory ranges, permitting later parallel constant/instance filling
without making model traversal or shared allocators implicitly thread-safe.

### Screen result: reject recording-only staging

114 renderer/prepared-draw tests passed with NWN_ROOT configured. The extended
culling test exercises serial/two/four-worker recording across material lighting,
mirrors, projection orientation and repeated frame slots; empty-batch and
capacity/scope rejection tests also pass. The first test attempt had an incorrect
expectation that the index counter multiplies by instance count; corrected the
expectation to its existing per-draw semantics. A subsequent asset-dependent
preview failure was resolved by configuring the existing game install path.

All 15 capture pairs (five views x three variants) are pixel-identical with clean
validation reports. All logical counters and upload bytes match. Physical counter
changes are precisely +4 descriptor-buffer binds, -5 descriptor-buffer bind skips,
+3 pipeline binds / -3 skips, and +19 storage descriptor encodes / -19 reuses in
overviews (+20/-20 in local views). No additional upload capacity, failures or drops.

Three alternating release rounds, 120 warmup + 300 measured frames per view/mode:
18,000 measured frames, 13,500 matched frame pairs. No tests/builds/captures ran
alongside timing. Same fixed camera, full overview coverage, resources, time and
step as baseline. Main CPU 2; existing four workers use CPUs 2/0/1/3 (four distinct
performance cores, verified with lscpu). Two-worker mode uses two tasks on that
existing executor, not a second pool. Validation capture timings are excluded.

| View | Original host ms | Serial secondary | Two workers | Four workers |
|---|---:|---:|---:|---:|
| overview 32 | 2.090 | 2.359 | 2.890 | 2.747 |
| overview 64 | 8.641 | 8.691 | 8.702 | 8.729 |
| overview 96 | 18.511 | 18.575 | 18.967 | 18.586 |
| local 32 | 1.415 | 1.915 | 1.473 | 1.548 |
| local 96 | 5.913 | 5.513 | 5.767 | 5.530 |

Values are medians of three run medians. Small-view/background timing variation
is substantial (including one original 32 overview run at 3.474 ms); do not treat
small differences or the local-96 result as isolated causal gains. All candidates
independently fail the predeclared large-view opaque-improvement gate:
96 opaque 1.594 -> 1.621 / 1.929 / 1.643 ms; host p95 20.482 -> 21.575 / 20.857 /
21.244 ms. No candidate merits the longer acceptance matrix. This rejects this
implementation/partition, not parallel rendering in general or a CPU ceiling.

A separate diagnostic build (not used for release speed claims), two 96-view runs
per mode, splits the opaque CPU path into serial preparation, reservation/setup,
recording including dispatch/join, and execution setup. Median of run medians:

| Phase ms | Serial secondary | Two workers | Four workers |
|---|---:|---:|---:|
| preparation/uploads | 1.018 | 1.041 | 1.036 |
| descriptor reservation/setup | 0.022 | 0.023 | 0.023 |
| recording including dispatch/join | 0.535 | 0.837 | 0.548 |
| execution setup | 0.004 | 0.004 | 0.004 |

These measurements locate about 1 ms before recording; they do not attribute it
all to uniforms. Source inspection also identifies a concrete lifetime mismatch:
`render_context.hpp:99` combines camera/lighting/fog/shadow fields with per-draw
model matrices in SceneConstants, uploaded for every model draw. SurfaceConstants
also mixes material fields with view exposure/IBL multipliers. A future layout
experiment should separate view constants, retained material rows and instance
rows before considering a wider parallel preparation path. Cost: shader/descriptor
layout changes and validation across static/skinned/shadow/material variants;
benefit remains unmeasured. Shared executor ownership should move to RenderService
only with a retained cross-scene workload, not as an unsupported performance claim.

An additional interval-only diagnostic confirms actual overlap: four-way mode
reached four concurrent batches in 268/300 measured frames (three in 31, two in
one); two-way mode reached two in 293/300. This rules out wholly serial dispatch.
Serial per-chunk medians were 0.167/0.155/0.110/0.121 ms; four-way medians were
0.440/0.265/0.220/0.297 ms. These are wall intervals, including preemption, from a
separately instrumented run; they do not identify driver locks, cache effects or
scheduler delay individually. The increased per-chunk wall cost is unresolved.
No claim that shared GPU memory or the driver is the cause is supported yet.

Final simplification/self-check: reject and remove the experimental source, APIs,
worker dispatch, tests and diagnostic controls rather than retaining unused
machinery. Shipping render sources are restored byte-for-byte from ecb7a14d1.
The experiment meets correctness checks but fails performance criteria. No camera
restriction, LOD policy, new dependency, or additional GPU ring capacity was added.
The remaining architecture questions are tracked in this issue: view/material/
instance data lifetime split, recording-cost inflation under concurrency, and
whether parallel preparation can amortize scheduling. ThreadSanitizer was not
run; ownership was reviewed and Vulkan threaded validation/images/tests were run.

Restoration verification: all 113 original tests pass; no source diff remains in
gfx/render or render_gfx_resources.cpp. Rebuilt shipping executable SHA256:
`c1f4ec968eb8608e1287326cff21b334cb55cc7322d66e2e1a60e2369de020ac` (current build
identity differs from the frozen reference; shipping source is unchanged).

Evidence archive:
`/home/josh/projects/rollnw-evidence/2026-10-16/draw-recording.tar.gz`, SHA256
`2a31e971beec2391aacc98b4f9454529e15b7a87fd6fd476a0e23c4a8cee49d5`.
Manifest verification streamed all 1,252 payload files (789,707,426 bytes;
170,173,006 compressed bytes). Includes trial/diagnostic patches and binaries,
fixed fixture/harness snapshot, schedules, command lines, affinity/background/RSS
records, raw frame exports, images, test logs and phase/interval diagnostics.
Linked game asset directories remain external and are not recursively copied.

Follow-up: the [shared view constants experiment](render-perf-view-constants.md)
passed its measured gates and is retained. It reuses the existing two uniform
bindings and reduces overview-96 host frame time from 18.421 to 17.821 ms in the
matched run set; parallel command recording remains rejected.
