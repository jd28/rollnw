# Performance evidence

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Current work/results: [tracker](../../rendering-performance-overhaul.md).
Keep actionable issues concise. One-off plans, reports, probes, logs, binaries
and captures belong outside Git; do not add a README per experiment.

Latest optimization trial: `build/render-fused-preparation-2026-10-22/RESULT.md`
— rejected and restored despite a measured overview gain. Per-instance range and
surface construction saves 0.739593 ms / 4.10% paired geometric host time; all six
pairs improve, upper one-sided95% ratio 0.981246. Paced-city outer GPU p95 rises
5.15% (bounds 1.003430–1.101948, median paired increase 0.140767 ms), failing the
declared control guard. All 202 tests, 27 exact images, 48 starts / 57,600 frames
and 28,800 exact logical pairs complete; confirmation did not run.

The first five starts remain at their original positions. A post-exit guard
incorrectly required two observer samples from a short uncapped outdoor run;
all its frames and GPU/activity checks were valid. Only that terminal guard was
corrected, preserving sampling behavior, candidate binaries and acceptance rules.
The missing summary was validated from saved rows; no frames were repeated.
Original failure/controller, repair plan, server-recovery checks, both input
manifests and the interruption spanning one outdoor pair are preserved. All nine
source files, 192 client / 160 mudl shipping files, original test binary, frozen
runtimes and 5,950 resumed inputs verify exactly. No candidate implementation
remains; the binding-summary improvement is intact. Raw evidence is local,
external archival pending. [RP-18](render-perf-parallel-frame-preparation.md#fused-preparation-trial-2026-10-22).

Preceding measurement check: `build/render-measurement-calibration-light-2026-10-22/RESULT.md`.
Identical frozen executables complete 24 starts / 28,800 measured frames and
14,400 exact logical pairs. Eight resolution checks and five directional checks
fail; overview median paired difference is 0.174160 ms, paced-city outer GPU
median ratio 1.022843 (bounds 1.009932–1.035920). Observer CPU is 0.484–0.721%.
The first observer attempt is separately preserved after five complete starts
and one interrupted start because it exceeded its 1% CPU budget. No renderer
source change or speedup; 5,710 current and 5,701 original inputs, nine sources,
192 production client files and 160 mudl files verified unchanged. No child
remains. Raw evidence is local, external archival pending. This does not explain
the previous outlier or alter earlier retention decisions.

Preceding optimization trial: `build/render-surface-order-2026-10-22/RESULT.md` — rejected.
Constructing fresh surfaces directly in cached pass order removes full-payload
permutation, but its 0.753548 ms median paired overview host saving is inconclusive
(ratio 0.976414, upper one-sided 95% 1.015091). Paced-city outer GPU median rises
2.26% under the declared rejection rule. All 48 starts / 57,600 measured frames
and 28,800 logical pairs complete; 202 tests and 27 corrected exact images pass.
An original reference orbit-image divergence remains unexplained and preserved.
Before timing, both replays were corrected to isolate native input, apply commands
once per recorded frame and compare capture camera rays; all five pairs pass.
No timing retry or confirmation cohort ran. Original source/runtime/test hashes
and 5,896 inputs are verified. No candidate remains; the prior binding-summary
optimization is intact. Raw evidence is local, external archival pending.
See [RP-18](render-perf-parallel-frame-preparation.md#phase-1-extract-one-simple-serial-batch-transform).

Preceding trial: `build/render-draw-attribution-2026-10-22/RESULT.md` — rejected.
Two baseline CPU profiles select direct draw-row emission. The 48-start screen
measures 0.214548 ms median paired overview host saving (1.37% geometric reduction),
below the declared 0.25 ms minimum; paced-city median interval rises 0.154%.
All 201 tests and 27 corrected exact images pass. The original browser-hover
comparison failure and identical replay repair remain preserved; no timing retry
or confirmation cohort ran. All 57,600 measured frames / 28,800 logical pairs,
profiles, code, raw runs and exact source/runtime/input restoration are recorded.
No candidate implementation remains. Raw evidence is local; external archival
is pending. See [RP-18](render-perf-parallel-frame-preparation.md#phase-1-extract-one-simple-serial-batch-transform).

Preceding trial: `build/render-draw-refresh-2026-10-22/RESULT.md` — rejected.
In-place draw refresh removes duplicate transform storage, but overview host time
does not meet either gain gate and paced city establishes five control regressions
(host p95 +4.08%). Median peak overview RSS is 842.94 → 835.34 MiB. All 48 timed
starts / 57,600 frames complete, with 201 passing tests, 27 exact images and
28,800 exact timed logical pairs. The confirmation matrix was not run after
rejection. Entry source, 192 client and 160 mudl runtime files, and 5,893 timing
inputs are hash-verified after restoration; the prior retained optimization below
is intact. Raw evidence remains local; external archival is pending. See
[RP-18](render-perf-parallel-frame-preparation.md#phase-1-extract-one-simple-serial-batch-transform)
for the decision and unresolved attribution.

Latest retained optimization: `build/render-surface-preparation-2026-10-22/RESULT.md`.
Collect binding statistics during surface emission: 0.436358 ms median paired
large-overview host saving, 2.15% paired geometric reduction. Six-pair screen and
extended paced-city control pass; other confirmation cases retain two-pair
limits. Evidence includes 68 timed starts / 81,600 frames, 201 passing tests,
27 exact images, all original runs, frozen inputs and final source/runtime hashes.
The small paced-city GPU concern and its bounded four-pair extension are preserved.
Raw evidence remains local; external archival is pending. See
[RP-18](render-perf-parallel-frame-preparation.md#phase-1-extract-one-simple-serial-batch-transform)
for the retained contract, costs and next step.

Preceding attribution: `build/render-parallel-phase0-2026-10-22/RESULT.md`
and `results-summary.json`. [RP-18](render-perf-parallel-frame-preparation.md)
records the actionable decision: serial surface-preparation simplification before
threading. Seven client starts, two glTF starts, four exact images and two CPU
profiles complete; production restoration is hash-verified. The original overlay
comparator failure and corrected comparisons are preserved. Raw evidence is local
and external archival remains pending; that diagnostic phase claimed no gain.

The original 94 performance notes/scripts/issues from `e673bea3d` are preserved,
with original repository paths and a verified per-file manifest, in:
`/home/josh/projects/rollnw-evidence/2026-10-07/repo-performance-notes.tar.gz`.
SHA-256: `e67a30e1e7ae82ad1b8ea63ff88f5748a3b0a8c7d7291aab82f655f32a90fc12`.
Historical script/report links now point here; extract the archive to recover
those files. Maintained benchmark tools remain in `tools/mudl/`.

The two presentation evidence directories are also archived under
`/home/josh/projects/rollnw-evidence/2026-10-22/`. Every payload entry was read back
and verified, then each canonical copy was checked against the archive hash.

| Archive | SHA-256 |
| --- | --- |
| `render-client-waits-2026-10-22.tar.gz` | `c61b4ff95d1c029f6059eb75840f796bdffa2e947b8beabff89c484bb325ea5e` |
| `render-display-paced-2026-10-22.tar.gz` | `080262ef99cb9c8427192b2c9138f3dde9dee1cc7664fdc7592bdfeed1331ce8` |

The subsequent one-pending wait screen is staged in
`build/render-present-wait-2026-10-22/`: sixteen exact images, twelve complete
timing starts, 14,400 calibrated frame records and 7,200 exact timed logical pairs.
`result.md`, `results.json` and `costs.json` reject this policy due to reduced
cadence and worse tails. Per-frame wait ordering, all returned image counts,
separate process/DRM memory, frozen source/runtime and exact production restoration
are retained. No production change or complete pacing acceptance is claimed.

Display-paced evidence is staged in `build/render-display-paced-2026-10-22/`:
sixteen exact images, 2,400 capture logical pairs, one complete timing pair with
1,200 exact logical pairs, and the city run with one unavailable pixel timestamp.
The fixed twelve-start screen stops after three starts. `partial-results.json`
and `result.md` reject simple FIFO due to queue delay and report per-second cost;
no complete-cohort gain is claimed. All calibration samples, the guarded duplicate
preflight invocation, frozen runtime and exact restoration are preserved.

Wait/presentation evidence is staged in `build/render-client-waits-2026-10-22/`:
six exact images, 2,400 logical pairs and eight fixed observation starts with
9,600 complete frame/presentation records. Flat CSVs retain requested/actual sleep,
main-thread execution, fence/acquire/submit/present waits, opaque clock domains,
refresh properties and unavailable pixel results. `result.md` and `results.json`
separate ordinary limiter rounding, uneven displayed cadence and unreproduced
rare stalls. The failed initial build, frozen diagnostic, original sources,
system SDK hashes and exact six-source/192-runtime restoration are preserved.
No production optimization or new performance acceptance is claimed.

The nine recent evidence directories below are now archived under
`/home/josh/projects/rollnw-evidence/2026-10-22/`. All 9,929 regular payload
files were read back and checked against per-file manifests; every canonical
archive matches its locally verified SHA-256. Raw staging directories remain.
Together they contain 5,904,904,124 payload bytes in 1,078,477,821 compressed bytes.

| Archive | SHA-256 |
| --- | --- |
| `render-client-acceptance-2026-10-21.tar.gz` | `d9723fa6c9d1120995133035be7663cce67059e04bda650f8cad99804cafae48` |
| `render-client-pacing-2026-10-21.tar.gz` | `84aa5021a30a0707cbb5b52c89b615df9094babb02bb17f67ac44a119a81a46e` |
| `render-material-coverage-2026-10-21.tar.gz` | `c3ed2842743a7eb2db5596cd94858750d344d4a73f2abe296ad48497d8cc4658` |
| `render-skin-lookup-2026-10-21.tar.gz` | `18e5ae654e92a3a1f56ae4c697c95a18535eeffab398cfdc3dc6df24b166b6f7` |
| `render-indirect-models-2026-10-21.tar.gz` | `991458795a98e3b737d3fd2dae507eb989763a7614cff6485c2ee72687c57d8e` |
| `render-client-large-2026-10-21.tar.gz` | `d3cfef53b44a7cc433a40c5ec9bde4a40878fcabf476cfd4591f8f30e02afe0c` |
| `render-client-exit-2026-10-22.tar.gz` | `fe8362c876341aa9e1aefe6a896cc34ee98858cd2ecd13ad504743df826f6960` |
| `render-display-sync-2026-10-21.tar.gz` | `74a8b52a55bbc89304ad9fc81e0c2ecc8592068ff0f61fcd3caf01fcf50961f5` |
| `render-client-final-checks-2026-10-21.tar.gz` | `f5833d15f6b8bbc7d81c1baef31d91d93df85a36cb13f03c58356bbee4b6397a` |

Final editor evidence is staged in `build/render-client-final-checks-2026-10-21/`:
222 additional passing tests, reused unchanged renderer coverage, 322 synthetic
edit/undo actions with seven exact restored roots, save/reopen and scene reload.
Three replacements invalidate old areas and 40 child handles; 1,024 models remain
after reload. Three visually inspected cloth-edit captures are not exact-pixel
comparisons across animation frames. Failed build/adapter attempts, both diagnostic
runtimes, all import errors and exact source/runtime/fixture restoration checks
are preserved. Native pointer input and timing are outside this check's scope.

Large-view evidence is staged in `build/render-client-large-2026-10-21/`.
Six complete capture starts supply twelve exact image pairs and 4,800 exact
logical pairs, including diagnostic/production capacity equivalence. All twelve
production-capacity starts pass their overview median/p95 budgets with 14,400
complete rows. `production-result.md` and `production-results.json` preserve
tails, exceedances, GPU/memory details and scope. The historical comparison is
incomplete after nine complete starts and a normal reference exit without
terminal rows; `incomplete.json` and the failed start remain intact. No sample
was replaced or retried. The separate one-run quit-observer diagnostic in
`build/render-client-exit-2026-10-22/` completed without reproducing the exit;
its timing is excluded. Exact production source/runtime restoration passes.

Display synchronization findings are staged in
`build/render-display-sync-2026-10-21/`: raw KWin diagnostics report 120 Hz and
Adaptive Sync Never; the current configuration predates the pacing trial.
This is not historical per-frame VRR or physical presentation telemetry.

The rejected nanosecond frame-sleep trial is staged in
`build/render-client-pacing-2026-10-21/`. Five tests,
20 exact images and 6,000 capture rows pass; all 96 timing starts finish with
115,200 frame/GPU samples and 57,600 exact logical pairs. All three pacing targets
pass, but 16/42 declared regression guards fail. `result.md`, `timing-results.json`
and all raw pairs preserve that decision. `restoration-verification.json` verifies
exact original source and all 192 production runtime files. Candidate source and
both candidate runtimes remain frozen; no implementation is retained or run retried.

The completed corrected-reference client cohort is staged in
`build/render-client-acceptance-2026-10-21/`.
All 160 scheduled starts complete with 192,000 checked frames/GPU samples and
96,000 exact logical pairs, without retries or discarded starts. `result.md`,
`timing-results.json`, all raw rows/process/memory/background records, input/runtime
manifests and terminal process records preserve the result. Cumulative regression
guards pass; paced gameplay's absolute interval-p95 gate fails. Rare stalls and
baseline variability remain included. The later large-production and lifecycle results are recorded above; large
paired timing and native pointer checks remain outstanding; no new optimization was made in
this measurement step.

The retained material coverage repair is staged in
`build/render-material-coverage-2026-10-21/`.
It contains the pre-edit plan/source snapshots, review, 236 passing tests,
20 exact client and two exact glTF images, 6,000 matching client activity rows,
12 standalone timing starts and final source/runtime manifests. `result.md`
reports the cost of restoring valid coverage separately from performance gains;
the local96 host baseline is unstable. `final-verification.json` verifies all six
changed files, three restored probe sources, production/frozen runtimes and inputs.
The known coverage defect is repaired; broad gameplay/headroom acceptance is open.

The bounded skin-lookup experiment is staged in
`build/render-skin-lookup-2026-10-21/`: plan, frozen original routine, synthetic
comparison, ASan/UBSan parity, candidate source/runtime, three exact image pairs,
twelve timing starts, and exact source/runtime restoration with 141 passing tests.
`screen-result.json` records passing target/point guards and failed baseline
stability; `result.md` distinguishes the measured routine gain from unaccepted
frame performance. No implementation is retained and no timing retry was run.

Rejected packed geometry/indexed submission evidence is staged in the ignored
`build/render-indirect-models-2026-10-21/` directory.
The final four-control matrix has 44 successful starts, 33 exact whole-RGBA
comparisons across eleven scenes and 252 matching non-timing area sample pairs
with declared binding/allocation/call deltas. The prototype's 156-test cohort has no
failures/skips. Source/runtime manifests, prior failed/revised attempts, plans,
review, captures and the completed 64-run timing cohort are retained. All 28,800
paired timing samples preserve non-timing work with the declared deltas. Point
guards pass, but the baseline-stability gate fails in three views; the screen is
inconclusive and no timing-based retry was performed. See
[RP-05](render-perf-draw-submission.md#2026-10-21-diverse-content-comparison--rejected-and-source-removed).
The subsequent seven-case glTF comparison completes 21 correctness and 126
timing starts: 14 exact images, 294 capture sample pairs and 50,400 timing sample
pairs. All four model gates fail. The stable textured case is slower in all six
pairs. model-comparison-result.md, model-timing-results.json and the before/after
input/runtime hashes preserve the result. The prototype is removed. The rebuilt
runtime passes 174 selected tests without skips and two exact restoration captures
(textured glTF and area32), with all 42 non-timing sample pairs matching without
counter exceptions. retirement-verification.json records the final source/runtime
hash checks and terminal process results. This is not an accepted gain.
The same staging directory now holds the common model-benchmark plan, review,
two frozen runtime/source manifests, 22-test CLI results, ten runtime checks and
the final versioned-report check. Helmet and area output are exact; all 21 area
rows match with no command-counter exceptions. Those initial checks verify the
tool; the separate matched model comparison above supplies performance evidence.

Raw evidence remains in `/home/josh/projects/rollnw-evidence/`:

Latest submission-layout inventory is at
`2026-10-20/submission-layout.tar.gz`; the canonical copy matches the locally
verified archive. SHA-256:
`47ef985bc07aa5f193e986f1c07fc93be1700f8a38fa007afa244be819466535`.
All 564 payload entries / 509 regular files match the manifest, with 90,640,177
payload bytes and 45,363,728 compressed bytes. Eleven reference/probe pairs have
exact images, all 22 corrected starts exit zero, and 84 paired area samples have
identical counts/commands. No timing result or production change. Raw rows,
generated glTF controls, scripts, two frozen runtimes, failed initial build/start,
source restoration and external dependency hashes are preserved. Both hooks and
156 production runtime files are restored exactly. The report distinguishes
ordered compatibility bounds from actual performance and retains transparency,
skinning, texture-diversity and final-art coverage limits.

| Location | Contents |
| --- | --- |
| `2026-10-06/` | Earlier comparisons, manifests and pre-cleanup history bundle |
| `2026-10-07/client-overview.tar.gz` | Full-client fixed/orbiting overview |
| `2026-10-07/play-preview.tar.gz` | Gate movement/opening and navigation fix |
| `2026-10-07/gameplay-views.tar.gz` | Interior city/outdoor actor routes |
| `2026-10-07/material-packing.tar.gz` | Structural audit; no packing implementation |
| `2026-10-08/shadow-sampling.tar.gz` | Shader correction, red/green GPU regression, 655 tests, matched costs, batch/count-cleanup image checks |
| `2026-10-08/area-scaling.tar.gz` | Area/coverage/content/resolution controls, zero-loss CPU profiles, uniform/descriptor capacity failures and isolated probe |
| `2026-10-09/particles-doors.tar.gz` | Spawn batching, idle-door hold correction, live particle inventory, 166 tests, matched three-way timings and exact captures |
| `2026-10-09/idle-event-particles.tar.gz` | Empty event-emitter derived-work reduction, fresh zero-loss profile, 167 tests, matched two-way timings and exact captures |
| `2026-10-10/particle-attachments.tar.gz` | Direct attachment updates, zero-loss profile, 186 tests, matched local/complete-overview timings and exact captures |
| `2026-10-10/shadows-lights.tar.gz` | Retained static shadow summaries and early zero-color light filtering, 143 tests, three-way matched timings and exact captures |
| `2026-10-10/particle-search.tar.gz` | Two rejected particle candidates, fresh local/overview profiles, 18,000 timed frames, exact captures/state, duplicated-definition inventory; baseline restored |
| `2026-10-11/particle-definitions.tar.gz` | Shared immutable definitions, 388 tests, 18,000 timed frames, exact captures/state and memory inventory; large-view gates pass, small-view p95 follow-up open |
| `2026-10-11/particle-tails.tar.gz` | Fixed 21,600-frame production follow-up and 31,680-frame same-process sharing/CPU-time diagnostic; original small-view increase not reproduced; no production change |
| `2026-10-12/backface-culling.tar.gz` | Explicit model color culling; 165 tests, 56 new GPU readbacks, 11 matched captures and 21,600 final timed frames; GPU gains, overview host median +1.66% |
| `2026-10-12/uniform-observability.tar.gz` | Uniform failure/demand counters and benchmark rejection; 117 tests, five exact captures, 10,800 original / 21,600 pinned follow-up frames, fresh CPU profiles |
| `2026-10-12/submission-fusion.tar.gz` | Rejected filter/run-construction fusion; 108 tests, three exact captures, 21,600 frames; overview host +1.52% in all six pairs; baseline restored |
| `2026-10-13/group-search.tar.gz` | Call-local model-partitioned search; 403,648 fewer overview checks/frame, 109 tests, exact captures/counters, 21,600 frames; overview host -1.58%, large-local +1.07% |
| `2026-10-13/particle-execution-review.tar.gz` | Fresh zero-loss local/overview profiles and shared particle execution proposal; previous inventory labeled, runtime unchanged |
| `2026-10-13/particle-batches.tar.gz` | Persistent pooled update probe on real snapshots; exact state parity, six starts/view; large-local isolated update -8.79% (0.078 ms), below material-gain gate; runtime restored |
| `2026-10-13/overview-attribution.tar.gz` | Reconciled complete-overview operation timings, original/disabled/enabled controls, 21,600 frames, five exact captures and two original-runtime profiles (19,927 samples, zero lost); runtime restored |
| `2026-10-13/overview-scaling.tar.gz` | Current 32/64/96 full overviews with identical diagnostic capacity; 32,400 complete frames and per-stage scaling; 17 captures including an unreproduced one-pixel discrepancy; runtime restored |
| `2026-10-13/retained-groups.tar.gz` | Provisional static grouping cache; 151 tests, five exact scene captures, 36,000 matched frames plus 21,600 duplicate-baseline tail frames, storage/rebuild probe; overview gains 13.9–19.1%, strict small-view tail gate inconclusive |
| `2026-10-14/uniform-capacity.tar.gz` | Production 32 MiB capacity; 109 tests, five exact pairs, 18,000 matched standalone frames, explicit 96/128 negative cases; 5,400 paced full-client frames meet 30 fps median/p95 across 32/64/96; tail and gameplay limits retained |
| `2026-10-14/draw-preparation.tar.gz` | Fresh draw-preparation attribution; rejected compact-sort trial; validated cached permutation, 155 tests, five exact pairs and 36,000 matched frames; three full-client cohorts with explicit background-load limits; capacity/reuse probes and exact production restoration |
| `2026-10-14/particle-taskflow.tar.gz` | Four-worker particle candidate; 244 tests, exact live state/images, three final TSan runs, 36,000 final standalone frames and 3,600 client frames; repeated small-local p95 gate failure, subsequently accepted by user |
| `2026-10-14/draw-threading.tar.gz` | Current 32/64/96/96 main-thread profiles, 14,640 samples with zero loss; parallel recording ownership audit and GPU shadow visibility/indirect design; no new runtime change |
| `2026-10-14/shadow-culling.tar.gz` | GPU caster inventory; retained exact CPU culling simplification, 12 tests, 25.2 million live visibility comparisons, ten exact image pairs, 36,000 final frames, and fresh 96 profile; all final gates pass |
| `2026-10-15/shadow-preparation.tar.gz` | Rejected shared caster/range preparation; 19 tests, five exact image pairs, 36,000 matched frames and 21,600 duplicate-baseline tail frames; overview median gain 2%, small-local acceptance inconclusive; production restored |
| `2026-10-15/draw-bounds.tar.gz` | Rejected exact world-bounds arithmetic; 45 tests, 9.94 million exact live bounds, 15 exact image pairs and 36,000 matched frames; repeated static root/normal payload inventory; production restored |
| `2026-10-15/root-payloads.tar.gz` | Retained static root/normal payloads; 48 tests, 4.61 million exact live rows, ten exact image pairs, 36,000 main and 21,600 duplicate-baseline control frames; 96 overview median 19.377 → 18.736 ms; fresh profile |
| `2026-10-16/particle-packets.tar.gz` | Rejected packet-local particle UV lookup; 149 candidate/restored tests, 12.08 million exact live rectangles, 15 exact image pairs, 36,000 matched frames; 96 overview median 19.264 → 19.866 ms; production restored |
| `2026-10-17/particle-compute.tar.gz` | Compute billboard screen; 236 candidate / 150 final-boundary / 235 restored tests, five bounded image comparisons, 4,500 matched timed frame pairs; large-view gain, small-local control failure; baseline restored |
| `2026-10-18/particle-projection.tar.gz` | Projected billboard inventory and two-phase removal grid; 15 exact observation pairs, 36 removal comparisons, 34 empty cases; tiny bright details and offscreen stretched batches identified; 149 restored tests; no latency claim or production change |
| `2026-10-18/stretched-cull.tar.gz` | Rejected stretched-batch frustum culling; 151 candidate / 149 restored tests, 15 exact image pairs, 953,920 rejected quads verified clipped and 9,000 matched timed frame pairs; local-96 particle phase gains no time, control tails fail; original source/runtime restored |
| `2026-10-19/particle-vertex.tar.gz` | Direct vertex billboard trial; GPU rotation mismatch eliminated by original CPU trigonometry, five exact corrected captures and 4,500 matched timed frame pairs; overview median -8.51%, large-local controls fail; 173 candidate/restored tests; original source/runtime restored |
| `2026-10-19/particle-attribution.tar.gz` | Three-path same-process diagnostic; nine exact capture pairs and 27,648 measured frames, GPU expansion saves ~0.90 ms overview particle thread CPU but only ~0.012–0.015 ms locally; previous local failure not reproduced or explained; exact baseline restored, 175 tests pass |
| `2026-10-19/particle-material.tar.gz` | Rejected two-file material-copy removal; 175 candidate/restored tests, ten exact captures and 9,000 exact timed render-counter pairs; local96 particle target and overview32 frame controls fail; original source/runtime restored |
| `2026-10-19/renderer-controls.tar.gz` | 36 unchanged-code runs / 21,600 matched frames and three exact image pairs; both worker placements exceed 5% repeatability controls. GPU boundary test isolates small 3×2 atlas-coordinate differences; exact production restored, 175 tests pass |
| `2026-10-19/particle-vertex-uv.tar.gz` | 48-byte CPU-atlas/GPU-expansion trial; 176 candidate tests, 12,096 exact GPU outputs, ten exact capture pairs and 96,000 timed frames. Overview96 paired host/particle medians improve 4.55%/16.67%; small-view p95 bounds fail. Preserved storage abort plus fresh whole cohort; exact baseline restored, 175 tests pass |
| `2026-10-20/gameplay-activity.tar.gz` | Door close/reopen/cleanup regression, 104 tests; city/outdoor native X11 keyboard checks, 1,560 observed frames and six captures. Four failed pointer attempts preserved; pointer/Wayland and matched visual/performance acceptance remain open. Three source hooks and 192 runtime files restored exactly |
| `2026-10-20/common-client-visuals.tar.gz` | Corrected common reference, five replay cases/three variants, 18,000 checked rows and 20 exact no-cull/reference images. Current visual differences attributed to color culling; not approved as equivalent. Harness failures, invalid reference64 capacity and incomplete reference96 retained. No timed cohort; 51 source replacements and 192 runtime files restored exactly |

Shadow-sampling archive: 517 entries verified against its per-file manifest;
SHA-256 `7335ddc9a79b853ec228bcbc8a231ef2922ddce8f7786f8c714a93bc6ab345c5`.
Its `plan.json`, `review-result.json` and `final/self-check.json` describe the
scope; `timing/`, `captures/` and `count-captures/` retain raw comparisons.
`entry/worktree.patch` preserves the initial probes; shipping C++ probes were
removed. Fixture symlinks refer to the existing local NWN installation; the
archive does not bundle or fingerprint the entire game installation.

Area-scaling archive: 448 payload entries verified against its per-file manifest;
SHA-256 `246c869d77f2f60321306b995816c7b45ad03b1314e4d8138a9ee02e0575e54e`.
`result.json`, `checks.json` and `self-check.json` distinguish complete workloads
from invalid capacity cases. In the original summaries, `valid` only means the
existing counters passed: production 96/128 overviews silently omit draws.
Use `aggregate.json`'s `complete_render` and `capacity-aggregate.json` when
interpreting timings. The probe restores complete 96×96 output; its 128×128
overview still fails descriptor allocations. Both runtime binaries, exact probe
patch, fixtures, raw reports, images and profiles are retained; production source
and executable were restored exactly. Fixture/resource symlinks require the local
game installation. The directory date records the investigation's start.

Particles/doors archive: 390 payload entries verified against its per-file manifest;
SHA-256 `da2b110a9246d1eb27c49d8df15b1099e83a1ca2e663954ec3f181ac97d23a6f`.
`result.json`, `checks.json` and `self-check.json` record the outcomes and limits;
`matched-final-*` contains 10,800 timed frames. Baseline/spawn-only/final binaries
and separate inventory probes are retained. Inventory runs after timing; the
spawn-only timing binary also retains that untimed report field. Final production
contains no probe. Shaders and stdlib are frozen in the archive; fixtures/harness
depend on the area-scaling archive and local game/module paths listed in
`dependencies.json`. Initial wrong-directory/test-tolerance failures are retained
alongside the intended original-code door regression failures and final passes.

Idle-event-particles archive: 265 payload entries verified against its per-file
manifest; SHA-256
`75a42d11fa1a57142c129e9cbb094baeae9013c5ba9eb3d5d79f2eb08e6606ec`.
`result.json`, `checks.json` and `self-check.json` retain outcomes and limits;
`matched-*` contains 7,200 timed frames, `verify-*` contains exact image checks.
Both timing binaries are production builds without inventory probes. The baseline
binary is the preceding experiment's final production build (code committed as
`155ae5add`); the source patch and binary hashes are retained. `profile/` contains
1,193 main-thread samples with zero loss; its timings are excluded from comparisons.
Shaders/stdlib are frozen here; fixtures/harness and prior inventory depend on
the existing archives and game/module paths recorded in `dependencies.json`.

Particle-attachments archive: 397 payload entries verified against its per-file
manifest; SHA-256
`0be854e2d1f9fc4ca18999d17b368a4f630da0afc133df825cb16c83042c1395`.
`result.json`, `final-results.json`, `final-checks.json` and `self-check.json`
record the decision and limits. `matched-final-*` contains 14,400 timing frames:
three variants for both local views, baseline/final for the complete 64×64 view.
`direct` is final; `candidate` is the first fused version. Initial two-way runs
remain under `matched-*`; use `verify_final.py` for the final workflow. All three
production binaries and shaders/stdlib are retained, with no timing probes.
`entry.patch` records the accepted idle-event baseline change from `155ae5add`;
`change.patch` is incremental from that baseline. The profile has 1,209 samples
and zero loss; its frame times are excluded. Fixture/harness dependencies are
listed in `dependencies.json`; local game/module resources are not bundled.

Shadows/lights archive: 355 payload entries verified against its per-file manifest;
SHA-256 `982008f88f551dcc8b5c317ebb7596b3a54a0173e0ce0b4e639d20b86bdddc01`.
`plan.md`, `results.json`, `checks.json` and `self-check.json` record costs,
invalidation, outcomes and limits. `matched-*` contains 16,200 timed frames across
baseline/shadow-only/combined variants and three views; `verify-*` captures match
exactly. `before-*`/`shadow-check-*` are preliminary comparisons. All three
production binaries, incremental patches and shaders/stdlib are frozen; no probes.
`profile/` contains 1,200 main-thread samples with zero loss, excluded from timings.
`entry.patch` includes the accepted uncommitted particle changes; baseline matches
the preceding archive's `direct` binary. Dependencies remain the area-scaling
fixture/harness and local game/module paths listed in `dependencies.json`.

Particle-search archive: 499 payload entries verified against its per-file manifest;
SHA-256 `2218f162c106d1c6aea0fa91184d70dc9c6624f538fc5001bed7adf622cbf7a8`.
`plan.md`, `acceptance.json`, `final-results.json`, `final-checks.json` and
`self-check.json` record both rejected candidates and the restoration. Use final
results (3 small-view / 6 large-view starts); initial `results.json` has only the
first 3 starts. `matched-*` records 18,000 frames with the main thread pinned to
CPU 2 after loading; `.affinity.json` records each operation. Preflight/trials and
captures remain unpinned. Profiles have 1,198/1,194 samples, zero loss, excluded
from timing comparisons. All five binaries, frozen shaders/stdlib, candidate
patches and probe source are retained. `particle-state-checks.json` verifies both
local inventories; `definition-summary.json` records repeated/distinct payload.
144 candidate tests and 143 restored-baseline tests pass; restored production
SHA-256 equals the frozen baseline. `entry.patch` preserves prior uncommitted
work; dependencies still require the area-scaling harness/fixtures and local
module/game paths. No candidate runtime code remains.

Particle-definitions archive: 458 payload entries verified against its per-file
manifest; SHA-256
`8de2a2ef91d58a0b6c5d76d8beaacb552e6862a0cdeb6aeaa4ca190a0da4982b`.
`plan.md`, `result.json`, `acceptance.json`,
`final-results.json` and `self-check.json` record the ownership contract and outcome.
`matched-*` contains all 18,000 timed frames (3 small-view / 6 large-view starts),
main thread pinned to CPU 2 after load. Large-view gates pass; small-view p95
increases 24.6% and remains open. `additional-results.json` records binary hashes,
loads, exact diagnostic images and single-capture peak RSS, including initialization
and graphics allocations. `particle-state-checks.json` confirms exact local
core/emitter state and 36 shared definitions / 74 compiled rows. All three production
captures match exactly. Four binaries, frozen shaders/stdlib, incremental and full
patches, original plan, probes and raw reports/logs are retained; production has
no probe. 388 tests pass with no skips. Initial build/missing-time-tool failures
are preserved. Fixture/harness and local module/game dependencies are listed in
`dependencies.json`; the working directory retains the 2026-10-10 start date.

Particle-tails archive: 273 payload entries verified against its per-file manifest;
SHA-256 `e6e20544d53ba3d949dead48ad60718fc3c0ad6c29a8e34d8e0d7aa8d7faa55b`.
`plan.md` fixes the follow-up scope and thresholds before
running. `production-results.json` records six alternating 1,800-frame pairs;
`diagnostic-results.json` records four starts of twelve alternating shared/private
definition blocks, thread CPU/context switches, exact counts/state/captures and
193–195 ns stamp microchecks. All 53,280 rows are retained; 60 frames per diagnostic
block were designated settling beforehand. Diagnostic results are attribution,
not production acceptance. `original-cohorts.json` retains the prior-run breakdown;
the original regression is not discarded or combined into the new comparison.
Three frozen binaries, resources, commands, temporary probe patch, raw reports,
build logs and self-check are included. Production source/executable are restored
exactly; existing 388 regressions were not rerun. Dependencies still require the
area-scaling fixture/harness and local game/module resources. General tail causes
and paced/presented gameplay acceptance remain open.

Backface-culling archive: 949 payload files verified against its per-file manifest;
SHA-256 `c3b5dbe87a4ad4795b3fae3bc5a2aedc526936ec81bec8434509b550011a94a6`.
`report.md` contains the full Tier 1 plan, decisions, costs and self-check.
`accepted-results.json` is the final production comparison: six alternating pairs
per scene, 21,600 frames, main thread pinned to CPU 2 after load. GPU scene time
falls 12.45% / 8.63% / 2.69%; overview host median rises 1.66%, with improved p95.
All paired counts/commands match; recorded failures/drops/overflows are zero.
Earlier `matched` and historical `final` cohorts retain the wider-row and compact
intermediate candidates separately. Four binaries, resources, source patches,
raw commands/reports/logs, failed attempts, process RSS/elapsed times, captures,
analysis drivers and `self-check.json` are included. Final source/binary match the
measured production version; 165 tests pass without skips, including 56 new GPU
readbacks. Eleven production captures match earlier culling captures exactly;
baseline differences and visual/IBL limits are quantified in the report. Shadow
passes remain two-sided. Fixture/harness and local game/module dependencies are
listed in `dependencies.json`; no full-client or all-assets acceptance claim.

Uniform-observability archive: 498 payload files verified against its manifest;
SHA-256 `15ba57c9f467b98470c99efa29b79efce61aa32a9cf7c59bf09d422de5001fdc`.
`plan.md`, `report.md`, `self-check.json` and `accepted-results.json` record the
contract, costs and original three-pair timings. Host medians remain within 0.2%;
small-view p95 +8.15% is retained. The separate six-pair `tail-pinned` cohort does
not reproduce it (p95 -2.61%, 2/6 pairs worse); an initial unpinned 21,600-frame
cohort is excluded for its documented driver setup mismatch. No samples are
discarded within the accepted cohorts. Negative 96/128 overviews preserve pixels
and old counters but now report uniform exhaustion and exit 1. All new pass
deltas sum to frame totals. Both binaries/resources, raw reports/logs, commands,
drivers and failed setup are retained. Fresh main-thread profiles contain
1,181 / 1,184 samples and zero loss; frame times under profiling are excluded.
Caller truncation and dependency/visual/pacing limits remain explicit. Base is
704e222e9; external fixture/game/module dependencies are in dependencies.json.

Submission-fusion archive: 431 payload files verified against its manifest;
SHA-256 `215b6d9b1d0d77e0f64b82023d36a4cb3fbb2203bb77c841c8b9b6505f349545`.
`plan.md` fixes the trial and acceptance gates; `accepted-results.json` is the
six-pair timing cohort (the prefix does not mean the change was accepted).
`decision.json` rejects it: overview host median regresses 1.52%, in all six
pairs, while p95 rises 2.20%. All 21,600 frames preserve counts/commands; three
captures match exactly and 108 tests pass, including 16 added filtered/invalid-row
GPU readbacks. Trial source/tests and both binaries/resources are retained with
raw commands, logs, captures and build evidence. `self-check.json` verifies
source/tests restored to 448ca303e and installed mudl SHA equals the frozen
baseline. No trial runtime or test change remains. The restored baseline's 117
regressions were already verified; source restoration was rebuilt. Profiles and
external fixture/module/game dependencies are referenced in dependencies.json.

Scope corrections: legacy automatic gameplay cameras were roof/emitter views;
early standalone targets loaded uncleared color. Current explicit-view baselines
do not establish a cross-session speedup or full-game acceptance. Overview raw
JSON has a stale zoom label; its final driver uses fit/pitch only. Archived notes
retain hashes, exact commands, known asset failures and verification limits.


Group-search archive: 483 payload files verified against its per-file manifest;
SHA-256 `39bc12e84197f210dced605a2d8b9556df5c21b4016d71722564785cc81a92e5`.
`plan.md`, `review.md`, `report.md`, `decision.json` and `self-check.json` document
contracts, costs, gate and limits. `accepted-*` holds six alternating pairs/view,
21,600 complete uninstrumented frames, with exact sample/per-pass counters;
overview improves in five pairs, large-local medians regress in all six within
the predeclared tolerance. Three separate validation captures are byte-exact.
`diagnostic-*` and `probe-comparison.json` verify removed unrelated-model checks,
unchanged compatibility/group/run counts and observed lookup capacities across
4,320 paired calls; diagnostic timings are excluded from acceptance. All four
baseline/candidate/probe executables, patches, source snapshots, tests, drivers,
shaders and stdlib are retained. Production source contains no probes and the
installed executable matches the measured candidate exactly. Fixture/harness and
local game/module dependencies are listed in `dependencies.json`; those external
assets are not bundled. No full-client acceptance or optimization ceiling claim.


Particle-execution review archive: 192 payload files verified against its manifest;
SHA-256 `3155aa6038f55f42921fb4052c7f869c2f988cfb8531277428b50278f212e8af`.
`profile/` contains current-checkpoint main-thread captures: 1,181 local / 1,169
complete-overview samples, zero lost. Profiled frame times are excluded from
comparisons. `plan.md`, `proposal.md` and `self-check.json` record the user's
steering toward particle architecture and the proposed bounded batch experiment;
no runtime optimization was attempted. `prior-inventory-summary.json` explicitly
labels the older definition-sharing inventory and its archive dependency.
The frozen executable, resource snapshot, profile commands/reports and primary
engine references are retained. Production source/executable remain unchanged.


Particle-batches archive: 333 payload files verified against its per-file
manifest; SHA-256
`da0a52862460a7361c3519727c8a908de70fbb01416f9235df1972f83cc73cc5`.
`plan.md`, `result.md`, `probe-results.json` and `self-check.json` define the
scope and decision. `batch_probe.inc` and `probe.patch` implement the temporary
probe; `accepted-probe-*` retains all 18 independent starts, affinity records,
120 alternating paired blocks/case/start and exact core-state checks. The
rendered setup frames are not timing comparisons. Emission is disabled equally
in both update arms; live spawning, owner-range maintenance and rendering from
the pool are excluded. The actual large-local snapshot misses the predeclared
10% gate; synthetic half-dead churn passes. No production migration retained.
Three preliminary probe/capture starts and the fresh compiled-field inventory
are preserved separately. `restoration.json` verifies clean library/tool source
and the exact original runtime SHA. Binaries, shaders and stdlib are frozen;
`dependencies.json` lists external fixture/game/module requirements. No complete
frame or pooled-render image acceptance is claimed.

Overview-attribution archive: 493 payload files verified against its per-file
manifest; SHA-256
`19491c7fcdcf8a54c72a38c7c0fa3bdac642e8425bfccab4f04b1f5e6fcc94df`.
`plan.md`, `result.md` and `self-check.json` document the measurement-only scope.
`results.json` retains the more perturbed fine cohort; `thin-results.json` and
`ledger.json` give the primary exclusive elapsed means and exact reconciliation.
Each cohort has six starts per original/disabled/enabled variant, 600 frames per
start. `profile-bounded/` and `profile-results.json` retain both zero-loss profiles;
the initial perf buffer failure remains in `profile/`, excluded from attribution.
Five captures match exactly; all non-timing sample fields match across variants.
The archive freezes binaries/resources, probe headers/patch, final instrumented
source, original source, commands, affinity, logs and raw per-frame data.
`result.md` explains replay and probe limits; `dependencies.json` lists external
fixture/game/module and runtime library requirements. `restoration.json` verifies
clean production library/tool source and the original executable SHA. No runtime
optimization retained; historical timing drift and removable savings remain open.

Overview-scaling archive: 647 payload files verified against its per-file manifest;
SHA-256 `e573fda80196f68e92c57bd4828f70baa44129dcb624e4df0b1094610517327d`.
`plan.md`, `result.md`, `results.json` and `self-check.json` describe six starts
per size/reference/disabled/enabled variant, all using complete overview cameras
and a diagnostic 64 MiB uniform ring. `analyze.py` verifies every non-timing
sample field and all 10,800 probe frame ledgers; raw commands, affinity, timings,
counts and logs are retained. `validation-summary.json` covers all 17 captures:
one initial 64 enabled image differs at one pixel, while six fixed follow-up
captures match exactly. `initial-capture-audit.json` and `recapture-checks.json`
preserve that unresolved limit; the strict initial image checker still fails on
the original image. Capture frame times are excluded from timing comparisons.
`source-audit.md` distinguishes measured scaling from unmeasured algorithm/cache
causes. All four binaries, source snapshots, patches, probe header, shaders and
stdlib are frozen; external assets/libraries are listed in `dependencies.json`.
`restoration.json` verifies clean production source and the original executable.
No runtime optimization, production capacity policy or universal image-parity claim.


Retained-groups archive: 790 payload files verified against its per-file manifest;
SHA-256 `5fef690697b5cef0d5fe236cec98a79abd25d5757d378c15929f8fb53b5a0975`.
`plan.txt`, `result.txt`, `decision.txt` and `self-check.json` distinguish the
repeatable overview gain from provisional acceptance. `results.json` preserves
the initial small-local p95 gate failure; `tail-followup-plan.txt` and
`tail-results.json` preserve the fixed follow-up's failed baseline-stability gate,
even though the candidate beats both identical baseline controls. All raw
commands, timings, counters, affinities, logs, captures, original/candidate source,
patches and five binaries are retained. `invalidation-audit.txt` links ownership
and generation contracts to tests; `memory-results.json` records retained
capacity and one rebuild over 240 frames/size. The original missing-game-env test
failure is retained alongside the subsequent 151 passing tests. Shaders/stdlib
are frozen; external assets/libraries are listed in `dependencies.json`.
`restoration.json` verifies the measured production candidate and 16 MiB capacity,
with temporary diagnostics removed. Full-client acceptance remains open.

Uniform-capacity archive: 6,710 payload files verified against its manifest;
SHA-256 `8729f14e77199832cbb8ddb79a65e8fa66646d831b69bea36b6d527a10882ead`.
`plan.txt`, `self-check.json`, `results.json` and `client-results.json` distinguish
capacity correctness, equivalent-work timings and the scoped paced overview
budget result. All commands, raw frames, captures, logs, isolated client project,
five binaries and runtime resources are retained. The initial temporary-probe
build error and postprocessor availability correction are documented in
`harness-corrections.txt`; no timing run was discarded or repeated for those fixes.
`restore-audit.json` verifies exact production client/mudl hashes after removing
the probe. External fixture/game dependencies are in `fixture-dependencies.json`.
96 overview has 23/1,800 paced intervals above 33.333 ms despite passing median/p95
in all three starts. The capacity-only standalone 64-overview p95 increase remains
unresolved. Native-input/gameplay, asset failures and larger-area limits remain
open; this archive does not supersede earlier inconclusive tail gates.

Draw-preparation archive: 3,166 payload files verified against its manifest;
SHA-256 `4b556f76435460e9addcdf5fd4f5831e1f21b23227a0d992c3fc32664f5d863d`.
`entry.json`, `plan.txt`, `probe-results.json` and
`key-audit-results.json` preserve current-source attribution and exact key
stability. The root `results.json` and `first-candidate-decision.json` reject the
first compact-sort candidate despite its misleading aggregate-median ratio.
`permutation/results.json` and `gates.json` contain the retained candidate's six
matched pairs/view and all predeclared standalone gates. Raw frames, commands,
affinities, logs, images, source snapshots, patches and executable variants are
included. `permutation/confirm-results.json` records actual retained capacity,
one rebuild per 780-call fixed view and reconciled diagnostic ledgers.

`permutation/client-results.json`, `client-post-updater-results.json` and
`client-settled-results.json` preserve all client cohorts. The first two overlap
an unrelated updater; the second filename does not prove it had exited.
`background-load.txt` records the mistaken empty-snapshot inference and its
correction; the final cohort follows the user's completion report and records
actual updater CPU ticks over each run. No timing runs were discarded.
`client-capture-review.json` identifies the live timing HUD as the entire image
difference; all other pixels match. `restore-audit.json` verifies production
mudl/client/test hashes and removal of temporary hooks. External game/module and
fixture dependencies are in `dependencies.json`; raw artifacts remain outside Git.

Particle-taskflow archive: 2,409 payload files verified against its manifest;
SHA-256 `a14f4dd180a7a23a9d5ab1d99b4223024f6741ec17e0fb65e52ab2581aa9d599`.
`plan.txt`, `screen-decision.txt`, `measure-results.json` and
`local-followup-results.json` preserve the worker screen, cutoff decision and
both failed small-local tail gates. `decision-pending.txt` explicitly leaves the
tradeoff unaccepted at archive creation. The user subsequently explicitly chose
to keep the change; that acceptance is recorded in the maintained particle issue.
The immutable archive preserves the original failed gates and pending decision.
The fixed final standalone cohort has 36,000 frames; the two-control follow-up
has 10,800. `state-results.json` records 920 live serial/parallel comparisons,
including 2,649,600 system rows and 20,121,888 live particle rows. Five production
capture pairs and five audit captures match exactly. `client-results.json` holds
all 3,600 paced frames; `client-capture-review.json` confines differences to live
timing text and records visual inspection. Original harness/build failures,
all raw commands, affinity, process costs, captures, source and executable
variants are retained. TSan build intermediates are excluded, with the frozen
test executable, build configuration, source and logs included; upstream
atomic-fence instrumentation warnings remain explicit. `restoration.json` and
`self-check.json` verify temporary hooks removed and measured production candidate
hashes. `dependencies.json` lists external fixture/module/game/runtime needs;
the pinned upstream archive and hash are preserved. No camera limit or general
gameplay/deadline acceptance is claimed.

Draw-threading archive: 231 payload files verified against its manifest;
SHA-256 `a05818648201acad163f38e2d126cb07c3220c42b7cd8c3034ad2e26dbfde3b8`.
`plan.txt`, `design.txt`, `profile-results.json` and `self-check.json` retain
current-source costs, explicit ownership/ordering/error contracts, assumptions
and implementation boundaries. Four finite main-thread user-cycle captures
cover the complete 32/64/96 overview, repeating 96 once; all 31,000 report frames
remain complete. Profile timings are excluded from speed claims. The separately
labeled unprofiled scope medians reuse the retained Taskflow cohort, referenced
in `dependencies.json`. Raw perf data/reports, benchmark commands/counts/logs,
affinity, source snapshots and frozen runtime resources are included. Worker CPU
and off-CPU waits are outside the profile scope; inclusive shares overlap.
The user's GPU-shadow question is incorporated without changing runtime code.
GPU/parallel-recording proposals remain unimplemented and unverified for speed;
the production binary matches the retained particle candidate exactly.

Shadow-culling archive: 1,492 payload files verified against its manifest;
SHA-256 `03a09dab9240e286af05f753df561adc4c2f38f7eb560126182aeaf603fd8edb`.
`plan.txt` records the GPU inventory and measured CPU simplification control;
no GPU visibility/indirect implementation is claimed. `screen-results.json`
preserves the small-local regression in the short screen; `measure-results.json`
contains the planned six-pair comparison, all passing gates and 18,000 matching
frame pairs. Five production captures and five diagnostic captures are exact.
`audit-results.json` records 25,232,844 live caster/view comparisons in 1,820 calls.
`profile-results.json` has 3,535 main-thread user-cycle samples, zero lost, and a
complete 3,500-frame workload. Profile/audit/inventory timings are excluded from
acceptance. Source, binary variants, raw reports, affinity/process costs, test
results, fixture inputs and harnesses are retained. `restoration.json` verifies
the production source and measured binary restored exactly; `self-check.json`
records scope, numeric/error contracts and remaining GPU/full-client limits.
`dependencies.json` records external game/module/linked fixture assets; those
linked asset directories are excluded from the archive. An incomplete duplicate
asset copy exhausted tmpfs only after all runs; it was removed without touching
source assets or results. The final archive contains 2,950,525,701 payload bytes
and is 368,292,885 bytes compressed.

Shadow-preparation archive: 1,080 payload files verified against its manifest;
SHA-256 `03f49947cef7f7eb91f394d6ed453f2bb21fbb28afcd7751ad418bc3a0c57fc1`.
`plan.md`, `review.md`, `measure-results.json`, `tail-plan.md`, `tail-results.json`
and `self-check.json` preserve the trial and rejection criteria. The original
small-local p95 failure remains explicit; the fixed follow-up has 9.67% p95
disagreement between identical baselines and cannot clear acceptance. All 18,000
main frame pairs and 7,200 follow-up triples have exact non-timing fields; five
image pairs are exact. Candidate source/patches, frozen runtimes, test results,
raw measurements, affinity/process costs and harness are included. Nineteen
candidate tests pass, including 84 scalar depth comparisons; an extended pass-toggle
test also passes. `restoration.json` verifies all trial source files reverted and
the original measured executable restored exactly; 17 restored-source tests pass.
No new profile or GPU implementation. `dependencies.json` fingerprints fixture
files and records external game/module/fixture dependencies; their linked asset
directories are not copied. Payload is 1,723,468,413 bytes; archive is 243,209,301 bytes.

Draw-bounds archive: 1,180 payload files verified against its manifest; SHA-256
`5f6e3b90cabf355e30257fa0ee05ec6c49f2c2f6ecdf435f0085d84b826098d5`.
`plan.md`, `review.md`, `decision.json`, `measure-results.json` and `self-check.json`
record rejection after the required 64-overview median improvement fails. All
18,000 frame pairs have exact non-timing fields; five production, five bounds-audit
and five submission-inventory image pairs match exactly. `audit-results.json`
records 9,935,964 exact bound comparisons; `submission-inventory-results.json`
records unchanged root inputs across 182 rendered frames/view. All 45 candidate
tests and 43 restored-source tests pass without skips. Frozen runtimes, source,
patches, commands, raw measurements, process/affinity records and annotations
are included. The original profile remains in the shadow-culling archive.
`restoration.json` verifies exact original source/executable restoration;
`baseline-section-check.json` and `harness-corrections.txt` document rebuild
metadata and timestamp checks. No runtime optimization retained or new profile.
External fixture/game/module dependencies are listed in `dependencies.json`;
linked assets are not copied. Payload is 1,218,778,831 bytes; archive is 227,267,364 bytes.

Root-payload archive: 1,286 payload files verified against its manifest; SHA-256
`bdbfac98f094035822b73c917c38a5270b50f1425ab70c2fa8d445c022bbfda8`.
`plan.md`, `review.md`, `decision.json` and `self-check.json` record retention
after both large-view gates and the planned small-overview follow-up pass.
`measure-results.json` preserves the initial 32-overview median failure (+5.89%);
`tail-results.json` records all six passing duplicate-baseline gates, with
2.531/2.476/2.496 ms baseline A/B/candidate medians. All 18,000 main frame pairs
and 7,200 follow-up triples have exact non-timing fields. Five production and
five diagnostic image pairs are exact; 48 tests pass without skips and
4,606,784 live root/normal rows match exactly. Additional retained capacity
reaches 2,473,344 bytes including vector headers, before allocator overhead.
`invalidation-audit.md` records supported edit/publication lifetimes and their
limits. `profile-results.json` has 3,526 main-thread cycle samples, zero lost;
profile/audit timings are excluded from acceptance. Frozen runtimes, source,
patches, raw runs, test results, affinity/process costs and harnesses are included.
`final-source-check.json` verifies exact uninstrumented candidate source and
measured executable restoration after diagnostics. `dependencies.json` records
external game/module/fixture assets; linked asset directories are not copied.
Payload is 2,064,271,636 bytes; archive is 305,404,118 bytes.

Particle-packets archive: 1,190 payload files verified against its manifest;
SHA-256 `3082d6bb342170ddc034abefd7d4f0b1b89e1c128f8f6ff6275acd86cf9ee585`.
`plan.md`, `candidate-plan.md`, `review.md`, `decision.json` and `self-check.json`
record rejection after the required 96-overview median/paired and consistency
gates fail. The 32-overview median also fails its control limit; no control-only
follow-up applies after the large-view failure. All six pairs/view completed,
with 18,000 exact non-timing frame pairs and no extra retries. Five production
and ten diagnostic image pairs are exact. `inventory-summary.json` records
actual packet/frame reuse; `audit-results.json` has 12,077,952 exact live UV
comparisons and 7,864,320 synthetic boundary cases per run, repeated five times.
All 149 candidate and 149 restored-source tests pass without skips. Original,
candidate, inventory and audit binaries/source, raw runs, test results, process/
affinity records and harnesses are retained. `final-restoration.json` verifies
committed source and the frozen original measured executable restored exactly.
No new profile; the root-payload archive's profile still applies. External
game/module/fixture dependencies are recorded; linked assets are not copied.
Payload is 1,195,171,772 bytes; archive is 215,460,346 bytes.

Particle-compute archive: 530 payload files verified against its manifest;
SHA-256 `3a7c443af2c4502677b854efa7e09d3ce484136bf8cc77ceb9d44cc633fdfed5`.
`plan.md`, `decision.json`, `screen-results.json` and `self-check.md` preserve
the Tier 2 contract, unchanged gates and rejection after the small-local host
median rises 10.44%. The 96 particle CPU phase improves 28.20% and every overview
host pair improves; no full matrix or retries. Five image pairs stay within
predeclared bounds, with at most 20 pixels changing by one level. All 4,500 timed
frame pairs match logical work/draws. The frozen candidate source/executable and
shaders, reference runtime, initial fixture/harness failures, test logs, raw runs,
affinity/background costs and exact restoration checks are preserved. The indexer
was resumed. Per-arena retained capacity and full-client behavior remain unverified.
External local game/module/library dependencies are in `dependencies.json`;
linked game asset directories are not copied. Payload is 395,898,075 bytes;
archive is 76,191,861 bytes.

Particle-projection archive: 688 payload files and the canonical copy verified;
SHA-256 `c5bfe21c0ee29975c506337bbeda7ed2956cb5fef58e38fa516613f0d6a87f87`.
`plan.md`, `decision.json`, `inventory-analysis.json`, `capture-analysis.json`,
`outside-batch-analysis.json` and `self-check.md` record the unchanged quality/
removal gates and limits. Fifteen observation-only image pairs and 610 non-timing
frame pairs match exactly; 36 removal captures have exact packet/instance
accounting and unchanged non-particle work. Thirty-four size cases have no eligible
snapshot rows. No cutoff passes both useful-removal and negligible-difference
criteria; no instrumented timing is a latency claim. Frozen baseline/probe runtime
and source, raw commands/CSVs/images, crops, original harness failures and corrected
artifact mapping, 149 restored tests and exact restoration checks are included.
Dependencies are recorded; linked game assets are not copied. Payload is
533,925,819 bytes; archive is 227,684,083 bytes.

Stretched-cull archive: 653 payload files and the canonical copy verified;
SHA-256 `b00efbb227d62817483ab7992099e7acf3bbcb9637d97f9b885e48a843ae6b41`.
`plan.md`, `review.md`, `screen-results.json`, `decision.json`, `result.json` and
`self-check.json` record rejection at the three-pair screen, without extension or
retry. Local-96 particle median changes 0.740 → 0.747 ms, one of three pairs
faster; three host-p95 controls exceed +5%. Ten candidate and five audit image
pairs are exact; all 9,000 timed frame pairs have matching expected removal
accounting and unchanged other logical work. The audit verifies all 953,920
rejected generated quads outside a clip plane. Original/candidate/audit source,
executables, frozen resources, commands, raw CSV/images/timings, process/affinity
records, initial compile failure and red/green tests are included. All 151
candidate and 149 restored tests pass; `restoration.json` verifies original source
and exact frozen runtime/resources. The indexer was resumed. Bounds versus
presentation cost and full-client pacing remain unverified. External game/module/
library dependencies are recorded; linked game assets are not copied. Payload is
939,998,354 bytes; archive is 194,520,581 bytes.

Direct vertex archive: 648 payload files verified against the per-file manifest;
SHA-256 `31980d134ec6327ce48ccf7d55eef90ad9dfb5aee8e27511e3f4bb631779da41`.
Canonical copy verified. `gpu-trig/` holds the initial numerical-difference trial;
`cpu-trig/` holds its exact-image correction, three-pair screen, decision,
self-check, restoration checks and 173 passing restored tests. The correction
reduces overview-96 host 18.872 → 17.265 ms, but large-local median/p95 rise
6.21%/11.92%; rejected with no six-pair extension or timing retry. Both source
variants, frozen runtimes, all captures/reports, scripts, baseline, interrupted
sandbox attempt and background records are retained. The GPU boundary-test
snippet was uncompiled/unrun in that archive; the renderer-controls archive below
records its later execution. Full-client acceptance remains unverified.
The archive links its shared reference internally; external game,
module and library dependencies retain their recorded absolute paths.

Particle-attribution archive: 411 payload files and canonical copy verified;
SHA-256 `cd393758fe072a47a4a59152d5e1042539ada3f1e3b319fb28b86fc972cba5d9`.
`plan.md`, `RESULT.md`, `capture-checks.json` and `diagnostic-results.json` record
the fixed 18-run attribution and its limitations. Original, CPU-expanded candidate
layout and GPU-expanded layout rotate in 32-frame blocks; three label rotations,
frozen/animated inputs and four-frame transition exclusions preserve comparable
logical work. Thread CPU, context switches, uploads and active arena capacity are
recorded. These instrumented timings do not accept the rejected candidate or
establish a batch threshold. Probe source/runtime, original source and baseline
runtime, full raw data, process/affinity/guard records and 175 restored tests are
included. The indexer is resumed and no diagnostic remains in production.
Game/module/library dependencies are recorded; linked game assets are not bundled.
Payload is 912,532,462 bytes; archive is 151,810,779 bytes.

Particle-material archive: 528 payload files and canonical copy verified;
SHA-256 `114b69ace818d80e63836773a15314de9e6da3f5edd4134ef925f03912bd55e1`.
`plan.md`, `RESULT.md`, `decision.json`, `screen-results.json` and
`restoration.json` preserve the fixed screen and rejection without retry.
Borrowing only texture/mesh names removes confirmed Release material/string
copy paths, but local96 particle phase changes +1.29% and overview32 host
median/p95 rise 8.94%/15.04%. Ten images and 9,000 timed logical/physical
render-counter pairs match exactly; all 175 candidate and restored tests pass
without skips. Both frozen runtimes, original/candidate source and disassembly,
full raw captures/timings, process/affinity/guard records and exact restoration
checks are included. The indexer is resumed; no production change remains.
External game/module/library dependencies are recorded. Payload is 652,540,127
bytes; archive is 131,019,448 bytes.

Renderer-controls archive: 453 payload files and canonical copy verified;
SHA-256 `aeb01804e80e61ac0010d308d660c63812d411363fbd374d20fe386a974323c6`.
`plan.md`, `RESULT.md`, `measure-results.json`, `gpu-boundary-summary.json` and
`restoration.json` preserve the fixed controls, numerical findings and limits.
Main CPU2 with workers on 2,0,1,3 versus their original full 24-CPU mask: unchanged
code still exceeds 5% A/A repeatability under both placements, including serial
tails. No rejected candidate was retimed or accepted. All 21,600 measured frames
preserve logical/physical counters and three capture pairs are byte-identical.
The vertex-function compute wrapper finds 126 differing U values on 3×2 sheets,
maximum 5.96e-8; removing insets reproduces them. This does not establish visible
regression. Sources/test binaries, raw values/failures, full timing/process/affinity
records and dependencies are retained. Seven source files, executable and 28
shaders restore exactly; 175 selected tests pass, zero skips. The indexer resumed.
Payload is 805,620,129 bytes; archive is 145,502,002 bytes. Game/module/library
dependencies remain external; the archive includes their recorded paths/hashes.

Particle-vertex-uv archive: all 1,731 payload entries (1,722 regular files) verified;
SHA-256 `59843aef9ca2d3b62b2248e294d8fc0ae459088c87114250afeb2b0ef5a5b851`.
`result.md`, `retry/measure-results.json`, `restoration.json` and
`final-checks.json` record the fixed decision and limits. `failed-cohort/`
preserves source/runtimes, validation, the original plan and the 60-start storage
abort. Its samples are excluded; `retry/` preserves a fresh complete 160-start
cohort with the identical seed/order, binaries, workloads and gates. Overview96
gains are established; overview32/local32 p95 upper bounds +6.68%/+6.85% fail the
declared +5% guardrail without proving a >5% slowdown. No timing-based retry.
All raw frames, paired effects, process/affinity/guard observations, captures and
the corrected two-slot vertex test are included. Only the declared uniform-byte
delta differs in matched counters. Production source, executable and 28 shaders
restore exactly; 175 selected tests pass with zero skips. The indexer resumed.
Module-file and shared-library hashes are recorded; licensed game data remains
external. Payload is 3,826,909,640 bytes; archive is 225,897,354 bytes.

Gameplay-activity archive: all 5,827 payload entries (5,780 regular files) verified;
SHA-256 `7de6f785aefd43869fb3764d826467354b6de10709000909f598791bd3ab650d`.
`plan.md`, `result.md`, `checks.json` and `restoration.json` record the scope.
The retained test patch, 104-test results, three route-diagnostic failures, four
failed pointer attempts, two successful keyboard runs, six captures, original/probe
sources and binaries are included. Current-client rows verify keyboard activity
and cleanup only; there is no timing or matched-image claim. All three temporary
source hooks and 192 runtime files restore exactly. The copied project is included;
game installation, test corpus and system libraries remain external dependencies.
Payload is 221,821,518 bytes; archive is 57,828,017 bytes.

Common-client-visuals archive: all 8,091 payload entries (7,856 regular files)
verified; SHA-256
`469f664e34aad523256ad6b0feb982168acde5a34163ab7a29c0090873250e60`.
`plan.md`, `result.md`, `checks.json` and `detail1-detail-comparison.json` distinguish
the 15 successful small-view starts from preserved harness/capacity/termination
failures. All 20 reference/no-cull images are exact; current culling remains a
visual acceptance question. No timing cohort starts. Source/reference recipes,
all isolated binaries/shaders, raw rows, captures, process/DRM observations and
restoration verification are included. The source-neutral submission review adds
no runtime implementation. Production source and all 192 runtime files match the
originals; game installation and system libraries remain external dependencies.
Payload is 1,133,481,901 bytes; archive is 425,298,044 bytes.
