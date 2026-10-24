# RP-06: Gameplay verification

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: open. [Parent/results](../../rendering-performance-overhaul.md).
[Budgets](../../render-perf-workload-contract.md). [Archived evidence](../README.md).

User gameplay requirement (2026-10-19): at least 60 fps for basic NWN1 assets,
16.667 ms per complete frame. Overview remains 30 fps. Measure presented gameplay
frame distributions and misses, not just renderer phase CPU or median throughput.
The subsequent clarification makes 60 fps a floor: NWN1 assets should run well above
it, leaving headroom for Dragon Age: Inquisition / Ryse: Son of Rome art quality.
Passing an NWN1 budget alone does not establish final-art capacity. The user
clarified that glTF fixtures guard asset-format independence and the longer-term
direction is an offline compiler. Current renderer acceptance proceeds with the
available workloads; representative target-art capacity is a future measurement.

- [x] Full-area editor overview/orbit and actual gate, plaza and outdoor routes.
- [x] Paced complete 32/64/96 overview budget check with production uniform
  capacity (2026-10-14); all three starts/size meet median/p95 ≤33.333 ms.
- [x] Replacement-door close/reopen, return navigation and authored-state/actor
  cleanup regression; 104 related tests pass (2026-10-20).
- [x] City/outdoor native X11 keyboard movement, strafe, turn and preview cleanup
  through SDL, with unchanged authored rosters (2026-10-20).
- [x] Controlled full-client door close/reopen, traversal and cleanup against the
  corrected common reference (2026-10-20); synthetic replay, not native input.
- [x] Known culling coverage regression repaired (2026-10-21): 20 exact client
  images across the five replay cases, plus two exact modern glTF images.
- [x] Corrected-reference five-case paced/uncapped comparison (2026-10-21):
  160 starts, complete CPU/GPU/activity/memory records; regression guards pass.
- [x] Large 64/96 completeness and production budgets (2026-10-22): twelve exact
  image pairs; all twelve production starts meet overview median/p95 budgets.
- [x] Final edit/undo, save/reopen, scene reload and lifetime checks (2026-10-22):
  222 additional tests pass; 322 live replay actions restore seven roots exactly.
  Existing exact GPU readback tests cover retained/fresh batches and shader failure/recovery.
- [ ] Native pointer/Wayland validation and broader representative actor activity.
- [ ] Final/common-baseline comparison: paced frames, CPU/GPU, tails, memory,
  bytes/draws and image repeat variability. Track scoped limits and asset failures.
- [ ] Resolve remaining RP-05/08/10 acceptance beyond the verified regressions.
- [ ] Meet agreed workload budgets; earlier 4/2 ms aspirations are not mandatory
  overview gates. Record remaining gaps and defer optional work explicitly.
- [ ] Close required children and parent after verification, with evidence outside Git.

2026-10-08 review: the rigid-batch pixel discrepancy is fixed in shadow sampling.
655 renderer/navigation/preview/area-edit regressions pass, zero skips; this
covers existing movement, door opening, edits/undo, reload and lifetime tests.
24 corrected-shader captures are byte-exact across four fixed views and
14,400 before/after frame pairs preserve non-timing fields. Six additional
material-count cleanup captures resolve its old one-pixel difference exactly.
Costs and scope are in [RP-05](render-perf-draw-submission.md).
Full-client door closing, representative full
activity, native input, paced full-client comparison, total memory and the
gameplay budget remain open; fixed-view checks do not close those gates.

Cost: bounded verification and necessary fixes; no new renderer architecture.

## 2026-10-22 final editor and lifetime checks

214 document/edit/preview/navigation/animation tests and eight isolated SDL runtime
tests pass without failures/skips. The unchanged 236-test renderer coverage result
is reused, including exact retained/fresh GPU comparisons across edits, replacement,
clear/rebuild, animated poses and shader failure/recovery.

The full client completes 322 synthetic edit/undo actions across seven scenarios,
restoring every serialized root exactly. Save/reopen restores the edited area;
restoring and saving the original tile returns the disposable file to identical
JSON. Three replacements release old area roots and 40 child handles. Scene
reload preserves the document and all 1,024 models. Selection and item DOM survive
the edit/undo replay. External fixtures remain unchanged.

