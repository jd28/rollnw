# RP-06: Gameplay verification

Status: open. [Parent/results](rendering-performance-overhaul.md).
[Budgets](render-perf-workload-contract.md). [Archived evidence](render-perf-evidence/README.md).

- [x] Full-area editor overview/orbit and actual gate, plaza and outdoor routes.
- [x] Paced complete 32/64/96 overview budget check with production uniform
  capacity (2026-10-14); all three starts/size meet median/p95 ≤33.333 ms.
- [ ] Door closing, representative actor activity and native-input validation.
- [ ] Final/common-baseline comparison: paced frames, CPU/GPU, tails, memory,
  bytes/draws and image repeat variability. Track scoped limits and asset failures.
- [ ] Relevant regressions: movement, doors, edits/undo, reloads and lifetimes;
  shader/pipeline failures must fail capture. Resolve RP-05/08/10 acceptance.
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
Door closing, representative full
activity, native input, paced full-client comparison, total memory and the
gameplay budget remain open; fixed-view checks do not close those gates.

Cost: bounded verification and necessary fixes; no new renderer architecture.

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
hardware or full-client 32/64 views. [Evidence](render-perf-evidence/README.md)
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
were not rerun here. [Evidence](render-perf-evidence/README.md) includes raw
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
optimization ceiling. [Evidence](render-perf-evidence/README.md).
