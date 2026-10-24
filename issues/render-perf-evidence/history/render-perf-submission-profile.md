# RP-02: Submission profile

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: open. [Parent](../../rendering-performance-overhaul.md). Depends on
[RP-01 fixtures](../../render-perf-workload-contract.md); current fixtures are usable now.

## 2026-10-21 skin range lookup — isolated gain, frame screen inconclusive

The common skin-table batch scans every prior range for each new instance/skin.
The complete96 input has 972 distinct entries: 471,906 unsuccessful comparisons.
An indexed per-instance range chain reduces an isolated synthetic batch matching
its counts from 0.185405 to 0.045831 ms; a four-skin/64-bone stress case changes
3.083440 to 1.172060 ms. These are routine measurements, not final-art capacity.

Three alternating complete96 pairs record host median 21.833754 → 19.992724 ms,
all three faster; setup 5.539077 → 4.597954 ms. Target and small-local point guards
pass, but unchanged large-view median/p95 spreads are 15.15%/46.94%, beyond the
predeclared 10%/15% limits. The frame screen is inconclusive; no retry or extended
cohort. Process records include substantial concurrent file-indexer/browser CPU
work; this is observed interference, not isolated attribution of the variance.

Three images and all 3,600 timed counter pairs are exact. All 142 candidate tests
and an ASan/UBSan parity audit of 180,224 surfaces pass. Candidate source/runtime
remain in `build/render-skin-lookup-2026-10-21/`; the two source files and all 160
production runtime files are restored exactly, with 141 restored tests passing.
No implementation from this screen is retained. `result.md` preserves the measured
algorithmic opportunity, costs, failed stability gate and full verification.

## 2026-10-19 CPU-atlas GPU expansion comparison

