# Rendering performance overhaul

Status: open. Keep populated NWN areas fast and the implementation simple.
The user considers 30 fps reasonable for a toolset overview; earlier 4/2 ms
aspirations are not mandatory overview gates. Gameplay needs a separate complete-
frame budget. No new view/content limit has been selected.

Latest trial: [packet-constant particle base axes](render-perf-particle-presentation.md)
fails its three-pair screen. The 96 particle phase stays 3.866 → 3.878 ms and
large-local host median rises 5.30%, exceeding the 5% limit. Rejected; committed
source and the exact reference executable are restored. 149 candidate/restored
tests, 12.08 million exact axes, five exact captures and 4,500 matching timed
frame pairs pass. No retries or larger CPU-ceiling conclusion.

Latest retained change, committed as `34e296313`: [exact draw-transform reuse](render-perf-draw-transforms.md).
Six matched pairs give 96 host median 18.704 → 17.907 ms, all six faster, and
p95 22.080 → 20.653 ms. Active cache payload is 5.49 MiB plus vector capacity.
125 tests and five exact capture pairs pass; every declared control gate passes.
The fresh post-change profile has 7,013 main-thread user-cycle samples, zero lost:
particle rendering 24.80–25.30% inclusive, draw construction 9.01–9.88% self.
Shares overlap and exclude workers/off-CPU waits; consistent 60 fps remains open.

