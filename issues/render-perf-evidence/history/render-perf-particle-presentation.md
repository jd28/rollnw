# Particle render preparation

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: open follow-up. The 48-byte GPU expansion trial establishes overview gains
but fails the declared small-view tail bounds; no trial implementation is retained.
Production baseline remains `34e296313` (documentation HEAD `6b03d8c96`).
[Parent](../../rendering-performance-overhaul.md) / [profile](render-perf-submission-profile.md).

## CPU atlas rectangles with GPU expansion, 2026-10-19 — not retained

Ordinary billboards use 48-byte rows instead of 80-byte CPU-expanded rows.
Original CPU sine/cosine and atlas rectangles preserve their arithmetic; the
vertex shader expands axes and colors. Other modes retain their existing path.
Cost: 32 fewer upload bytes/ordinary particle, repeated expansion at six GPU
vertices, another retained CPU vector, and 48 additional uniform bytes/draw
within the existing 256-byte alignment. No worker, dispatch, cache or name policy.

All 176 selected candidate tests pass. The final vertex-function readback checks
168 particle pairs / 1,008 vertex pairs / 12,096 scalar outputs exactly, including
3×2 atlases, rotations through 1e12, clamps, flips, blends and both frame slots.
Ten capture pairs across five views and two animation phases are byte-identical.
All 48,000 timed frame pairs preserve logical and physical counters except the
declared uniform-byte increase; no validation, allocation, binding or draw failures.

The original cohort stopped after 60 completed starts when `/tmp` filled.
Its files remain archived and supply no acceptance samples. A fresh whole cohort
uses disk-backed storage with the same frozen binaries, seed/order and gates:
16 pairs/view, 180 warmup + 600 measured frames, 160 starts / 96,000 frames.
Reference/candidate order is balanced and view order shuffled. Same Linux
Release/O3 Vulkan, Ryzen AI 9 HX370 / Radeon 890M, 1080p fixtures and fixed 16-ms
steps, main CPU2 and workers 2,0,1,3. Medians of 16 run statistics, milliseconds:

| View | Host median | Host p95 | Particle CPU median |
| --- | ---: | ---: | ---: |
| Overview32 | 2.206 → 2.109 | 2.932 → 2.893 | 0.346 → 0.273 |
| Overview64 | 7.914 → 7.564 | 8.980 → 8.784 | 1.743 → 1.452 |
| Overview96 | 18.579 → 17.772 | 21.263 → 20.794 | 3.912 → 3.254 |
| Local32 | 1.379 → 1.388 | 1.909 → 1.976 | 0.074 → 0.066 |
| Local96 | 4.980 → 4.911 | 6.213 → 6.202 | 0.757 → 0.774 |

Overview96 passes the target: median paired host change **−4.55%**, 15/16 pairs
faster; particle phase **−16.67%**, all 16 faster. Their one-sided upper bounds
are −2.94% and −16.05%. GPU scene median is 5.013 → 5.031 ms; its paired upper
bound is +1.06%. Every host-median, GPU-median and peak-RSS control passes.
Local96 particle time has no established saving: its median paired change is
−0.06%, upper bound +11.06%, with eight of sixteen pairs faster.

**The overall gate fails:** overview32/local32 host-p95 upper bounds are
**+6.68%/+6.85%**, exceeding the declared +5% limit. Their median paired p95
changes are −0.61%/+1.34%; this is inconclusive tail non-regression, not proof
of a >5% slowdown. The predeclared bound is the 12th ordered effect of 16 pairs
(96.1594% one-sided coverage assuming independent/exchangeable pair effects).
No timing-based retry, selected-run removal or threshold relaxation follows.
The earlier 36-byte variant remains rejected.

Overview96 peak process RSS is 888,450 → 887,064 KiB; this includes startup and
is not total memory. Retained arena/vector inspection was conditional on timing
acceptance and was not run. Per-run p99/p99.9, process CPU, background activity
and all paired effects are archived; with 600 samples, nearest-rank p99.9 is the
run maximum. Overview96 host-work budget misses are 6→3 per 9,600 frames; the
other views have zero. These are not presented-gameplay or final-art measurements.