Three images visibly confirm the cloth color edit and undo; different frame
poses/UI prevent an exact-pixel claim for these captures. No Vulkan validation
errors occur. Known invalid plc_d07 geometry, missing-resref fixture rows and
empty optional resource containers limit asset coverage; all error lines remain
in the report. This is synthetic SDL-queue input, not native pointer validation.

The first lifecycle adapter checked the document before the normal frame loaded
it; its failed run and source are preserved. Correcting the diagnostic's ordering
completed the replay without a production change. Both source hooks and all 192
production runtime files are restored exactly. Evidence is staged in
`build/render-client-final-checks-2026-10-21/`; this check claims no speedup.

## 2026-10-22 large-view correctness and production budgets

Six capture starts complete with all expected models, systems and chunks: twelve
full-RGBA comparisons and 4,800 logical pairs match exactly. This includes both
corrected-reference/current comparisons at equal 96/8 MiB diagnostic capacities
and current diagnostic/production comparisons at production 32/2 MiB capacities.
No production capacity increase is retained.

All twelve production starts finish: three per size/cadence, 180 warmup + 1,200
measured frames. All 14,400 rows preserve complete workloads and outer GPU samples,
with no recorded dropped work or allocation/binding/submission/upload/tick errors.
Every start meets the 33.333 ms overview median/p95 requirement. Medians of three
per-run statistics, milliseconds:

| Stress overview / cadence | Host median | Viewport GPU median | Interval p95 | Over budget / 3,600 |
| --- | ---: | ---: | ---: | ---: |
| 64 / paced | 9.153 | 2.274 | 16.808 | 0 |
| 64 / uncapped | 9.425 | 2.250 | 10.486 | 0 |
| 96 / paced | 21.004 | 4.651 | 24.966 | 10 |
| 96 / uncapped | 21.078 | 4.649 | 26.244 | 26 |

The fitted scenes contain 6,720/15,120 model records and 2,400/5,400 particle
systems. These costs are not normal gameplay camera costs or final-art capacity.
Worst 96-view intervals are 37.555/37.934 ms; all outliers remain included. The
intervals are application timing, not physical presentation. Uncapped removes
only application sleep. Process and DRM memory observations remain separate.

The planned historical timing comparison is **incomplete**: after nine complete
starts, the next reference96 process returned zero without terminal rows. No run
was restarted or substituted. A single separate SDL quit-observer diagnostic
completed all rows with no quit event, so the earlier exit cause remains unknown.
The independent production checks above do not complete that paired comparison.

Source/controller/runtime hashes and exact production restoration pass. Evidence:
`build/render-client-large-2026-10-21/production-result.md`, `production-results.json`,
`incomplete.json`, raw captures/runs; the diagnostic is separately labelled under
`build/render-client-exit-2026-10-22/`. No new speedup is accepted from this cohort.

## 2026-10-21 completed small-view common-reference comparison

Eight adjacent alternating pairs for gate, city, outdoor, overview32 and orbit32,
each paced and uncapped, finish without restarted/discarded runs. All 192,000
measured frames have complete outer GPU samples; all 96,000 directly paired
logical rows match. No dropped/failed submissions, ticks, allocations, bindings,
uploads or paths. Input, frozen-runtime and production-runtime hashes match.
The preceding coverage cohort supplies the 20 exact client/two exact glTF images;
these timing runs have no capture or validation-layer overhead.

| Uncapped case | Host median reference → current (ms) | Host p95 (ms) |
| --- | ---: | ---: |
| Gate | 3.226 → 1.548 | 4.295 → 2.449 |
| City | 4.248 → 2.304 | 5.597 → 3.174 |
| Outdoor | 1.037 → 0.956 | 1.888 → 1.608 |
| Overview32 | 6.281 → 3.215 | 7.567 → 4.264 |
| Orbit32 | 6.148 → 3.102 | 7.427 → 4.170 |

Values are medians of eight per-run statistics. Every host-median pair improves;
all 40 declared host/interval median/p95 +5% guards pass using the predeclared
individual one-sided paired bounds. Baseline spread remains material and is
reported; these cumulative results do not identify historical component-tail
causes or establish final-art capacity. No percentages are added across cohorts.