The preceding [shared view constants](render-perf-view-constants.md), committed
as `e070e8c2a`, reduce per-draw model uniforms to 288 bytes and pass their own
matched gates. Its 96 host median is 18.421 → 17.821 ms; do not compare absolute
times across sessions. Earlier [static root/normal payload reuse](render-perf-draw-submission.md#2026-10-15-retained-static-root-payloads)
is retained as `57ea71926`. Each linked issue preserves its cohort and limits.

Previous review: [draw construction and submission](render-perf-draw-submission.md#2026-10-15-draw-bounds-trial-rejected-submission-reuse-inventory).
Exact interval bounds pass all correctness checks but fail to establish a useful
frame gain: 64 overview 8.067 → 8.136 ms, 96 overview 18.339 → 18.302 ms.
Trial rejected and production restored. Its submission inventory found 16,272
root/normal rows rebuilt per 96-overview frame from 10,872 unchanged roots,
leading to the retained payload change above. The bounds trial itself made no
runtime change.

Previous trial: [shared shadow preparation](render-perf-draw-submission.md#2026-10-15-shared-shadow-preparation-trial-rejected)
improves 96 overview median 18.354 → 17.999 ms, with exact output, but misses
the small-local p95 limit. A fixed follow-up has 9.67% p95 disagreement between
identical baselines, leaving acceptance inconclusive. Trial rejected and the
committed renderer restored; all measurements and the candidate are archived.

Previous retained change: [exact shadow-culling simplification](render-perf-draw-submission.md#2026-10-14-exact-shadow-culling-simplification).
Six matched pairs/view give 32/64/96 overview medians 2.393/9.158/21.283 →
2.221/8.422/19.087 ms, with every overview pair faster and all local/control gates
passing. Geometry counters and five image pairs are exact. The new profile puts
bounds culling at 2.53% inclusive; draw preparation/submission and particle
rendering remain larger targets. GPU visibility/indirect submission remains open;
the inventory exposed substantial per-surface binding cost in that proposal.

Previous retained change: [Taskflow particle batches](render-perf-particle-execution.md#2026-10-14-taskflow-particle-batches-accepted-tradeoff)
reduce complete 96-overview host median 22.336 → 19.741 ms and large-local
7.132 → 4.759 ms, improving all six pairs. 244 tests, exact live state/captures
and three ThreadSanitizer runs pass. The small-local p95 gate fails twice:
the controlled follow-up rises 1.984/1.931 → 2.104 ms. The user explicitly accepts
that tradeoff for the large-view gains. The [three-pair client check](render-perf-gameplay-acceptance.md#2026-10-14-taskflow-candidate-replay)
improves interval median/p95 26.334/28.773 → 22.712/25.077 ms, with 2 → 6
intervals above 33.333 ms per 1,800 frames. This does not close gameplay or
hard-deadline acceptance.

Previous CPU checkpoint: [validated surface sort reuse](render-perf-draw-submission.md#2026-10-14-validated-surface-sort-reuse)
reduces clean matched 32/64/96 overview host medians **2.327→2.190 /
10.090→9.293 / 23.525→21.961 ms**, improving all six pairs/size. Exact incoming
keys validate the retained indices; animated payloads remain fresh. 155 tests,
five exact capture pairs and 36,000 equivalent-work frames pass. Local-control
median increases of 2.88% / 0.63% remain within the declared 5% tolerance.
The subsequent [full-client replays](render-perf-gameplay-acceptance.md#2026-10-14-sort-reuse-replay-under-concurrent-load)
initially overlap a CPU-heavy updater. After the user's completion report and
measured 9–11% residual updater CPU, the final three-pair replay gives interval
median **30.631→29.770 ms**, p95 **43.078→39.540 ms**, improving two pairs.
That still fails the 30 fps tail budget; remaining timing variation is not
attributed solely to the updater. No CPU ceiling or need for camera restriction
established.

Next performance candidates follow the [current submission profile](render-perf-submission-profile.md):
[GPU particle preparation](render-perf-particle-presentation.md#next-experiment-gpu-particle-preparation),
remaining draw preparation and model submission. Parallel command recording was
[measured and rejected](render-perf-draw-submission.md); another threading change
needs new evidence. GPU visibility/indirect output remains a separate proposal.
Also finish [gameplay/native-input acceptance](render-perf-gameplay-acceptance.md)
and resolve tail variability. The earlier capacity checkpoint's paced full-client
check meets the 30 fps overview budget at median/p95 in all three starts of each
32/64/96 fixture.
Pooled frame interval median/p95: 16.061/16.835, 16.071/17.572 and 27.010/30.731 ms.
96 has 23/1,800 intervals above 33.333 ms, maximum 37.220 ms; no hard-deadline claim.
All chunks/records remain visible and all allocation/bind/draw counters are clear.
[Production uniform capacity](render-perf-uniform-descriptors.md) is now 32 MiB
per slot (+32 MiB/context), enough for the complete 96 frame including UI.
128 remains beyond uniform/descriptor capacity. No camera restriction selected.
The latest [retained static grouping checkpoint](render-perf-draw-submission.md#2026-10-13-retained-complete-view-static-groups)
improves complete 32/64/96 overview host medians from 2.752 / 12.697 / 29.578 ms to
2.370 / 10.268 / 24.340 ms, with gains in all six pairs/size. 151 tests pass;
five captures and all 36,000 measured-frame counters match. Retention costs
0.75 / 3.01 / 6.79 MiB of vector/container capacity. No camera or quality limit.
Acceptance is provisional: small-local p95 initially rose 11.69%; a fixed 21,600-frame
follow-up beats both baseline controls, but the identical controls differ 5.19%,
so the strict tail gate remains inconclusive. The subsequent capacity-only check
has median changes −2.2% to +2.3%, but 64-overview p95 rises 11.43%; retain that
unresolved result. That earlier capacity replay demonstrates a scoped 30 fps
result; aggressive CPU targets and an optimization ceiling remain
unproven. Known asset failures and untested gameplay activity remain explicit.
The [preceding attribution](render-perf-submission-profile.md#2026-10-13-matched-overview-scaling)
identified the repeated static filtering/run/group work removed by this change.
The earlier [particle storage probe](render-perf-particle-execution.md) saved only
0.078 ms in the isolated large-local update phase, below its material-gain gate;
the production ownership migration remains rejected.
The [uniform-overflow reporting gap](render-perf-uniform-descriptors.md) is fixed;
larger workloads still reject explicitly. The preceding [group-search change](render-perf-draw-submission.md)
removes 403,648 irrelevant checks per overview frame: host median -1.58%, with a
96×96 local median tradeoff of +1.07%. 109 tests and exact captures/counters pass.
Packing, SIMD culling and additional spatial trees remain unselected/deferred.
Require a measured need before adding machinery. Keep this tracker and existing
work issues concise; detailed notes, one-off scripts and generated evidence stay
[outside Git](render-perf-evidence/README.md).

## Recorded full-client baseline

Ryzen AI 9 HX 370 / Radeon 890M, Release/Tracy off, uncapped 1080p full client.
Three runs/view, 180 warmup + 1,200 samples; medians of per-run statistics.

| View | Host median / p95 ms |
| --- | ---: |
| Full-area overview | 5.457 / 7.121 |
| Orbiting overview | 5.472 / 8.096 |
| Moving city plaza | 2.713 / 3.749 |
| Moving outdoor | 0.921 / 1.641 |
| Moving gate route | 1.886 / 3.038 |

Overview retains all 1,024 chunks, 1,680 models and 600 particle systems;
viewport 1101×986 with editor UI. Gameplay viewport 1919×1080, synthetic input,
fixed 60 Hz simulation and 16 ms animation steps. The gate is an area-border
route; interior/outdoor coverage is separate. Instrumented baselines, not a
speedup comparison or proof of display cadence/input latency. CPU/GPU overlap;
complete gameplay activity, native input and total memory remain unverified.
Known asset failures: city `plc_s03`, outdoor `plc_d07`.

2026-10-08 review corrected nonuniform shadow sampling, resolving both recorded
pixel discrepancies. 655 regressions pass without skips; 24 fixed-view captures
match batched/split output exactly, and six material-count cleanup captures
match their corrected reference exactly. [RP-05](render-perf-draw-submission.md)
records matched correction costs, including increased GPU time in player views.
The full-client baseline above predates this correction; it is not a final
acceptance result.

2026-10-08 scaling investigation does **not** establish an optimization ceiling
or justify a tile-based camera limit. A fixed 51-chunk city view grows from
1.821 ms host median at 32×32 to 30.807 ms at 128×128, with unchanged main/shadow
draw counts; whole-area particle/animation work dominates that growth.
The complete 64×64 overview is 16.827 ms. A diagnostic 64 MiB uniform buffer
allows a complete 96×96 overview at 39.998 ms host / 5.098 ms GPU; the production
16 MiB buffer silently omits draws there. Even that probe exhausts descriptors
at 128×128. Incomplete frames are excluded. Production source/binary restored;
no camera or capacity policy shipped. These are synthetic standalone workloads,
not a supported-area limit or full-client acceptance.

2026-10-09 follow-up found unnecessary idle-door animation: default hold clips
looped even though the transition path already froze completed holds. Corrected
default playback plus particle spawn batching reduces matched 96×96 local host
median 17.448 → 12.848 ms (32×32: 2.012 → 1.677 ms), with exact captures and 166
passing regressions. [Results and limits](render-perf-submission-profile.md).
Particle batching helps bursts; its area-only result is inconsistent. Whole-area
particle work remains a candidate; there is still no basis for declaring a ceiling.

The next isolated particle change skips unused derived samples for empty event
emitters while retaining clock/motion history. A fresh matched 96×96 local set
improves host median 14.263 → 12.838 ms (10.0%); the 32×32 frame result is small
and inconsistent. 167 tests pass, captures and submission counts match. See the
same [results](render-perf-submission-profile.md) for costs, tails and limits.

2026-10-10 attachment synchronization now writes directly into emitter state,
removing its intermediate frame array and duplicate pass. Complete 64×64 overview
host median improves 14.203 → 13.931 ms (1.9%), with scene tick 2.643 → 2.421 ms;
local frame results are less conclusive. 186 tests pass, captures and logical
submission counts match. No new cache, LOD or camera policy; [results and limits](render-perf-submission-profile.md).

The following static-shadow and light-filtering changes remove two whole-area
costs: classify cached shadow ranges when their surfaces change, and reject
zero-color lights before priority sorting. Matched 96×96 local host median falls
10.940 → 7.982 ms (27.0%); complete 64×64 overview 14.124 → 12.930 ms (8.4%). All three
captures and 16,200 timed-frame submission counts match, with 143 passing tests.
Large-local scene tick remains 5.430 ms; the estimated 2–3/4–6 ms local/overview
CPU targets remain unmet. [Costs, tails and remaining work](render-perf-submission-profile.md).

A further 2026-10-10 search retained no new runtime change: short-curve sampling
showed no measured benefit, and skipping empty particle batches improved local
median 4.2% but failed the overview tail gate. Both were removed. The next
structural lead is observed duplication: 5,400 systems / 9,504 compiled emitters
represent only 36 distinct effect definitions, with 14.45 MB of repeated emitter
struct payload versus 112 KB of distinct rows before curve allocations. Sharing
had no measured performance result at that point. [Experiments, evidence and limits](render-perf-submission-profile.md).

2026-10-11 implements [shared particle definitions](render-perf-particle-definitions.md):
the 5,400-system fixture now uses 36 definitions / 74 compiled emitter rows.
Matched large-local host median improves 8.177 → 7.443 ms (9.0%); complete overview
13.153 → 12.673 ms (3.7%). All predefined large-view gates pass, 388 tests pass,
captures/state match, and 18,000 timed frames preserve counts. Small-view p95
worsens 1.828 → 2.278 ms; retain that unresolved follow-up before full acceptance.
The CPU targets remain unmet and there is still no evidence of a hard ceiling.

A separate six-pair small-view follow-up does not reproduce that tail increase
(2.049 → 2.018 ms p95; tick median improves in every pair). Same-process
definition-sharing diagnostics separate waiting from execution and show no
consistent sharing penalty. Keep the sharing change and the original evidence;
general tails/full-client acceptance remain open. [Details](render-perf-particle-definitions.md#2026-10-11-small-view-follow-up).

Explicit model backface culling reduces matched GPU scene time 12.45% / 8.63% /
2.69% for small local / large local / complete overview. Overview host median
increases 1.66% (12.848 → 13.061 ms); p95 improves. All 165 selected tests pass,
and 21,600 timed frames preserve counts with no recorded dropped draws. This is
a GPU gain with a measured CPU tradeoff, not evidence of a CPU ceiling.
[Details and visual limitations](render-perf-backface-culling.md).

Uniform exhaustion is now visible in frame/pass counters and causes incomplete
area benchmarks to fail after writing their reports. The former 96/128 overview
false successes are rejected with unchanged pixels/counts. 117 tests and five
matched captures pass; host medians stay within 0.2%, and the initial small-view
p95 increase is not reproduced by a separate fixed follow-up. Fresh profiles
still identify whole-area particle work locally and surface grouping/submission
in overview. [Reporting fix](render-perf-uniform-descriptors.md),
[current attribution](render-perf-submission-profile.md#2026-10-12-current-cpu-attribution).

A subsequent single-pass run-construction trial regresses overview host median
1.52% in all six pairs despite exact output/counts and 108 passing tests. Source,
tests and executable are restored; no trial optimization retained.
[Rejected trial](render-perf-draw-submission.md#2026-10-12-filtered-run-fusion-rejected).

## Remaining work

- Verify doors closing, actor activity, edits/undo, reloads and resource lifetimes;
  compare final/common baseline, paced frames, tails, visuals and memory (RP-06).
- Complete paced/full-client acceptance of provisional batching and
  constant-pose reuse (RP-05/08/10); the recorded fixed-view differences are resolved.
- Assess measured preview startup (~1.6 s city) and door transitions (RP-15).
- Decide optional investigations from actual budgets; an unselected idea is not
  a required implementation. Do not close unresolved correctness work as complete.

| Issue | Status |
| --- | --- |
| [RP-01: Workloads/budgets](render-perf-workload-contract.md) | Open |
| [RP-02: Attribution](render-perf-submission-profile.md) | Open |
| [RP-03: Static groups](render-perf-static-main-batches.md) | Open; deferred |
| [RP-04: Uniforms/descriptors](render-perf-uniform-descriptors.md) | Open |
| [RP-05: Submission](render-perf-draw-submission.md) | Open |
| [RP-06: Acceptance](render-perf-gameplay-acceptance.md) | Open |
| [RP-07: Shadow ranges](closed/render-perf-shadow-surface-ranges.md) | Closed |
| [RP-08: Frame tails](render-perf-frame-pacing.md) | Open |
| [RP-09: Shader failures](closed/render-perf-shader-failures.md) | Closed |
| [RP-10: Constant poses](render-perf-constant-poses.md) | Open |
| [RP-11: Swapchain count](render-perf-swapchain-images.md) | Open; optional |
| [RP-12: Depth targets](render-perf-frame-depth.md) | Open; optional |
| [RP-13: Target initialization](closed/render-perf-benchmark-clear.md) | Closed |
| [RP-14: Particle culling](closed/render-perf-particle-culling.md) | Closed |
| [RP-15: Navigation transitions](render-perf-navigation-transitions.md) | Open |
| [RP-16: Shared particle definitions](render-perf-particle-definitions.md) | Implemented; small-view increase not reproduced, full-client acceptance open |
| [RP-17: Backface culling](render-perf-backface-culling.md) | Implemented; GPU gain, overview host median +1.66%, full-client acceptance open |

Close the cycle when scoped acceptance passes and required work is resolved.
Record deferrals/rejections explicitly; preserve visuals and behavior. Keep one
justified change per commit. The archived notes retain earlier evidence/history.