[The 48-byte variant](render-perf-particle-presentation.md#cpu-atlas-rectangles-with-gpu-expansion-2026-10-19--not-retained)
preserves original CPU trig/atlas arithmetic and moves axis/color expansion to
the vertex shader. Sixteen pairs/view establish overview96 host median
18.579 → 17.772 ms (median paired −4.55%, 15/16 wins) and particle CPU
3.912 → 3.254 ms (−16.67%, all 16 wins). However, small overview/local host-p95
upper bounds +6.68%/+6.85% exceed the declared +5% guardrail; their median paired
effects are −0.61%/+1.34%, so this is inconclusive tail non-regression.
No candidate is promoted or retimed after the timing decision. The full 160-start cohort follows one
preserved storage-failure abort and uses no samples from it.

Ten images, 12,096 GPU output comparisons and 48,000 timed counter pairs pass
(only the declared uniform-byte delta). All 176 candidate tests pass; exact
production source/runtime/shaders restore with 175 tests passing. No new profile
or production speedup is claimed.

## 2026-10-19 unchanged-code measurement controls

[36 unchanged-code runs](render-perf-particle-presentation.md#measurement-controls-and-gpu-boundaries-2026-10-19)
retain identical work/images but fail 5% repeatability under both worker placements.
OS workers slow local96 host median in all six pairs (median paired +4.37%);
overview changes -1.58%. Even the serial control has p95 differences above5%.
Affinity alone does not explain prior failures; no candidate was retimed/accepted.
The GPU boundary follow-up finds 126 U-coordinate differences on 3×2 atlases,
maximum 5.96e-8, without establishing a visible regression. Production is restored
exactly and 175 selected tests pass. No new profile or speedup is claimed.

## 2026-10-19 material-copy screen

[Borrowing only texture/mesh names](render-perf-particle-presentation.md#material-copy-removal-2026-10-19--rejected)
removes confirmed Release string-copy paths but fails the three-pair screen.
Local96 particle phase changes 0.754 → 0.764 ms, with one of three particle/host
pairs faster; overview32 host median/p95 rise 8.94%/15.04%. Overview96 particle
phase saves 5.06%, but its host median rises. All 9,000 timed counter pairs and
ten images are exact; 175 candidate tests pass. Rejected without retry, source
and exact baseline executable restored, with all 175 restored tests passing.
No new profile or production gain claimed.

## 2026-10-19 particle attribution

[Three paths on identical scene data](render-perf-particle-presentation.md#three-path-attribution-2026-10-19)
separate candidate infrastructure from GPU expansion. Across 27,648 diagnostic
frames, GPU expansion saves about 0.90 ms overview96 particle thread CPU in all
24 frozen and 24 animated cycles. Local96 savings are only 0.012–0.015 ms and
less consistent; the earlier local slowdown is not reproduced or explained.
No threshold or production acceptance follows. Nine image pairs are exact;
probe removed, exact baseline restored, all 175 selected tests pass.

## 2026-10-19 direct vertex billboard screen

[Direct vertex preparation](render-perf-particle-presentation.md#direct-vertex-preparation-2026-10-19--overview-gain-local-control-failure)
with original CPU trigonometry gives exact images and 96 overview host median
18.872 → 17.265 ms / particle CPU 4.045 → 2.858 ms (three wins each). Large-local
host median/p95 rise 6.21%/11.92%, so the candidate is rejected and restored.
Its tick also rises; neither GPU overhead nor a useful hybrid threshold is
established. Preserve the exact-output rotation boundary while attributing the
local control. All 4,500 timed frame pairs match logical work; 173 tests pass.

## 2026-10-18 stretched-batch culling screen

[Conservative early bounds](render-perf-particle-presentation.md#stretched-batch-culling-screen-2026-10-18--rejected)
remove all 36 offscreen stretched batches / mean 1,428.48 billboards in local 96,
with ten exact candidate captures, five exact audit captures, 151 passing tests
and 953,920 rejected quads verified outside a clip plane. Full overviews remove
nothing. Across three alternating 180+600 pairs/view, local-96 particle CPU changes
0.740 → 0.747 ms (one of three wins), failing the required ≥5% gain/two wins.
Host median changes 4.792 → 4.921 ms; p95 exceeds the +5% control limit in three
views. All 9,000 timed frame pairs have exact expected work reductions.
The candidate is rejected without extension/retry; original source/exact runtime
are restored and all 149 restored tests pass.
The bounds scan versus avoided presentation cost still needs attribution; no new
profile, established culling saving or full-overview improvement is claimed.

## 2026-10-18 projected particle detail

[Projected-size inventory](render-perf-particle-presentation.md#projected-size-and-image-contribution-2026-10-18):
50.92% of complete-96 billboards are ≤2 px across, 96.37% ≤4 px, averaged over
120 fixed-step frames. Whole batches whose largest sprite is ≤2 px contain only
11.57% of billboards. Two removal phases drop 5,820/5,751 instances but change
140/155 pixels with maximum channel difference 43/53, above the predefined
negligible-difference limit. Small size alone does not establish invisible output.
Five observation-only image pairs and 600 non-timing frame pairs are exact.

The local-96 inventory also finds 36 stretched batches / 1,428.6 billboards per
frame entirely offscreen. This mode bypasses the existing batch frustum test;
conservative early bounds are tested in the rejected screen above. Full overviews
have no offscreen billboards. No timed gain or new profile is claimed; diagnostic
source/executable are removed, the exact baseline is restored and 149 tests pass.

## 2026-10-17 compute billboard screen

[The always-compute trial](render-perf-particle-presentation.md) reduces the
96-overview particle CPU phase 3.948 → 2.835 ms and host median 18.714 → 17.576 ms,
with all three pairs faster (median paired change -3.65%). GPU scene time,
including compute, is 5.087 → 4.666 ms. Five captures pass explicit pixel bounds
and all 4,500 timed frame pairs match logical work. The small-local host median
rises 10.44% (+0.145 ms), failing the predeclared 5% control limit. No full matrix
or retries; source/exact runtime are restored and 235 restored tests pass.
Large-batch GPU preparation improves this screen. The next experiment needs
attribution of the local regression and explicit small-batch cost/ownership gates.

## 2026-10-17 post-transform profile and rejected particle axes trial

Baseline `34e296313`: two complete-96 main-thread profiles, 3,510/3,503 user-cycle
samples with zero loss. Particle rendering is 25.30/24.80% inclusive; draw
construction 9.01/9.88% self, surface conversion 4.57/3.69%, material validation
3.36/2.37%. Worker CPU/off-CPU waits are excluded and inclusive shares overlap.

[Packet-constant particle base-axis hoisting](render-perf-particle-presentation.md)
fails its three-pair screen: 96 particle phase 3.866 → 3.878 ms (required >=3%
gain), large-local host 4.861 → 5.118 ms (+5.30%, limit 5%). No further runs or
retained implementation. 12.08 million exact axes, five exact capture pairs,
4,500 matching timed frame pairs and 149 candidate/restored tests pass. Source
and frozen reference executable are restored, so this profile remains current.
The follow-up issue records a GPU preparation experiment and its unresolved
input/ownership/numerical/cost questions; no GPU performance gain is claimed.

## 2026-10-16 exact draw-transform reuse retained

After shared view constants, two complete-96 profiles put draw construction at
12.62/13.29% self cycles, particle rendering at 22.09/22.53% inclusive and model
batch submission at 19.23/20.19% inclusive (6,680 samples, zero lost; shares
overlap and exclude worker CPU/off-CPU waits). An exact input audit found all
32,688 dynamic draw matrices/local bounds unchanged across 181 subsequent frames.

[Retaining only derived normal matrices and world bounds](render-perf-draw-transforms.md)
passes all declared gates: six paired 96-overview host medians 18.704 → 17.907 ms,
all six pairs improve, p95 22.080 → 20.653 ms; setup 4.825 → 4.305 ms. Active cache
payload is 5.49 MiB plus vector slack/header. 125 tests, five exact capture pairs
and 22,500 matching timed frame pairs pass. The small-local median/p95 tradeoff
is +0.78%/+3.45%, within the 5% limits. These profiles precede this retained change;
they are not a post-change attribution or proof of a CPU ceiling/60-fps gameplay.

## 2026-10-16 particle UV lookup rejected

The current profile puts sprite-frame rectangle calculation at 2.74% self cycles.
An inventory found 1x1, 2x2 and 4x4 sheets in these views, with only 9.8% of
overview particles using uniform-frame packets. Across 182 rendered frames, the
96 overview calculated 7,517,196 rectangles for 2,544,228 distinct packet/frame
combinations. The trial used a packet-local 16-entry table and validity mask;
frames >=16 used the original helper. Cost: 320 bytes of table scratch plus a
4-byte mask, initialization and lookup branches, no heap or cross-frame state.
It removed 66.2% of overview helper evaluations but did not establish a frame win.

Six alternating pairs/view, 180 warm-up + 600 measured frames, complete 1080p
overviews and unchanged camera/time/step/capacities/affinity. Medians of per-run
medians/p95s, milliseconds:

| View | Host median | Host p95 | Faster pairs |
| --- | ---: | ---: | ---: |
| 32 overview | 2.329 → 2.599 | 3.389 → 3.518 | 1/6 |
| 64 overview | 8.957 → 8.609 | 10.814 → 9.835 | 5/6 |
| 96 overview | 19.264 → 19.866 | 23.031 → 23.402 | 2/6 |
| 32 local | 1.412 → 1.393 | 2.149 → 1.921 | 4/6 |
| 96 local | 5.612 → 5.490 | 6.781 → 7.046 | 2/6 |

**Rejected:** the required 96-overview median/paired improvement and four-of-six
consistency gates fail; its median paired change is +0.99%. The 32-overview
median also exceeds its +5% limit. The control-only follow-up does not apply.
No extra retries. The 96 particle phase changes only 3.862 → 3.851 ms, while
whole-process CPU including startup changes 22.769 → 23.081 s/run. Background
costs and every run remain recorded; no single cause is assigned to variability
or to the lack of gain. Fewer helper calls are not equivalent to saved latency.

All 149 particle/import/viewer-particle tests pass without skips, as do another
149 after restoration. Live audit compares 12,077,952 UV rectangles exactly;
synthetic checks cover the full uint16 frame range in both directions across
zero/large/non-square sheets, row flips and null/invalid/real source images
(7,864,320 cases/run, repeated in five runs). All five production and ten
diagnostic image pairs are exact. All 18,000 measured frame pairs have identical
non-timing fields, with no validation/submission failures. Production source
matches the committed renderer and its frozen executable is restored exactly.

The retained matrix change is committed as `57ea71926`. Its profile below still
applies; no fresh profile is needed for this rejected trial. Model submission,
draw construction and particle rendering remain open targets. The inventory,
candidate, audit and raw timings are in [external evidence](../README.md).
No full-client, CPU-ceiling or camera-limit conclusion.

## 2026-10-15 current profile after retained root payloads

Latest runtime change: [retained static root/normal payloads](render-perf-draw-submission.md#2026-10-15-retained-static-root-payloads).
Matched 64/96 overview medians fall 8.548/19.377 → 8.424/18.736 ms; the 96 opaque
phase falls 2.255 → 1.659 ms. The initial 32-overview median gate fails (+5.89%),
then the single predeclared duplicate-baseline follow-up passes all gates.
48 tests, ten exact captures and 4.61 million live payload comparisons pass.
Additional retained capacity reaches 2.36 MiB including vector headers.

Fresh complete-96 main-thread profile: 3,526 user-cycle samples, zero lost,
199 Hz over 20 seconds after settling, same main/particle-worker affinity and
production capacities. All 3,500 workload frames complete with every chunk
visible and zero culled records. Current attribution:

| Work | Self cycles | Inclusive cycles |
| --- | ---: | ---: |
| Model batch submission | 2.31% | 23.15% |
| Particle renderer | 7.02% | 23.47% |
| Draw-record construction | 13.10% | 13.10% |
| Shadow candidate construction | 5.27% | 5.29% |
| Shadow culler | 0.31% | 2.98% |

Batch submission's self share was 6.12% in the preceding 3,535-sample profile;
the fresh result is consistent with removing root gathering/inversion. These
are sampled cycle shares, not matched elapsed savings. Inclusive rows overlap;
the main-thread profile excludes worker CPU and off-CPU waits. Profile timings
are excluded from acceptance. Scalar model surface submission remains 4.44%
self / 15.86% inclusive within the batch path; particle sprite-frame lookup is
2.74% self and particle sorting 2.00% self. These identify remaining work to
inspect, not established savings or justification for another cache.

Previous investigation: [exact draw-bounds trial and submission inventory](render-perf-draw-submission.md#2026-10-15-draw-bounds-trial-rejected-submission-reuse-inventory).
Bounds arithmetic passes exact output checks but does not establish a frame gain:
64 overview median 8.067 → 8.136 ms, 96 overview 18.339 → 18.302 ms. Trial rejected;
production restored. Submission annotation locates instance-root access/inversion
and payload copies. Across 182 rendered frames, retained overview batches rebuild
1,808 / 7,232 / 16,272 root/normal rows per frame with no changed root inputs.
That inventory led to the retained candidate above; the rejected bounds trial
made no runtime change and needed no new profile.

Previous investigation: [shared shadow preparation](render-perf-draw-submission.md#2026-10-15-shared-shadow-preparation-trial-rejected)
improves 96 overview median 18.354 → 17.999 ms, but fails the small-local p95
gate. A fixed duplicate-baseline follow-up is inconclusive (baseline p95s differ
9.67%). Trial rejected; production restored. No new profile was needed for that
restored checkpoint; the following profile describes that earlier renderer.

Previous runtime change: [exact shadow-culling simplification](render-perf-draw-submission.md#2026-10-14-exact-shadow-culling-simplification)
passes the matched five-view gate. A fresh 96 profile puts the complete culler at
2.53% inclusive; candidate construction is 5.30% self, draw-record construction
12.54% self, model submission 26.34% inclusive and particle rendering 23.68%
inclusive (3,535 samples, zero lost). GPU culling is not implemented; preparation
and submission remain larger measured targets. Profile timings are excluded
from latency comparisons.

Previous: [profiles after particle threading](#2026-10-14-after-particle-threading)
identify model submission, particle rendering, dynamic draw construction and
shadow candidate/bounds work. [Parallel-recording/GPU-shadow ownership review](render-perf-draw-submission.md#2026-10-14-parallel-recording-and-gpu-shadow-review)
records the next boundaries; no further runtime change or speedup is claimed.

Earlier preparation attribution, 2026-10-14: after retained static grouping and
32 MiB uniform capacity, complete 96×96 setup costs **6.516 ms**, including
**2.117 ms draw construction, 0.888 ms surface conversion and 2.145 ms sorting**.
These are instrumented elapsed means, not removable savings. Exact key audits
find unchanged incoming sort keys in all 779 subsequent frames at 32/64/96
overview and 96 local. [Method and limits below](#2026-10-14-draw-preparation-attribution);
[sort experiments](render-perf-draw-submission.md) test the removable work.

Earlier matched scaling, 2026-10-13: **full overviews at all three sizes** give
32/64/96 host medians **2.857 / 13.026 / 30.105 ms**. Nine times the surfaces and
particles takes 10.537× the frame time. The total is roughly linear, but surface
filtering/run construction/group selection grows **0.321 / 2.265 / 5.729 ms**:
17.861× for 9× the surfaces, with higher cost per surface in all six pairs.
Slower growth in instanced submission partly offsets it. [Matched breakdown and limits](#2026-10-13-matched-overview-scaling).
No runtime change retained; this identifies a scaling lead, not its algorithmic
or cache cause.

Earlier attribution, 2026-10-13: the complete 64×64 overview spends **5.419 ms
(36.87%) preparing surfaces**, split between filtering/group selection and
dynamic draw construction/sorting. Main model packing/submission costs 2.528 ms,
particle update 2.382 ms, particle rendering 1.961 ms and shadows 1.527 ms.
These are exclusive elapsed means from a reconciled 14.698 ms instrumented
frame, 99.58% on CPU. Two unmodified profiles (19,927 samples, zero lost) agree
with the operation ranking. [Method, breakdown and limits below](#2026-10-13-complete-overview-attribution).
No runtime change retained; measured cost is not established removable work.

Earlier profile follow-up, 2026-10-13: 1,181 local / 1,169 overview main-thread
samples, zero loss. Local particle ticking is 41.38% inclusive cycles, attachment
sync 11.58% self, presentation 10.29% self, scalar curves 7.14% self and billboard
visibility 6.94% self. Overview indexed submission is 22.23% self / 36.66%
inclusive; surface sorting is 6.63% self. Shares overlap and are not elapsed
savings. User steered the investigation toward [particle execution architecture](render-perf-particle-execution.md):
the first shared-storage probe now covers 40,032 real particles in 1,116 systems,
pooled into 10 update groups. Its isolated large-local update cost falls
0.88938 → 0.81124 ms (-8.79%, all six starts improve), below the predeclared 10%
material-gain gate. Exact state comparisons pass, but spawning, ownership and
render integration are excluded. Runtime restored; no frame-speedup claim.
[Result and remaining work](render-perf-particle-execution.md).

Latest runtime change, 2026-10-13: [partitioning group searches by shared model](render-perf-draw-submission.md)
removes 403,648 unrelated-model checks per 64×64 overview frame. Six matched
pairs give host median 12.908 → 12.704 ms (-1.58%), p95 -1.17%; 96×96 local
median +1.07% is an explicit tradeoff. 109 tests, exact captures and counters pass.
The profiles above follow this checkpoint. The small lookup does not establish
a CPU ceiling or justify retained compatibility caches.

Start at `model_renderer.cpp`, `viewer/preview_model_draws.cpp` and
`gfx/backends/vulkan/vulkan_context.cpp` under `lib/nw/` (first two under `render/`).

[Initial evidence](../README.md): shadow submission 6.64% self
cycles, main-pass grouping/submission 4.37%; complete scene GPU 3.59 ms.
Dynamic scans are now [bounded (RP-07)](../../closed/render-perf-shadow-surface-ranges.md).
New caller stacks/disassembly locate the driver hotspot under `vkGetDescriptorEXT`
(uniform and storage encoding). That capture lost 56.36% of samples: qualitative
attribution only, no percentages/timings. [Storage reuse experiment](../../closed/render-perf-storage-descriptors.md)
tests the removable work; grouping and constant-write attribution remain open.
Captured payloads: 9,817 model draws, eight shared scene rows, 162 surface rows.
[Pass/transform split](../../closed/render-perf-model-constants.md) reduced bytes but
regressed host time; its per-call cause remains unisolated.

[Main-pass attribution](render-perf-main-grouping.md): max grouping ~0.129 ms;
instanced/scalar submission ~0.510/0.587 ms. Dynamic opaque has 2,255 eligible
rigid surfaces but only 115 compatible primitive/material keys. Prioritize this
RP-05 experiment over retained compatibility groups. Probe overhead/variance
limits and complete-frame comparisons are recorded with the evidence.

Current-runtime [profile/constant-pose evidence](../README.md):
zero-loss, measured-frame main-thread samples put animation at 18.34% inclusive
cycles (truncated callers; not elapsed-time savings). RP-10 reuses 200 constant
max-scene poses; complete-frame acceptance remains open under concurrent load.

Priority correction: use [explicit overview/player views](../../render-perf-workload-contract.md),
not the legacy low-angle roof/emitter camera. Both originally issued 172 particle
draws despite retaining 1,024 versus 51 chunks. [RP-14](../../closed/render-perf-particle-culling.md)
reduces player particle draws to 20–21 and CPU submission 0.388 → 0.096 ms;
simulation is unchanged. Proceed to actual toolset/moving-gameplay acceptance.

[Full client](../README.md): overview host 5.457 ms,
UI 0.560 ms, viewport 4.630 ms; viewport tick/pre-render/render 0.851/0.520/3.145 ms.
Area preparation is only 0.040 ms. [Actor routes](../README.md)
are now measured. Remaining overview cost is concentrated in the viewport.

[Material packing audit](../README.md): current
overview has 2,507 opaque draws; 2,134 eligible static draws reduce to 1,012
same-material/shadow groups within shared models. Packing is an unselected
candidate; first establish a budget need that justifies its implementation and
maintenance. Frame-time benefit remains unmeasured; no packing implementation.

## 2026-10-14 after particle threading

Checkpoint `57c38888b`, same measured particle candidate binary, Ryzen AI 9 HX370 /
Radeon 890M, Release/Tracy off, complete fixed 32/64/96 overview cameras at 1080p.
Four 20-second main-thread `cycles:u` captures at 199 Hz: 3,980 / 3,496 / 3,579 /
3,585 samples; zero lost. 96 repeats once. Main CPU2, workers CPU2/0/1/3; worker
CPU and off-CPU waiting are excluded. Frame-loop ancestry covers 99.77–99.94%.
Profiled frame times are excluded from performance comparisons.

| Main-thread path | 32, % | 64, % | 96 first / repeat, % |
| --- | ---: | ---: | ---: |
| Model batch submission, inclusive | 33.97 | 27.84 | 23.76 / 23.49 |
| Particle rendering, inclusive | 13.60 | 21.11 | 20.16 / 20.33 |
| Draw-record construction, self | 5.51 | 11.59 | 11.15 / 11.15 |
| Shadow bounds tests, self | 8.72 | 11.82 | 9.89 / 9.75 |
| Shadow candidate construction, self | 0.50 | 2.08 | 5.68 / 5.52 |
| Vulkan driver, all self samples | 12.76 | 8.78 | 6.17 / 6.60 |

Inclusive shares overlap with driver work; these are sampled cycle shares, not
elapsed savings. Source audit locates mutable frame allocation/command state and
read-only model inputs. Existing unprofiled six-run candidate medians at 96 are
setup 4.487 ms, opaque 2.056, transparent 2.501, particle rendering 3.729, directional
shadow 1.593 and local shadow 1.910. Setup includes nested subphases; do not add
these to profile shares. Those timings come from the preceding Taskflow cohort,
not a fresh speed comparison. All 31,000 report frames across the four attribution
workloads remain complete. Raw profiles, commands, source audit, counts, hashes
and the explicit GPU-shadow follow-up are [archived](../README.md).

## 2026-10-14 draw preparation attribution

Baseline `3f1ddff90`, full overviews, fixed 16 ms simulation, 1920×1080,
Ryzen AI 9 HX 370 / Radeon 890M, Release/Tracy off, main CPU2 after load.
Three starts × original/disabled/enabled binaries × three sizes, 180 warmup +
600 measured frames: 16,200 frames. All variants use production 32 MiB uniform
capacity; every chunk/record remains visible and non-timing fields match.
Inclusive elapsed means (nested rows must not be added to setup):

| Operation | 32 mean ms | 64 mean ms | 96 mean ms |
| --- | ---: | ---: | ---: |
| Complete setup | 0.577 | 2.758 | 6.516 |
| Draw records | 0.142 | 0.928 | 2.117 |
| Draw ranges | 0.005 | 0.040 | 0.129 |
| Surface conversion | 0.077 | 0.368 | 0.888 |
| Surface sorting | 0.209 | 0.920 | 2.145 |
| Material validation/statistics | 0.021 | 0.109 | 0.424 |
| Area visibility preparation | 0.040 | 0.166 | 0.370 |

The dynamic batches contain 3,632 / 14,528 / 32,688 surfaces, each 208 bytes;
these are distinct from the 30,016 / 120,064 / 270,144 total surfaces. Exclusive
scope ledgers reconcile exactly. Main-thread CPU time is 99.02% / 97.03% / 99.00%
of elapsed probe frames. The original/probe 96 capture is exact.

Enabled versus disabled median differences are −2.07% / −0.36% / −0.65%;
versus original, −0.11% / −0.68% / −3.07%. These are run/code-layout uncertainty,
not negative overhead; no subtraction applied. Original host medians in this
cohort are 2.430 / 10.508 / 26.060 ms. Compare variants within their own cohorts.

A separate read-only audit compares all ten ordering fields at every incoming
position, without hashes. Each of four views (32/64/96 overview, 96 local) has
780 calls: one initial size establishment, 779 identical key batches, zero field
changes. This justifies testing retained order; it does not justify retaining
animated payloads or establish a CPU ceiling. Audit timings are excluded.
[External evidence](../README.md) preserves both diagnostics.

## 2026-10-13 matched overview scaling

Current source, full-area cameras at every size, same repeated city, 1080p,
fixed 16 ms steps, Ryzen/Radeon Release Vulkan, Tracy off, main CPU 2. Six starts
per size/variant, all six size and variant orders, 180 warmup + 600 measured
frames: 54 starts / 32,400 complete frames. All diagnostic variants use a 64 MiB
uniform ring so 96×96 renders completely: +96 MiB across two frame slots versus
production, identical capacity across sizes, unchanged 2 MiB descriptor ring.
Production source and executable restored before the measurements.

Primary uninstrumented results are medians of per-start statistics. GPU and CPU
overlap. Compare sizes within this cohort, not absolute times across sessions.

| Full overview | Tiles | Host median / p95 ms | GPU median ms | Host / 32×32 |
| --- | ---: | ---: | ---: | ---: |
| 32×32 | 1,024 | 2.857 / 4.086 | 0.953 | 1× |
| 64×64 | 4,096 | 13.026 / 14.858 | 2.980 | 4.559× |
| 96×96 | 9,216 | 30.105 / 33.769 | 5.069 | 10.537× |

All tiles/records remain visible. Prepared surfaces are 30,016 / 120,064 /
270,144; particle systems 600 / 2,400 / 5,400; mean rendered particle instances
5,645.12 / 22,580.48 / 50,806.08. These grow exactly 1:4:9. Frame cost per tile
increases 13.98% from 32→64, then 2.72% from 64→96: +17.08% overall.

The same light probe provides **exclusive elapsed means**, 3,600 frames/size;
rows reconcile exactly to the measured probe frames. These are costs, not savings.

| Operation | 32 mean ms | 64 mean ms | 96 mean ms | 96 / 32 |
| --- | ---: | ---: | ---: | ---: |
| Surface filtering, runs and group selection | 0.321 | 2.265 | 5.729 | 17.86× |
| Dynamic draw construction and sorting | 0.477 | 2.308 | 5.585 | 11.72× |
| Model packing and submission | 0.839 | 2.332 | 4.602 | 5.49× |
| Particle update and packet construction | 0.425 | 2.224 | 4.968 | 11.70× |
| Particle rendering | 0.355 | 1.766 | 3.826 | 10.77× |
| Shadow passes | 0.390 | 1.428 | 3.956 | 10.14× |
| Other | 0.226 | 0.819 | 1.930 | 8.55× |
| Total | 3.032 | 13.142 | 30.596 | |

Filtering/run/group cost per surface nearly doubles from 32→96 (+98.45%; all six
pairs +78.5% to +112.7%). Filtering alone is 0.105 / 0.904 / 2.002 ms, group
selection 0.163 / 1.048 / 3.006 ms; cached opaque data dominates both. Dynamic
draw construction is 0.162 / 0.900 / 2.040 ms and sorting 0.206 / 0.909 / 2.145 ms.
Between 64→96, grouping grows 2.869× and shadow passes 2.770× for 2.25× the tiles;
particle update grows 2.234× and rendering 2.167×. Sorting is 2.361×, so it does
not explain all of the disproportionate preparation growth.

The roughly linear total hides this because other work grows more slowly.
Opaque draws are 2,507 / 3,287 / 4,587 despite 1:4:9 instance growth; instanced
rendering calls cost 0.490 / 0.648 / 0.680 ms. Local shadows submit 421 draws at
every size; directional shadows submit zero while bounds tests still run.
Do not normalize every bucket by draw calls or assume every stage is proportional.

This identifies the surface path as a scaling problem, but not a proven quadratic
algorithm. The filtering loop visits each supplied index once; more time per
index need not mean more iterations. Group selection also depends on compatibility
groups per model, whose comparison counts were not collected here. Inspect
repeated checks and data access next. Retaining metadata would cost memory and
invalidation/lifetime maintenance and needs a separate trial. Cache misses,
instruction counts and the causal mechanism remain unmeasured.

Calibration: enabled versus reference aggregate host medians are +0.36% / -1.74% /
-0.17%; enabled versus disabled are -1.65% / -2.19% / approximately 0%. These
negative differences are run/code-layout uncertainty, not negative probe overhead.
No overhead subtraction applied. All sizes spend about 99.6% of the probe frame
on CPU. All timed non-timing sample fields match, all 10,800 probe ledgers reconcile,
and no validation/submission failures are reported.

Seventeen validation captures are retained. Capacity-only 32/64 output matches
production exactly. One initial 64 probe capture differs at one pixel; all six
fixed follow-up captures match exactly, including repeated enabled-binary runs.
The original discrepancy remains unexplained and preserved; universal exact
pixel parity is not claimed. Capture timings are excluded. [Evidence and replay](../README.md)
include controls, raw timings, source, binaries and this validation limit.
No camera limit, optimization ceiling or production capacity policy established.

## 2026-10-13 complete overview attribution

Measurement only, targeting the historical ~13 ms complete overview. Same frozen
runtime, synthetic 64×64 city, 4,096 visible chunks / 6,720 records, fixed camera,
1080p, 16 ms simulation steps, Ryzen AI 9 HX 370 / Radeon 890M, Linux Release
Vulkan, Tracy off, main thread on CPU 2. Current original medians are 14.439 and
14.467 ms in the two cohorts, versus the earlier 12.704 ms. Inputs and logical
submission fields match; the absolute timing drift remains unisolated.

Two fixed cohorts each use six starts × three variants × 600 frames after 180
warmup frames, rotating all six variant orders. Original, instrumented-disabled
and instrumented-enabled binaries distinguish probe and code-layout effects.
The fine probe costs +5.94% aggregate median versus original (+3.09% disabled).
The lighter probe removes per-packet scopes: 482 scope calls/frame versus 4,610.
Its original/disabled/enabled medians are 14.467/14.080/14.363 ms: enabled is
+2.01% versus disabled, -0.72% versus original, with individual original/probe
pairs ranging from -4.73% to +7.61%. This is measurement uncertainty, not a
speedup or negative overhead. No overhead subtraction or normalization applied.

The primary ledger uses **exclusive elapsed means across all 3,600 lighter-probe
frames**; rows sum to 14.698 ms. Mean main-thread CPU time is 14.636 ms and mean
off-CPU time 0.062 ms (99.58% on CPU). CPU time is read at frame boundaries;
off-CPU time remains distributed among the elapsed scopes. Original GPU scene
median is 2.642 ms, overlapping CPU work rather than adding to this ledger.

| Operation | Mean ms | Frame share |
| --- | ---: | ---: |
| Surface filtering, instance runs, group selection and cleanup | 2.772 | 18.86% |
| Dynamic draw records, surface conversion, sorting and validation | 2.647 | 18.01% |
| Main model instance writes, allocation, fallback packing and submission | 2.528 | 17.20% |
| Particle owner/emitter update, simulation and packet construction | 2.382 | 16.21% |
| Particle rendering | 1.961 | 13.34% |
| Directional and local shadow passes | 1.527 | 10.39% |
| Animation clocks and pre-render synchronization | 0.362 | 2.46% |
| Area culling and light preparation | 0.376 | 2.56% |
| Frame begin/end | 0.058 | 0.39% |
| Remaining frame/render/setup | 0.086 | 0.58% |

Within surface preparation, the timed operations are index filtering 1.120 ms,
instance-run construction 0.351 ms, group selection 1.280 ms, dynamic draw-record
construction 1.144 ms, range construction 0.048 ms, surface conversion/skin tables
0.422 ms, sorting 0.924 ms and material validation/statistics 0.107 ms. Remaining
preparation bookkeeping is 0.022 ms. Cached opaque surfaces account for 0.971 ms
of filtering and 1.128 ms of group selection. The relevant code is
`append_render_model_draws_impl`, `collect_sorted_surface_draws` and
`render_prepared_render_model_surface_draw_indices_for_pass` in
`lib/nw/render/viewer/preview_model_draws.cpp`, plus the surface collection/sort
helpers in `lib/nw/render/model_draw.hpp`.

Main model work includes 0.428 ms instance writes, 0.005 ms mapped allocation,
0.700 ms instanced rendering calls and 1.385 ms fallback packing/scalar rendering
calls, plus pass bookkeeping. This bucket includes row copies and renderer
helpers; it is not pure Vulkan API time. The more perturbed fine cohort measures
particle geometry at 0.920 ms, order construction/sorting at 0.482 ms, visibility
at 0.218 ms, resources at 0.135 ms and upload/binding/draw at 0.122 ms. These
secondary details must not be added to the lighter ledger.

Two independent original-executable profiles capture 9,964 / 9,963 user-cycle
samples, zero lost, at 499 Hz for 20 seconds with 32 KiB stacks. Self cycle
shares agree: indexed surface path 21.63/21.50%, draw-record construction
7.47/7.37%, shadow bounds tests 6.14/5.97%, and prepared-surface introsort
4.77/4.44% (some sorting work is inlined into its caller). Vulkan-driver self
shares total 4.29/4.57%. The frame-loop caller is recovered for 97.14/99.97% of
inclusive cycles, but upper call chains can truncate. Inclusive shares overlap;
sample percentages are neither elapsed milliseconds nor removable savings.

All 21,600 timed frames complete with identical non-timing sample fields across
variants and zero reported validation/submission failures. Five separate RGBA
captures match exactly. Both scope ledgers reconcile every instrumented frame.
Capture/profile frame times are excluded. Production library/tool source and
the exact original executable are restored. [Archived evidence](../README.md)
includes both cohorts, failed initial perf buffer allocation, successful bounded
captures, probes, source snapshots, commands, resources and binaries.

The largest combined measured cost is surface preparation (5.419 ms); this does
not establish that all or any of it can be removed. Before another implementation,
inspect which filter/group/sort inputs actually change and which properties were
already validated at construction. Retaining results would cost memory and
invalidation/lifetime maintenance, requiring its own measured trial. Cache misses,
allocation causes, moving gameplay and full-client acceptance remain unverified.
No camera limit or CPU ceiling is established.

## 2026-10-08 scaling investigation

Same Ryzen/Radeon, Release, 1080p standalone viewport. Synthetic repetitions of
the authored city; three starts/case, reversed second order, 180 warmup + 600
samples. Medians of per-start statistics; CPU/GPU overlap. Raw reports, scripts,
profiles and negative capacity cases stay [outside Git](../README.md).

| Area, fixed local camera | Host median / p95 ms | GPU median ms |
| --- | ---: | ---: |
| 16×16 | 1.506 / 2.335 | 1.457 |
| 32×32 | 1.821 / 2.567 | 1.627 |
| 64×64 | 8.076 / 9.448 | 2.421 |
| 96×96 | 17.736 / 19.892 | 2.754 |
| 128×128 | 30.807 / 33.119 | 2.878 |

All 9,000 local-view frames retain 51 chunks, 90 records and 1,401 prepared
surfaces, with identical opaque/transparent/water/directional/local-shadow draw
counts. Particle draws and camera far plane still vary; this is not identical
total GPU work. Whole-area updates persist: 96×96 tick/pre-render medians are
7.627/5.158 ms, versus 0.259 ms for area preparation. Pausing gives 5.554 ms host;
removing lights/shadows still costs 14.126 ms. Both alter output and are diagnostic
controls, not demonstrated optimizations.

Two warmed main-thread profiles have 1,198/1,194 samples and zero lost samples.
Local-view inclusive cycle shares: particle/scene update 44.18%, animation
sampling 25.06%. In the complete 96×96 overview, surface-index submission is
16.94% self / 20.51% inclusive; collection/sorting is also prominent. These are
sampled cycles, not guaranteed elapsed savings; upper caller stacks can truncate.

Other controls: on the same 64×64 area, 51/588/3,008 retained chunks cost
8.361/10.881/15.684 ms host. At the same local city camera, 540p/1080p/2160p GPU
medians are 0.741/1.627/4.613 ms. The 32×32 outdoor view retains 54 chunks at
0.588 ms host versus city's 51 at 1.821; tile-only city is 1.331 ms. Content and
pixel cost prevent a universal tile-count budget.

Complete overviews: 64×64 production is 16.827/18.478 ms host median/p95.
96×96 requires the isolated [uniform-capacity probe](render-perf-uniform-descriptors.md):
39.998/42.623 ms host, 5.098 ms GPU. Production 96/128 overviews silently omit
draws; the probe's 128 overview exhausts descriptors. Exclude incomplete frames.

Next: fix capacity observability, then test reducing offscreen presentation work
while preserving simulation, shadows, lights, attachments and re-entry. Cost:
dependency/state tracking and correctness tests; savings remain unmeasured.
For full views, isolate repeated surface preparation/sorting/submission before
adding caches, whose invalidation/lifetime cost must be justified. This evidence
does not establish an optimization ceiling or a camera limit.

Keep the flat tile grid pending a measured need to replace it. Whole area
preparation is 0.259 ms local / 0.387 ms complete overview at 96×96; the partial
588-chunk view reaches 0.945 ms, so cost is not uniformly negligible. These scopes
include bookkeeping, not just culling. A hierarchy adds storage, traversal and
bounds maintenance. SIMD/culling-layout work remains deferred.

## 2026-10-09 particles and idle doors

Three alternating fresh starts/variant/view, 180 warmup + 600 samples, same
Release Ryzen/Radeon 1080p local cameras. Medians of per-start statistics:

| Area | Original host median / p95 ms | Spawn batching only median ms | Batching + door holds median / p95 ms |
| --- | ---: | ---: | ---: |
| 32×32 | 2.012 / 2.901 | 2.079 | 1.677 / 2.357 |
| 96×96 | 17.448 / 18.817 | 16.904 | 12.848 / 13.931 |

Shared spawn curves now sample once per capacity-clipped batch. Existing RNG
order, force events and placement remain. No retained memory; two extra floats
in the existing stack-local sample. Matched 1,024-particle burst CPU median
31.342 → 19.280 µs; mixed-emitter tick controls are within 1%. Batching alone has
inconsistent area effects (32 regresses 3.3%, 96 improves 3.1%; earlier 96 improved
1.0%), so do not claim a universal frame-time win. The large improvement comes
from [holding idle door poses](render-perf-constant-poses.md), with no new cache.

Live inventory at 32×32: 600 systems / 1,056 emitters, but only 144 systems / 184
emitters contain particles; 5,644 particles total, 4,720 smoke. The source city's
256 tiles enable all three animation-loop flags. Authored smoke examples use
8/20 particles per second and two-second lifetimes. No density change was made.

166 tests pass. Spawn-only and combined captures at both sizes match original
RGBA bytes and particle core fingerprints/emitter seeds. All 10,800 final timing
samples preserve logical submission fields except intended pose reuse/runtime
sync counts; zero reported failures. Temporary inventory code ran after timing
and is removed from production. [Raw evidence](../README.md).

Remaining particle work still spans the whole area, including empty systems and
offscreen presentation. Test those costs next with conservative bounds and
attachment/event correctness; no camera restriction or optimization ceiling is
established. Full-client pacing, active gameplay and memory acceptance remain open.

## 2026-10-09 empty event emitters

After the preceding commit, a fresh 1,193-sample main-thread profile loses zero
samples: particle ticking is 13.74% self / 35.60% inclusive cycles. These are
sampled cycles, not elapsed-time savings. The captured 32/96 scenes have
832/7,488 active event-burst emitters with no particles. They now preserve clocks
and motion history but defer inverse transforms and sprite-sheet sampling until
a queued event or live particle needs them. Cost: one existing-state condition
per active emitter, no allocation or additional persistent state. Attachment
synchronization and empty-system bookkeeping remain.

Three alternating fresh starts/variant/view, 180 warmup + 600 samples, same
Release Ryzen/Radeon 1080p local cameras; medians of per-start statistics:

| Area | Baseline host median / p95 ms | Candidate host median / p95 ms | Scene tick median, before → after ms | GPU median, before → after ms |
| --- | ---: | ---: | ---: | ---: |
| 32×32 | 1.945 / 2.793 | 1.916 / 2.503 | 0.774 → 0.673 | 1.682 → 1.677 |
| 96×96 | 14.263 / 16.461 | 12.838 / 15.170 | 8.083 → 6.640 | 2.664 → 2.618 |

Keep the change: 96×96 host median improves 10.0%, with nonoverlapping per-start
medians (14.107–14.523 versus 12.635–13.061 ms). The smaller scene's 1.5% median
improvement varies by start, including one regression; do not claim a reliable
small-scene frame win. Compare within this matched set, not absolute times from
the earlier experiment; CPU and GPU times overlap.

167 regressions pass, including idle movement/time followed by event activation
and continued live updates in world/attached space. Both captures have identical
RGBA bytes; all 7,200 timed frames preserve logical draw/command fields. No
reported validation/submission failures. [Raw evidence](../README.md)
includes both production binaries, profile and exact commands; no timing probes.
This verifies the fixed standalone workloads, not active gameplay, pacing or an
optimization ceiling. No camera or authored particle-density change.

## 2026-10-10 particle attachment updates

Fresh current-build profile: 1,209 main-thread cycle samples, zero lost.
Attachment synchronization accounts for 13.63% inclusive cycles, with the
intermediate-frame resolver at 12.15% inclusive / 5.49% self. These overlap and
are not elapsed-time savings. That temporary array had only one consumer.

Resolve binding/root/node/default data directly into emitter state, removing
the per-system scratch vector, frame record and second pass. Resolve the bound
owner once per emitter; consult the system owner only when needed for fallback.
No cache or invalidation state added. Indexed node reads, stale-generation/model
checks, authored defaults, explicit targets and visibility resets remain.

Three alternating fresh starts/variant/view, 180 warmup + 600 samples, same
Release Ryzen/Radeon 1080p workloads. Baseline includes the preceding idle-event
change. Medians of per-start statistics:

| View | Baseline host median / p95 ms | Final host median / p95 ms | Scene tick median, before → after ms | GPU median, before → after ms |
| --- | ---: | ---: | ---: | ---: |
| 32×32 local | 2.058 / 3.139 | 1.903 / 3.194 | 0.695 → 0.619 | 1.699 → 1.694 |
| 96×96 local | 12.196 / 15.631 | 12.171 / 15.290 | 6.235 → 6.011 | 2.770 → 2.704 |
| Complete 64×64 overview | 14.203 / 16.456 | 13.931 / 16.117 | 2.643 → 2.421 | 3.150 → 3.189 |

Keep this simplification: complete-overview host median improves 1.9%, and scene
tick improves 8.4%; all three overview pairs have lower host/tick medians. It
retains all 4,096 chunks / 6,720 records using the production buffers. Local
results are noisier: 96×96 tick falls in every pair, but aggregate host median
is nearly unchanged amid downward timing drift. The 32×32 host median improves
7.5% while two individual pairs regress and aggregate p95 worsens, so do not
claim a reliable small-scene gain. CPU/GPU times overlap; no pacing acceptance.

An intermediate fused version and the final deferred-owner-lookup version were
measured alongside baseline. The last lookup change has no demonstrated separate
frame-time benefit; both remove the intermediate array. Initial two-way runs and
all per-start statistics remain in [raw evidence](../README.md).

186 tests pass, including current node/target updates, matrix and decomposed
defaults, alternate owners, model mismatch, reused stale handles and visibility
re-entry velocity. All three final captures match baseline RGBA exactly; the
intermediate version's two captures also match. All 14,400 final timed frames
preserve logical draw/command fields, with no reported validation/submission
failures. No LOD, camera or density change. Full-client editing/gameplay, total
memory acceptance and larger complete overviews remain unverified.

## 2026-10-10 static shadow summaries and light filtering

Current-build profile: 1,200 main-thread samples, zero loss. Directional/local
shadow functions account for 10.39%/7.10% self cycles; light priority evaluation
is 5.38%. These are attribution, not elapsed-time savings.

Both shadow passes reclassified all cached static surfaces every frame: 237,456
surfaces at 96×96. Cache the existing range flags and statistics with their source
surfaces, refreshing on rebuild/edit and clearing on scene reset. Cost: one byte
per cached range (10,872 bytes in this fixture), vector metadata and 20 bytes of
statistics; one extra classification on cache rebuild/edit. Same-size replacements
and same-asset shadow-flag edits refresh the summary. Dynamic surfaces retain
their existing frame path; invalid caster indices remain counted and skipped.

Lighting sorted 19,623 candidate indices before rejecting zero-color lights;
the 96×96 fixture has only 180 colored lights and three final local-view lights.
Move the existing exact-zero RGB rejection before sorting. No additional memory
or retained state; preserve the original sort threshold and stable survivor order.
Current colors are read every frame, so day/night and edits remain live.

Three alternating starts per variant/view, 180 warmup + 600 samples, same Release
Ryzen/Radeon 1080p fixtures. Medians of per-start statistics; baseline includes
the preceding particle changes. [Raw evidence](../README.md).

| View | Baseline host median / p95 ms | Shadow-only host median ms | Combined host median / p95 ms | GPU median, before → after ms |
| --- | ---: | ---: | ---: | ---: |
| 32×32 local | 1.562 / 2.320 | 1.519 | 1.519 / 2.282 | 1.500 → 1.468 |
| 96×96 local | 10.940 / 12.348 | 8.663 | 7.982 / 9.533 | 2.548 → 2.419 |
| Complete 64×64 overview | 14.124 / 15.683 | 13.309 | 12.930 / 14.409 | 3.022 → 3.025 |

Keep both changes: large-local host median improves 27.0%, complete overview
8.4%; both improve in every paired host median/p95. Small-local total improvement
is 2.7%, with one paired p95 regression; the extra light change has no clear
small-local whole-frame benefit. At 96×96,
directional/local shadow medians fall 1.303/0.823 → 0.264/0.069 ms and lighting
preparation 0.771 → 0.060 ms. CPU/GPU overlap; compare within this matched set.

143 tests pass without skips, including GPU-backed area edits, fixed/variable
surface replacement, shadow-flag changes, clearing/visibility and light-order
thresholds/ties/reactivation. All three combined captures match baseline RGBA
exactly; all 16,200 timed frames preserve logical draw/command counts with zero
reported validation/submission failures. Complete overview retains all 4,096
chunks / 6,720 records. No density, camera, LOD or capacity changes.

The estimated 2–3 ms large-local / 4–6 ms overview CPU targets remain unmet.
Large-local scene tick is still 5.430 ms; complete-overview setup/opaque/transparent
submission remains 2.573/3.310/1.037 ms (scopes are not all additive). Full-client
pacing, active gameplay and total-memory acceptance remain unverified.

## 2026-10-10 follow-up search: rejected small changes, duplicated particle data

Fresh main-thread profiles have 1,198 local / 1,194 overview samples, zero loss.
The local view attributes 18.02% self cycles to particle tick, 11.15% attachment
sync, 10.97% presentation and 8.96% scalar curve evaluation. Complete overview
attributes 21.05% to indexed surface submission, 7.50% draw construction and
5.13% surface sorting. These are cycle shares, not predicted time savings.

Two isolated candidates were tried and **neither is retained**:

- Direct segment selection for two/three-key particle curves: every live
  size/color/alpha curve in the captured fixture has three keys. It passed 96
  particle tests, including duplicate-time and endpoint checks, but two opposite-order
  unpinned 96×96 trials worsened host median 9.171 → 9.513 and 9.291 → 9.466 ms.
  The limited trials do not establish an intrinsic algorithmic regression; they
  supplied no reason to retain the extra branches.
- Return after emitter processing when a system remains empty: 456/600 sampled
  city systems are empty. This omits bucket construction, empty particle passes
  and storage cleanup, preserving clocks, motion, emission and force events.
  It improved the large-local median but failed the overview tail acceptance.

The acceptance comparison pins only the benchmark main thread to logical CPU 2
**after loading**, before warmup ends. This HX370 has fast and compact core classes
(reported maxima 5.158 / 3.289 GHz); ordinary trials leave placement unconstrained.
This is a benchmark control, not a production scheduling change. Compare only
within this set. Fixed 1080p cameras, 180 warmup + 600 samples, alternating order,
three small-view starts and six starts for each large view. Three extra large-view
pairs were specified after the first three showed concerning tails; all runs stay
in the aggregate. Values are medians of per-start statistics.

| View | Baseline host median / p95 ms | Empty-tick candidate median / p95 ms | Scene tick median before → after ms |
| --- | ---: | ---: | ---: |
| 32×32 local | 1.592 / 2.318 | 1.529 / 2.258 | 0.292 → 0.262 |
| 96×96 local | 7.923 / 9.403 | 7.588 / 9.239 | 5.394 → 5.007 |
| Complete 64×64 overview | 13.189 / 14.919 | 13.239 / 15.942 | 2.494 → 2.356 |

Local median improves 4.2%; overview median is essentially unchanged (+0.4%),
but overview p95 worsens 6.9%, exceeding the specified <5% regression gate.
The cause of the tail difference is unresolved; a local win alone did not justify
keeping the change. No further reruns or sample exclusions were used to rescue it.
GPU scene medians are 2.589 → 2.564 ms locally / 3.167 → 3.161 ms overview;
CPU/GPU overlap and these results do not establish paced/full-client performance.

The candidate passed 144 particle/import/viewer tests. All 18,000 timed frames
preserve every logical count/command field with zero reported validation or
submission failures. Three captures match RGBA exactly; both local particle
inventories match all core-state hashes and reported emitter state. Complete
overview preserves 4,096 visible chunks / 6,720 records. Candidate/probe source
was removed and production restored to the entry state. [Raw evidence](../README.md).

**Next structural candidate: share immutable particle definitions.** The actual
96×96 scene has 5,400 systems and 9,504 compiled emitter rows, but only 36 distinct
effect definitions (including attachment defaults), containing 74 emitter rows.
Each compiled emitter struct is 1,520 bytes: repeated rows alone hold 14,446,080
bytes versus 112,480 bytes for the distinct definitions, before nested curve
allocations. The scene currently imports/compiles each placement separately in
`append_render_model_particle_systems`. This is observed duplication, not a
measured frame-time benefit from sharing. Investigate asset-owned immutable data
and instance-only mutable state; account for animation selection, edit/reload
invalidation, effect lifetime and non-model previews before implementing. Costs
would be a lifetime/interface change and focused edit/rebuild regressions; avoid
adding a cache keyed by model names. The two small rejected experiments do not
establish a performance ceiling or justify restricting the camera.

## 2026-10-11 shared particle definitions

Implemented the preceding structural candidate: model placements share const
import/compile data by actual asset identity and payload index. Mutable clocks,
seeds, attachment state and particle arrays remain independent. Setup uses weak
cache entries; leases retain definitions, full rebuild rereads authored data,
and model replacement cannot reuse an expired asset's definition. No per-frame
cache lookup/refcount updates. [Contract, costs and tests](render-perf-particle-definitions.md).

Same pinned-main-thread protocol as the search above, with six large-view starts
specified in advance and three small-view starts. Medians of per-start statistics:

| View | Baseline host median / p95 ms | Shared host median / p95 ms | Scene tick median before → after ms |
| --- | ---: | ---: | ---: |
| 32×32 local | 1.527 / 1.828 | 1.540 / 2.278 | 0.273 → 0.266 |
| 96×96 local | 8.177 / 10.463 | 7.443 / 8.948 | 5.492 → 4.911 |
| Complete 64×64 overview | 13.153 / 14.992 | 12.673 / 14.764 | 2.506 → 2.176 |

Retain for the measured large-area benefit: local median improves 9.0%, overview
3.7%, and both aggregate large-view tails improve. All predefined large-view gates
pass. **Small-view p95 worsens 24.6% (+0.450 ms), with median +0.9%; its cause is
unresolved.** Two of three small-view pairs are worse. No added reruns or exclusions;
this needs follow-up before full acceptance and is not an across-workload win.

The 5,400-system inventory now retains 36 definitions / 74 compiled emitter rows,
down from 9,504 rows. Fixed compiled emitter payload falls 14,446,080 → 112,480
bytes (99.2%), excluding nested allocations. Scene rows shrink 216 bytes each.
One production capture reports peak process RSS 853,332 → 817,416 KiB (35.1 MiB
less), including initialization/graphics allocations. Median area load across six
starts falls 1.852 → 1.687 s. This is not a total GPU-memory acceptance result.

388 tests pass with no skips. Exact RGBA matches for all three production captures
and both diagnostic captures, identical local core/emitter state, and unchanged
logical counts/commands over all 18,000 timed frames; zero reported validation or
submission failures. Complete overview keeps all 4,096 chunks / 6,720 records.
Production has no probe. [Raw evidence](../README.md).

Large-local tick still costs 4.911 ms; overview setup/opaque/transparent submission
2.567/3.289/1.012 ms (scopes not all additive). GPU scene medians are 2.421 → 2.391
ms local / 3.012 → 3.009 ms overview. Estimated CPU targets remain unmet; sharing
does not establish a ceiling or imply a camera cap. Full-client pacing remains open.

A separate fixed small-view follow-up (six pairs, 1,800 frames each) does not
reproduce the prior tail increase: host median/p95 1.507/2.049 → 1.510/2.018 ms;
only 2/6 p95s worsen and tick median improves in every pair. A same-process
shared/private-definition diagnostic finds no consistent sharing penalty;
begin-frame time is mostly off-CPU, while slow cohorts also use more CPU time
across update/render. Keep the original result and the new comparison separately.
No runtime changes were needed; [attribution, verification and limits](render-perf-particle-definitions.md#2026-10-11-small-view-follow-up).

## 2026-10-12 current CPU attribution

After culling and uniform-failure observability, fresh warmed main-thread profiles
have 1,181 local / 1,184 complete-overview samples, zero loss. Large-local particle
tick is 16.64% self / 41.68% inclusive cycles; presentation 11.19% self / 18.30%
inclusive, attachment synchronization 11.99% self. Overview indexed surface
grouping/submission is 20.18% self / 34.66% inclusive; draw-row preparation 8.02%
self and surface sorting 4.23% self. Caller stacks truncate; shares overlap and
are not elapsed-time savings. Profiled frame timings are excluded from comparisons.

The bounded run-construction trial was rejected: fusing filtering and run creation
removed a traversal but increased overview host median 12.728 → 12.921 ms (+1.52%),
regressing all six pairs; p95 14.227 → 14.540 ms. Small-local median/p95 rose
0.88%/7.13%; large-local median fell 1.40%. All 21,600 frames preserve counts and
commands, three captures match RGBA exactly, and 108 tests pass. Source/tests and
production executable were restored. Trial added per-row run-end writes and
changed allocation order/capacity for filtered inputs; their separate costs are
not isolated. Fewer passes alone did not yield a win.

Next: measure group-search/compatibility distributions and stable versus changing
surface fields before retaining groups. Offscreen particle presentation remains
a separate dependency/re-entry problem. No CPU ceiling or camera restriction
established. [Evidence](../README.md).

- [ ] Attribute CPU time to grouping, constant construction/copying, allocation,
  binding, animation/particles and waits; separate render passes.
- [ ] Record draw/group distributions and which payload fields actually change.
- [ ] Check GPU pass/whole-frame timing and validity; investigate opaque/shadow
  cost as well as CPU submission. Exclude incomplete/lost samples explicitly.
- [ ] Rank RP-03/04/05 work by measured cost; include rejected directions.

Use existing scopes/counters first. Measure instrumentation overhead separately;
performance comparisons run with tracing off. Cost: diagnostic runs and any
necessary temporary instrumentation. Done: attributed costs and next experiments.