Seven source files, executable and 28 shaders restore exactly; all 175 restored
selected tests pass without skips. The indexer resumed. No production change
remains. [Evidence](../README.md) preserves both cohorts, source,
tests, captures, runtime hashes, decision and restoration. Required full-client
acceptance remains open; no further timing retry of this variant is selected.

## Measurement controls and GPU boundaries, 2026-10-19

Unchanged production, six starts per placement/view, 180 warmup + 600 measured
frames: 21,600 complete frames, exact logical/physical counters and three exact
capture pairs. Main CPU2; compare workers pinned to 2,0,1,3 against their original
full 24-CPU mask. Local32 is serial. Medians of six run medians/p95s, ms:

| View | Pinned host median / p95 | OS host median / p95 | Median paired OS change |
| --- | ---: | ---: | ---: |
| Local32 | 1.357 / 1.879 | 1.364 / 1.960 | +0.77% |
| Local96 | 4.824 / 5.850 | 5.087 / 6.046 | +4.37% |
| Overview96 | 18.656 / 21.600 | 18.269 / 20.642 | -1.58% |

OS placement slows local96 host median in all six pairs; its tick is slower in
all six pairs of both large views. Predeclared identical-code pairs still exceed
5%: local96 median variation reaches -8.55% pinned / -10.30% OS. Every view/layout
has at least one p95 pair outside 5%, including the unchanged serial control.
Removing forced main/worker sharing does not resolve repeatability or explain
earlier candidate failures. No rejected candidate was retimed or accepted.

The pending GPU boundary test now runs the actual vertex function in a compute
wrapper: 72 particle pairs / 432 vertex outputs / 5,184 float comparisons.
Rotations through ±1e6 and 1e12, zero/maximum atlas dimensions, frame 65535, flips,
blends and size clamps are covered. Only 126 U-coordinate values on 3×2 sheets
differ, maximum 5.96e-8. Removing CPU/GPU atlas insets reproduces the differences
at 1/3, 2/3, 3/3, isolating this division path. All other tested outputs match exactly.
This is a numerical mismatch, not proof of a visible regression; the prior five
fixed captures remain exact. Broader atlas visual acceptance remains open.

No corrective shader arithmetic or production change is retained. Seven source
files, executable and 28 shaders are restored exactly; 175 selected tests pass,
zero skips. Indexer resumed. [Evidence](../README.md) preserves
all runs, observations, boundary failures, sources/binaries and restoration.
Before accepting small gains, separate the effect from this observed baseline
variation; same-process attribution alone does not establish presented frames.

