# Performance evidence

Current work/results: [tracker](../rendering-performance-overhaul.md).
Keep actionable issues concise. One-off plans, reports, probes, logs, binaries
and captures belong outside Git; do not add a README per experiment.

The original 94 performance notes/scripts/issues from `e673bea3d` are preserved,
with original repository paths and a verified per-file manifest, in:
`/home/josh/projects/rollnw-evidence/2026-10-07/repo-performance-notes.tar.gz`.
SHA-256: `e67a30e1e7ae82ad1b8ea63ff88f5748a3b0a8c7d7291aab82f655f32a90fc12`.
Historical script/report links now point here; extract the archive to recover
those files. Maintained benchmark tools remain in `tools/mudl/`.

Raw evidence remains in `/home/josh/projects/rollnw-evidence/`:

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