Current uncapped runs have zero budget misses. Paced gate/city/outdoor interval
p95 is 16.853/16.831/16.787 ms, failing the 16.667 ms gate in all eight runs each;
990/1037/880 of 9,600 intervals exceed budget respectively. Overview/orbit32 pass
their 33.333 ms median/p95/p99 gates but each has one rare miss. Current host
stalls up to 137.266 ms are retained, with causation unresolved. These are
application intervals under fixed 16 ms replay, not physical scanout latency.

Gate preserves 40 two-tile rebuilds per variant: rebuild median 2.446 → 2.586 ms
(no navigation speedup), corresponding host median 6.097 → 4.661 ms. Peak RSS,
PSS and deduplicated DRM regions are reported separately, without adding
overlapping memory counters. Full results, every pair, background activity,
monitor cost and manifests are staged in
`build/render-client-acceptance-2026-10-21/result.md` and `timing-results.json`.
Large views, pacing and final edit/lifetime/native-pointer gates remain open.

## 2026-10-21 coverage repair verification

The [common material repair](render-perf-backface-culling.md#2026-10-21-independent-coverage-repair--retained)
matches all 20 reference RGBA images across gate close/reopen, moving city/outdoor
actors, overview32 and orbit32. All 6,000 checked activity rows match and report
no dropped/failed work. Textured and animated glTF captures remain exact, and 236
affected tests pass without skips. Known asset failures and four existing Vulkan
interface warnings per client case remain; there are no Vulkan errors.

These controlled captures close the known coverage discrepancy, not paced-frame
performance or native pointer acceptance. Separate standalone timing measures the
cost of restoring coverage against the visually regressed build; it cannot be
used as a new gain or a complete-client budget result. Large reference-view
completeness, gameplay headroom, memory and edit/lifetime acceptance stay open.
All three temporary client sources and production runtime files are verified
restored to the uninstrumented repair. Evidence is staged under
`build/render-material-coverage-2026-10-21/`.

## 2026-10-20 common-reference visual checks

Corrected `b5aa7cc78` and current code share shader/pipeline rejection, shadow
sampling, idle-door hold, nav-boundary and capacity corrections. Gate, moving city
and outdoor actors, full 32 overview and orbit preserve all checked logical rows.
Three variants/case complete 18,000 checked frames with no recorded submission,
allocation, path, upload or dropped-tick failures. Gate runs each record three
opens, two closes and two traverse/return cycles, with actor cleanup and unchanged
authored creature counts. This verifies controlled full-client behavior, not
native pointer input.

The [color-culling diagnostic](render-perf-backface-culling.md#2026-10-20-full-client-visual-attribution)
restores exact reference pixels in all 20 captures. Current culling still changes
visible pixels, so visual acceptance remains open. Earlier dark-actor/marker
concerns also occur in the reference. No timing cohort starts: capture/validation
timings are excluded. Recorded 32-overview median uniform allocation falls from
9,615,144 to 1,234,424 bytes/frame with 1,680 visible records; this is submission
work reduction, not a newly measured frame-time gain.

The early reference64 exceeds its 2 MiB descriptor capacity, dropping 2,495 draws
per checked frame; that run is invalid. Equal 96 MiB uniform/8 MiB descriptor
diagnostic variants are built for large scenes, but reference96 exits normally
after captures 180/780 and before terminal rows. The cause is unresolved and no
large-view completeness claim follows. Both failed attempts are preserved.
Production remains at 32 MiB/2 MiB, with all 51 source replacements and 192 runtime
files restored exactly. See [evidence](../README.md),
`2026-10-20/common-client-visuals.tar.gz`. Performance, memory, repeatability and
remaining parent gates stay open.

## 2026-10-20 activity checks

The replacement-door regression explicitly closes and reopens through the indexed
command, verifies affected tiles and published visual transitions, then verifies
return navigation and detached-actor cleanup. Its original return-route assumption
was incorrect: the fixture permits walking around the closed door. Three diagnostic
failures are preserved; production behavior is unchanged. All 104 selected
preview/navigation/animation/PC-input/runtime-input tests pass, zero skips.

Two real-clock, normally paced client runs verify W/S/Q/E/A/D through XTest → SDL
acquisition → translation → fixed ticks, with both X and WM focus checked before
each key. City/outdoor retain 1693/591 models and 600/16 particle systems across
600 checked frames each. Stop removes the actor, restores 1680/578 models and
preserves authored creature counts 0/2. No checked draw/dispatch/allocation/bind/path
failures or dropped simulation ticks. Both logs have zero Vulkan errors and four
known unused shader interface/attribute warnings. Known asset failures remain
`plc_s03`/`plc_d07`.

Four failed pointer attempts remain separate. The last preserved keyboard focus
but SDL reported mouse focus zero and incorrect local coordinates, so no click was
sent. The passing keyboard runs send no pointer clicks. Native pointer and the
SDL Wayland backend remain unverified. Six captures show active preview and return
to the editor; they are not a matched image comparison. Check the dark actor body
and city markers against the common baseline before visual acceptance.

No timing or memory gain is claimed: these runs include screenshot/validation
instrumentation, and the per-scene GPU-availability field is false throughout the
600 steady rows. All three temporary source hooks and 192 runtime files are
restored exactly. Only the regression extension remains. [Raw evidence](../README.md)
includes failed attempts, commands, sources, binaries, rows, captures and restoration.
Final/common-baseline performance, visual and activity acceptance remains open.

## 2026-10-19 compact billboard acceptance limit

The [48-byte GPU expansion trial](render-perf-particle-presentation.md#cpu-atlas-rectangles-with-gpu-expansion-2026-10-19--not-retained)
passes exact image/GPU-output checks and establishes a large-overview gain across
16 pairs. It fails the declared small-view host-p95 uncertainty bounds, so the
baseline is restored with 175 selected tests passing. The 96,000 measured frames
are standalone host/GPU work; their budget-miss counts do not establish presented
gameplay, active actors/doors, native input, final-art capacity or total memory.
No required full-client acceptance item is closed by this trial.

## 2026-10-14 Taskflow candidate replay

The [particle change](render-perf-particle-execution.md#2026-10-14-taskflow-particle-batches-accepted-tradeoff)
is retained with explicit user acceptance of its small-local tail tradeoff. Independent
validation uses three alternating 96-overview pairs, 180 warmup + 600 measured
frames/start, identical existing replay driver, fixed 16 ms simulation/time
11.25, normal pacing, 1920×1080 window and 1101×986 viewport. Main CPU2; the four
workers retain the full 24-CPU affinity mask for OS placement. No updater process
was found at either endpoint of these six runs; this is not proof of an idle host.

Pooled interval median/p95 improves **26.334/28.773 → 22.712/25.077 ms**. All three
paired medians improve (median paired change −14.22%). Both versions pass 30 fps
median/p95 in every start. Intervals above 33.333 ms increase **2 → 6 per 1,800**;
candidate's six misses occur in one start, maximum 36.654 ms. No hard-deadline or
cross-session speedup claim; compare this cohort's matched reference.

All 3,600 timing frames retain 9,216 chunks / 15,120 records, with no allocation,
binding or dropped-draw failures; paired non-timing fields match exactly. The
separate capture pair differs at 492 pixels, all live timing text in rectangle
(1093,99)–(1351,121); the scene and remaining UI match exactly. Vulkan reports no
errors and the same four unused shader interface/attribute warnings. The known
`plc_s03` asset import failure persists. Temporary replay/audit hooks are removed;
installed production binaries match the frozen measured candidate.

This fixed overview does not exercise native input, active actors/doors, other
hardware or full-client 32/64 views. [Evidence](../README.md)
preserves raw runs, source, hashes, affinity and whole-process CPU/RSS observations.

## 2026-10-14 sort-reuse replay under concurrent load

[Sort reuse](render-perf-draw-submission.md#2026-10-14-validated-surface-sort-reuse)
passes its separate six-pair standalone gates. Two fixed three-pair 96×96 paced
client cohorts (180 warmup + 600 measured frames/start) do **not** establish an
isolated full-client gain. An unrelated data updater started during the first
cohort. A one-off empty process snapshot incorrectly suggested it had exited;
the follow-up's per-run host process monitor proves it was still active. Preserve
both cohorts and this correction; no process was stopped or reprioritized.

First cohort pooled interval median/p95: original **29.873/39.088 ms**, candidate
**26.233/30.408 ms**; intervals >33.333 ms are 362/1,800 versus 30/1,800.
Follow-up: original **32.902/44.134 ms**, candidate **30.726/36.859 ms**;
811/1,800 versus 343/1,800 misses. Median paired host changes are −12.67% and
−8.52%, but varying concurrent load prevents attributing those differences solely
to sorting. The follow-up candidate fails the p95 budget; no new clean-client
acceptance claim. The earlier scoped capacity check below remains historical.

After the user reported completion, a final fixed three-pair cohort records
updater CPU ticks over every complete process: **9.05–10.62% of one core**,
substantially lower and steadier. Pooled interval median/p95 is original
**30.631/43.078 ms**, candidate **29.770/39.540 ms**; misses are 305/1,800 versus
226/1,800. Two of three pairs improve; median paired host change is −3.55%.
The p95 budget still fails. Remaining variation is not attributed to the updater:
viewport elapsed medians vary 27.734–31.238 ms, while begin/present medians stay
0.165–0.181 / 0.074–0.085 ms. A consistent low-variance full-client acceptance
result remains open; no CPU ceiling inferred from these runs.

All 10,800 measured frames remain complete and every paired non-timing field
matches. The separate capture pair differs at 627 pixels confined to live FPS/
timing text; scene and remaining UI are exact. Both have zero Vulkan errors and
the same four unused shader interface/attribute warnings; the known `plc_s03`
asset failure remains. 32/64 client views, native input and actor/door activity
were not rerun here. [Evidence](../README.md) includes raw
frames, process observations, captures and the temporary replay sources.

## 2026-10-14 paced complete overviews

The capacity checkpoint meets the user's 30 fps overview budget at median and
p95 for the tested 32/64/96 city fixtures on Ryzen AI 9 HX 370 / Radeon 890M.
No camera/content restriction or LOD was introduced. Three starts/size, 180 warmup
+ 600 measured frames, production pacing, main thread CPU2, fixed 16 ms simulation
steps, area time 11.25, fit then pitch-down scale 8. Physical window 1920×1080;
viewport 1101×986 with default UI/overlay/debug options. This is a fixed overview
replay, not native-input, real-time gameplay or display scanout latency evidence.

| Overview | Host work median¹ | Paced interval median / p95² | p99 / maximum² | Intervals >33.333 ms |
| --- | --- | --- | --- | --- |
| 32×32 | 4.318 ms | 16.061 / 16.835 ms | 17.338 / 19.009 ms | 0 / 1,800 |
| 64×64 | 12.295 ms | 16.071 / 17.572 ms | 19.439 / 23.414 ms | 0 / 1,800 |
| 96×96 | 26.884 ms | 27.010 / 30.731 ms | 34.231 / 37.220 ms | 23 / 1,800 |

¹ Median of the three run medians. ² All 1,800 intervals pooled per size.
Each individual 96 run also passes p95: 29.809 / 30.200 / 32.446 ms. The third run
has 21 of the 23 misses; this is not a hard-deadline guarantee. Total UI/viewport/
overlay GPU scope medians are 1.755 / 3.089 / 5.185 ms (median of run medians).
Nested viewer scene GPU timers are unavailable in these client rows; their zero
fields are excluded, not reported as measured GPU time. Peak process RSS ranges
are 495,888–496,864 / 626,616–631,976 / 836,788–837,396 KiB; these include project
loading and are not total system/GPU memory accounting.

All 5,400 measured frames show all 1,024 / 4,096 / 9,216 chunks and all cached
records, with no culling, uniform/descriptor allocation, binding or draw failures.
Full-frame aligned uniform demand is 6,395,464 / 13,144,648 / 24,393,288 bytes;
descriptor demand is 475,088 / 974,096 / 1,805,776 bytes. The selected 32 MiB uniform
capacity fits, as does the existing 2 MiB descriptor capacity. Three separately
validated captures were visually inspected: no Vulkan errors, four unused shader
interface/attribute performance warnings per run. The known `plc_s03` import
failure remains; these results cover the fixture's successfully loaded assets.
Validation captures are excluded from timing.

The temporary replay and capture hooks were removed; restored production client
and mudl hashes exactly match their frozen pre-probe 32 MiB builds. 109 renderer
tests pass. [RP-04](render-perf-uniform-descriptors.md) records the capacity cost,
five exact image comparisons and 18,000 matched standalone frames, including
the unresolved 64-overview p95 increase. Earlier small-view tail gates, native
input, door closing, actor activity, larger areas and final common-baseline
acceptance remain open. This establishes a scoped budget result, not a CPU
optimization ceiling. [Evidence](../README.md).