The subsequent [48-byte trial](#cpu-atlas-rectangles-with-gpu-expansion-2026-10-19--not-retained)
retains CPU atlas rectangles and eliminates all tested boundary differences.
It establishes overview gains but does not clear the small-view tail bounds.

## Material-copy removal, 2026-10-19 — rejected

Two-file candidate borrows only the texture/mesh name instead of copying a
120-byte material row and its three strings. The scene mesh pass reads no source
material for nonmesh packets. Release assembly confirms the removed copy paths;
no shader, layout, simulation, ordering, culling or error-policy change.

All 175 selected candidate tests pass, including mesh integration. Ten captures
across five views/two animation phases are byte-identical. Three alternating
180+600 fixed16-ms pairs/view give 9,000 exact logical **and physical** frame
counter pairs, with no validation/command failures. Same Linux Release/O3 Vulkan,
Ryzen AI 9 HX370 / Radeon 890M, 1080p, cameras/content/capacities/affinities.
Medians of run medians/p95s, milliseconds:

| View | Host median | Host p95 | Particle CPU phase median |
| --- | ---: | ---: | ---: |
| Overview32 | 2.049 → 2.232 | 2.891 → 3.326 | 0.342 → 0.345 |
| Overview64 | 7.850 → 7.765 | 8.900 → 9.184 | 1.766 → 1.681 |
| Overview96 | 18.445 → 18.812 | 21.566 → 21.830 | 4.036 → 3.832 |
| Local32 | 1.424 → 1.402 | 2.007 → 1.971 | 0.075 → 0.071 |
| Local96 | 5.043 → 4.948 | 6.081 → 6.329 | 0.754 → 0.764 |

Target local96 particle phase changes +1.29%, failing the required ≥5% gain;
only one of three particle and host pairs improves. Its median paired host
change is +1.32% despite the lower aggregate median. Overview32 host median/p95
rise 8.94%/15.04%, failing the 5% controls. Overview96 particle phase saves 5.06%
in aggregate, all three pairs faster, but host median rises. Local96 GPU scene
is 2.083 → 2.072 ms, process CPU including startup 11.113 → 11.148 s, peak RSS
860,744 → 861,512 KiB. These data do not isolate the source of variation.

Rejected without extension/retry; source and exact baseline executable restored,
with all 175 restored selected tests passing without skips.
The indexer is resumed. Disassembly, candidate and full measurements are preserved
in the [evidence index](../README.md). Fewer copied bytes alone
do not establish a useful complete-frame improvement. Full-client and other
hardware validation were not attempted after the failed gate.

## Three-path attribution, 2026-10-19

Fixed temporary diagnostic: original renderer, candidate layout with CPU
expansion, and candidate layout with GPU expansion alternate in one process.
Three label rotations, frozen/animated controls and three views produce 27,648
frames. Nine reference/probe image pairs are exact; logical work and upload/
uniform arithmetic pass. Four transition frames per 32-frame block are excluded.

GPU expansion versus the same infrastructure with CPU expansion saves a median
0.892/0.895 ms particle thread CPU in frozen/animated overview96, all 24 cycles
faster in each. Local96 saves only 0.0155/0.0116 ms, with 19/24 and 14/24 cycles
faster. Host deltas are -0.838/-1.070 ms overview96 and -0.0058/-0.1203 ms
local96. No stable local regression appears here; the previous separate-process
failure's cause remains unresolved. These instrumented measurements do not
overturn the rejection or justify a batch threshold.

Frozen local96 has 478 ordinary + 1,466 other instances; overview96 has
46,440 + 4,464. Active arena block capacity is 4 MiB in every mode; duplicated
renderers and diagnostic counters prevent production memory/latency claims.
Probe source/shaders are removed, rebuilt baseline matches the frozen executable,
and all 175 restored selected tests pass without skips. The indexer is resumed.
Full evidence and limits: [index](../README.md).

## Direct vertex preparation, 2026-10-19 — overview gain, local control failure

Ordinary billboards use compact rows in the existing arena and vertex shader;
CPU visibility/sorting, draw order, other particle modes and simulation remain
unchanged. The initial 32-byte GPU-rotation variant changes 2/0/5/20/20 pixels
across five views, maximum RGB difference one level. Keeping the original CPU
sine/cosine arithmetic in 36-byte rows eliminates every difference. All five
corrected captures are byte-identical; both candidates pass 173 selected tests
without skips. The boundary snippet was unrun during this trial; the later
[boundary check](#measurement-controls-and-gpu-boundaries-2026-10-19) exposes a
small non-power-of-two atlas-coordinate difference.

Three alternating pairs/view, 120 warmup + 300 fixed 16-ms frames, unchanged
Linux Release/O3 Vulkan / Ryzen AI 9 HX370 / Radeon 890M UMA, 1080p content,
cameras, capacities and affinities. Medians of run medians/p95s, ms:

| View | Host median | Host p95 | Particle CPU median |
| --- | ---: | ---: | ---: |
| Overview 32 | 2.309 → 2.234 | 2.864 → 2.891 | 0.352 → 0.240 |
| Overview 64 | 7.906 → 7.418 | 9.002 → 9.076 | 1.824 → 1.269 |
| Overview 96 | 18.872 → 17.265 | 21.337 → 21.310 | 4.045 → 2.858 |
| Local 32 | 1.373 → 1.396 | 1.918 → 1.791 | 0.076 → 0.065 |
| Local 96 | 5.036 → 5.349 | 5.894 → 6.596 | 0.823 → 0.901 |

Overview 96 improves host median 8.51% and particle CPU 29.35%, with all three
pairs faster in each. GPU scene median is 5.062 → 5.013 ms. Local 96 exceeds
both 5% controls: host median +6.21%, p95 +11.92%; only one host pair improves.
Its tick also rises 2.193 → 2.326 ms, while process CPU including startup falls
7.806 → 7.567 s. These observations do not isolate the regression's cause.
All 4,500 timed frame pairs match logical work, with zero validation/command
failures. Indexer suspension/resumption and other background loads are recorded.
The screen rejects this version; no six-pair extension or timing retry.

Cost: 36-byte gathered inputs instead of 80-byte expanded rows, 64 extra uniform
bytes/draw within existing 256-byte alignment, and repeated GPU arithmetic at six
vertices. No dispatch, intermediate output buffer, threshold or worker. Target
peak RSS is 838,932 → 838,436 KiB; retained arena capacity was not separately
measured. Source and exact baseline runtime/shaders are restored; all 173 restored
tests pass without skips. Full-client,
other hardware, further camera/time phases and boundary GPU checks remain open.

- [ ] The earlier large-local increase remains unexplained. Boundary checks and
  the separate 48-byte trial above are complete; its small-view tail bounds remain
  inconclusive. Do not infer a threshold from view names or promote either trial
  from its overview gain alone.

Evidence and restoration checks: [index](../README.md).

## Stretched-batch culling screen, 2026-10-18 — rejected

The candidate extends the existing indexed-batch frustum test to stretched
billboards before sorting, geometry construction and submission. Its bounds
enclose clamped size/blur and rotated fallback axes, including nonorthogonal
axes. Invalid bound inputs and derived overflow retain the batch. Cost: an
O(live batch size) position/size scan when wholly outside; the first intersecting
particle retains the batch. Packet scales are calculated once, with no allocation,
cache, new interface or simulation change. The overflow guard also affects ordinary
billboards; complete-overview controls measure that added work.

The live audit examines 2,289,408 generated stretched quads. All 953,920 rejected
quads are wholly outside a clip plane. Local 32 removes four batches and mean
158.72 billboards/frame; local 96 removes 36 and mean 1,428.48. Full overviews
remove nothing. All observed blur factors are 1; larger blur, rotation/fallback
edges and invalid/overflow bounds are covered by synthetic tests. All 151
particle/import/viewer tests pass; three visibility tests fail against the old
implementation. Ten candidate image pairs at two phases and five observation-only
audit pairs match exactly. All 9,000 timed frame pairs have the expected draw/
instance reduction and unchanged other logical work, with no command failures.

Three alternating pairs/view, 180 warmup + 600 fixed 16 ms steps, unchanged
Linux Release/O3 Vulkan / Ryzen AI 9 HX370 / Radeon 890M UMA, 1080p cameras,
content, capacities and main/worker affinity. Medians of run medians/p95s, ms:

| View | Host median | Host p95 | Particle CPU median |
| --- | ---: | ---: | ---: |
| Overview 32 | 2.219 → 2.325 | 2.929 → 3.185 | 0.351 → 0.354 |
| Overview 64 | 8.171 → 7.966 | 9.796 → 9.945 | 1.839 → 1.786 |
| Overview 96 | 18.328 → 18.564 | 20.634 → 21.624 | 3.997 → 3.964 |
| Local 32 | 1.377 → 1.405 | 1.838 → 1.996 | 0.076 → 0.071 |
| Local 96 | 4.792 → 4.921 | 5.917 → 6.306 | 0.740 → 0.747 |

The target local-96 particle phase changes +0.93%, with only one of three pairs
faster (paired median +4.05%); it fails the required ≥5% improvement and two wins.
Host p95 also exceeds the +5% limit in overview 32 (+8.75%), local 32 (+8.62%) and
local 96 (+6.58%). The screen stops here; no six-pair extension or timing retry.
Local-96 GPU scene median is 2.025 → 2.026 ms; whole-process CPU including startup
is 10.998 → 11.313 s, peak RSS 860,124 → 860,400 KiB. Browser/updater/desktop
activity remains in the per-run process records; the indexer was paused and resumed.
These results establish no saving, but do not isolate bounds cost from other
variation or prove culling cannot help. Audit/validation timings are excluded.

Production source and the exact frozen runtime/resources are restored; all 149
restored tests pass without skips. The candidate, tests, audit, frozen runtimes
and all attempts are preserved in the [evidence index](../README.md).
No size cutoff, camera restriction or full-client performance conclusion follows.

Evidence: `/home/josh/projects/rollnw-evidence/2026-10-18/stretched-cull.tar.gz`.
SHA-256: `b00efbb227d62817483ab7992099e7acf3bbcb9637d97f9b885e48a843ae6b41`.
All 653 payload files and the canonical copy are verified.

- [ ] Before another culling implementation, attribute the bounds scan and avoided
  presentation costs on identical live batches. This needs a bounded diagnostic;
  the draw reduction alone is insufficient evidence for a cache or a new pipeline.

## Projected size and image contribution, 2026-10-18

Temporary Tier 1 diagnostic, unchanged Linux Release/O3 Vulkan / Ryzen AI 9 HX370 /
Radeon 890M UMA / 1920×1080 fixtures. The five reference/no-drop pairs match exactly:
all images and 600 non-timing frame records. Each inventory uses 180 warmup + 120
fixed 16 ms steps. These instrumented timings are not performance evidence.

The probe projects the four actual billboard corners with the vertex shader's
view/projection arithmetic. Size is the maximum side of their screen-space AABB;
it includes transparent texels and does not measure depth visibility. Near-plane
crossings/nonfinite inputs retain the entire batch; none occurred in the measured
intervals. Histogram rows are interval bins, explicitly accumulated in analysis.
Means and percentages over the 120 measured frames:

| View | Submitted billboards/frame | Individual size ≤2 px | Individual size ≤4 px | Billboards in batches whose largest sprite is ≤2 px |
| --- | ---: | ---: | ---: | ---: |
| Overview 32 | 5,628.2 | 7.32% | 23.71% | 2.82% |
| Overview 64 | 22,512.9 | 24.60% | 74.22% | 10.09% |
| Overview 96 | 50,654.1 | 50.92% | 96.37% | 11.57% |
| Local 32 | 662.6 | 10.97% | 19.73% | 0% |
| Local 96 | 1,932.4 | 64.56% | 72.48% | 45.18% |

The full overview does submit substantial detail only a few pixels across.
Individual sprite size and whole-effect footprint are different constraints:
many small sprites share a larger batch. An individual two-pixel filter was not
tested; its 50.92% inventory share is not an established safe removal fraction.

At two sampled phases (180/300 warmup + one fixed step), dropping whole batches
with maximum sprite side ≤2 px removes 5,820/50,796 and 5,751/50,004 overview-96
billboards (11.46%/11.50%). It changes 140/155 pixels, with maximum RGB difference
43/53 out of 255. Dropping batches whose entire union bounds are ≤2 px removes
4,500/4,347 billboards (8.86%/8.69%), changing 129/141 pixels with the same maximum
differences. The first-phase all-billboards-off control changes 15,279 pixels
(0.737%), maximum difference 138. Crops show concentrated point highlights as well as
larger fountain/effect detail; small changed-pixel area does not imply low contrast.
The predeclared negligible-difference limit is ≤0.01% changed pixels, maximum
channel error ≤2 and unchanged alpha. It is a diagnostic criterion, not a shipped
quality policy. No nonempty overview-96 size cutoff meets that limit at both
phases; the half-pixel whole-bounds test removes nothing. No hard visibility
cutoff is selected. Removing these highlights could be an intentional quality
tradeoff, but its CPU benefit has not been measured.

The fixed grid covers both phases in all five views: 36 removal comparisons and
34 size cases skipped only because their exact snapshot contains no eligible
batch. All 15 observation-only image pairs and 610 non-timing sample pairs match
exactly. Every removal has matching logged draw/instance reductions and unchanged
non-particle work, with zero validation/command failures. No cutoff meets both
the declared ≥10% overview-96 removal requirement and negligible differences
across all views/phases. The original NumPy-import and decimal-filename harness
failures, corrected scripts and validated-result reuse are preserved in evidence.

### Local camera: unsupported frustum-culling mode

`particle_billboards_visible` handles ordinary/local-Z/world-Z/aligned-world-Z
billboards and conservatively returns visible for other modes, including stretched.
In the 120-frame local-96 inventory, all 36 stretched batches (mean 1,428.6
billboards/frame) have every projected quad wholly outside the viewport. Local-32
has the same condition in four batches (158.7 billboards/frame). The full overviews
have no offscreen billboards. This is a local-view unnecessary-submission finding,
not an explanation of the full-overview cost.
The local-96 two-pixel maximum-sprite cutoff removes 880/1,945 and 858/1,904
billboards (45.24%/45.06%) in the two captures, both byte-identical to reference.
This tests that subset's image contribution; an early conservative bound may
retain additional offscreen sprites and needs its own cost/correctness comparison.

- [x] Test conservative bounds for stretched batches before CPU presentation work.
  The screen above passes correctness but fails the declared latency gates;
  the original implementation is restored.
- [ ] If an intentional overview-quality tradeoff is desired, evaluate a defined
  reduced-detail effect representation with camera motion and temporal comparisons.
  Tiny bright effects can remain visible. These captures do not prove a CPU ceiling,
  a need to restrict the camera, or that general mesh detail has the same distribution.

The probe is build-owned: contiguous existing 80-byte instance rows → four corner
projections → fixed histogram/batch bounds → buffered CSV, then optional late packet
removal. Cost is O(submitted billboards), eight matrix-vector multiplies per row,
with no per-particle allocation. It changes no simulation, sorting, resources,
non-particle drawing or public API. Simplification reused final draw data and
existing fixtures; no persistent LOD/cache/GPU pipeline was built. Production
source/exact frozen runtime are restored; all 149 particle/import/viewer-particle
tests pass without skips. Remaining limits include animation/camera motion beyond
the sampled phases, depth/texture contribution attribution and full-client latency.

Evidence: `/home/josh/projects/rollnw-evidence/2026-10-18/particle-projection.tar.gz`.
SHA-256: `c5bfe21c0ee29975c506337bbeda7ed2956cb5fef58e38fa516613f0d6a87f87`.
All 688 payload files and the canonical copy are verified (533,925,819 payload
bytes; 227,684,083 archive bytes). Includes the plan, original/probe source and
executables, projection CSVs, raw captures, difference crops, analysis scripts,
harness corrections, decision/self-check and restoration/tests. External game,
module and library dependencies are recorded; game assets are not copied.

## Compute trial, 2026-10-17 — large-view gain, control failure

Ordinary camera-facing billboards use one compute dispatch per view. Simulation,
visibility, sorting, resource selection and draw order remain on the CPU; other
particle modes retain their existing arithmetic. Preparation moves before the
render pass, preserving per-system mesh-first submission in both callers. CPU
particle timings include packing/upload/preparation; GPU totals include compute.

Three alternating pairs/view, 120 warmup + 300 measured frames, Linux Release/O3,
Ryzen AI 9 HX370 / Radeon 890M UMA, Vulkan, 1080p. Unchanged complete-overview/local
cameras, content, step, capacities and main/worker affinity. Medians of run medians:

| View | Host before → compute, ms | Particle CPU before → compute, ms | Host wins |
| --- | ---: | ---: | ---: |
| Overview 32 | 2.129 → 2.045 | 0.340 → 0.210 | 3/3 |
| Overview 64 | 7.767 → 7.301 | 1.750 → 1.173 | 3/3 |
| Overview 96 | 18.714 → 17.576 | 3.948 → 2.835 | 3/3 |
| Local 32 | 1.386 → 1.531 | 0.073 → 0.075 | 1/3 |
| Local 96 | 5.081 → 5.078 | 0.882 → 0.758 | 2/3 |

The 96 particle phase improves 28.20%; its host paired median improves 3.65%
(the ratio of run-median summaries improves 6.08%). GPU scene median is
5.087 → 4.666 ms, including compute. This clears the large-view screen gates.
Small-local host median rises 10.44% (+0.145 ms), exceeding the declared 5% limit.
The 0.094 ms tick increase and other phase changes mean this regression cannot
be assigned solely to dispatch overhead. All runs/background costs are preserved.
The screen rejects the always-compute version; no full matrix or retry follows.
All 13 changed source files and the exact frozen runtime/resources are restored;
235 restored particle/rendering tests pass without skips. The trial is archived.

236 selected particle/rendering tests pass, followed by 150 after final boundary
checks; no skips. GPU readback checks 776 output rows over four frames/two batches,
with exact centers/order, axis tolerance 2e-5 absolute + relative, UV 1e-6 and
color 2e-6. All five image pairs pass the predeclared <=0.01% changed-pixel / <=2
channel-level limits: 2/0/5/20/20 changed pixels, maximum difference one level.
All 4,500 timed frame pairs match logical work/draws; no validation, allocation,
bind, preparation or dropped-command failures. The extra dispatch/timer and
physical bindings/uploads are expected differences. The indexer was resumed.

### Trial data contract and cost

Internal layout v1: 32-byte particle rows (position/rotation, size, RGBA8,
16-bit frame + 16-bit packet index), 48-byte packet rows (tint/opacity,
atlas/image dimensions, flags), 48-byte camera/count constants. Each thread
writes the existing 80-byte billboard record. Draws index this shared output;
CPU vectors belong to the view, GPU spans to fenced per-flight arenas. Multiple
views append within the same frame. Flat draw rows use gfx handles only to name
arena resources; particle traversal uses indices, with no retained scene pointers.

Limits are 65,536 packet rows and 65,535×64 particles. Empty input does no dispatch;
overflow, short columns, nonfinite particle/camera values, allocation or dispatch
failure invalidate the view. Invalid GPU packet indices write zero without an
out-of-bounds read.
Atlas/frame and size clamps preserve the CPU rules. Shader failure rejects init.
The frozen 96 capture packs 46,332 particles / 1,224 packets: 1,541,376 uploaded
bytes plus 48 uniform bytes, and 3,706,560 output bytes, one dispatch/barrier.
Median process peak RSS rises 837,944 → 841,656 KiB; arena retained capacity was
not separately instrumented before the screen rejected the trial. No capacity
or cross-hardware memory claim is inferred from RSS.

The simplification pass batches dispatches, reuses the existing output/allocator,
and limits the transform to the common ordinary-billboard case. It adds no GPU
simulation, GPU sorting, camera limit, readback in production or public tuning.
Full plan, source/runtime snapshots, commands, failed fixture/harness checks,
readback/capture results and timing runs are preserved in the compute evidence.

Evidence: `/home/josh/projects/rollnw-evidence/2026-10-17/particle-compute.tar.gz`.
SHA-256: `3a7c443af2c4502677b854efa7e09d3ce484136bf8cc77ceb9d44cc633fdfed5`.
All 530 payload files verified (395,898,075 bytes; archive 76,191,861 bytes).
The bundle includes `candidate-source/`, the candidate executable/shaders,
the frozen baseline, all raw runs and restoration checks.

### Open follow-up

- [ ] Establish why the small-local control regressed before selecting a small-
  batch CPU fallback. Its captured ordinary batch is only 479 particles; a
  threshold must use measured visible work, not area dimensions or asset names.
  A hybrid pays another path/decision and must pass a newly declared comparison.
- [ ] Compare direct vertex preparation if useful: it removes dispatch/output
  storage/barrier but repeats arithmetic across six vertices. This compute trial
  does not establish its cost or prefer it without measurement.
- [ ] Full-client pacing, other hardware, retained arena capacity and long-lived
  numerical extremes remain unverified. These results do not prove a CPU ceiling,
  consistent 60 fps, or a need to restrict the camera.

## Profile and preceding CPU trial

Two post-transform-reuse 96-overview profiles contain 3,510/3,503 main-thread
user-cycle samples, zero lost. Particle rendering is 25.30/24.80% inclusive;
base-axis resolution is 2.65/2.64% self. Draw construction is 9.01/9.88% self,
surface conversion 4.57/3.69%, material validation 3.36/2.37%. Inclusive shares
overlap; worker CPU and off-CPU waits are excluded. These are not elapsed savings.

The five-view, 182-frame inventory found packet-constant base axes for 97.442%
of overview billboards and 98.032/99.336% of small/large local billboards.
Most ordinary billboard rotations vary, so rotation hoisting was not attempted.
The bounded trial computed common axes once per packet, retaining the existing
resolver for local-Z and position-dependent aligned-world-Z modes. Cost: one
24-byte stack value and a packet-constant branch, no retained memory or worker.

## Rejected screen, 2026-10-17

Linux Release/O3 Vulkan, Ryzen AI 9 HX370 / Radeon 890M UMA, 1080p. Three
alternating pairs/view, 120 warmup + 300 measured frames, unchanged cameras,
content, 16 ms step, production capacities and affinity. Medians of run medians:

| View | Host before → trial, ms | Particle phase before → trial, ms | Host wins |
| --- | ---: | ---: | ---: |
| Overview 32 | 2.004 → 2.098 | 0.332 → 0.337 | 1/3 |
| Overview 64 | 7.857 → 7.816 | 1.729 → 1.725 | 3/3 |
| Overview 96 | 17.863 → 17.598 | 3.866 → 3.878 | 2/3 |
| Local 32 | 1.354 → 1.348 | 0.074 → 0.073 | 2/3 |
| Local 96 | 4.861 → 5.118 | 0.743 → 0.746 | 0/3 |

The required >=3% 96 particle-phase gain fails; large-local host median rises
5.30%, exceeding its 5% control limit. The 96 host paired median is -1.74%, but
the target phase does not improve. No full six-pair matrix or retries followed.
Fewer helper calls did not establish the required latency benefit. No cause is
assigned to the unchanged phase or the local regression from these data alone.

12,076,042 live base-axis comparisons are byte-exact; all five capture pairs and
4,500 timed logical frame pairs match, with zero validation/submission failures.
149 particle/import/viewer-particle tests pass both before and after restoration,
with no skips. Committed source and the exact frozen reference executable are
restored. The desktop indexer was paused during timing and resumed in cleanup;
background/process costs and every run are retained. Camera/content are unchanged.

Evidence: `/home/josh/projects/rollnw-evidence/2026-10-17/particle-axes.tar.gz`.
SHA-256: `6cc35d502bcae2e6b7a5683ca217eb6b3982e085e8ed419c4a3037d2f8a0530d`.
Verified all 563 payload files (877,519,751 bytes; archive 151,951,268 bytes).
The bundle preserves plans, profiles, inventory/parity sources and executables,
reference/trial runtimes, all commands/results, restoration and the self-check.
