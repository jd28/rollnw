# Particle execution across placements

Status: shared-storage migration rejected; Taskflow particle batches retained
with the user-accepted small-view tail tradeoff below.
[Parent](rendering-performance-overhaul.md), [profiles](render-perf-submission-profile.md),
[external evidence](render-perf-evidence/README.md).

## 2026-10-14 Taskflow particle batches: accepted tradeoff

The candidate uses pinned Taskflow 4.1.0 headers privately in `nw-render-viewer`.
A scene lazily owns up to four persistent workers and one reusable graph. Each
job updates at most 64 contiguous system rows through the unchanged serial
arithmetic. Fewer than 1,024 systems stay serial; unknown/single-core hardware
also stays serial. A fixed 1/2/4-worker screen selected four; its 600-system
regression motivated the cutoff before the final cohort. The cutoff is a
conservative assumption between observed 600/2,400-system cases, not an optimum.

Batch contract: `update_scene_particles` borrows stable const scene owner/pose
tables and an exclusive mutable span of independently owned system rows.
Definitions are shared const; clocks, RNG, events, particle columns and output
packets are written only by that row's worker. The renderer consumes the same
rows/order after join. Empty spans do nothing; negative dt clamps to zero;
stale owners keep existing hidden-owner behavior. Edits, resize, pose writes,
rendering and destruction cannot overlap update. Allocation/task exceptions
propagate after completion; partially updated rows are not retried. Executor
destruction joins before graph/owner destruction. No serialized ABI is added.

Access is linear by system index then particle column. Existing definition
pointers and indexed owner handles avoid a separate ownership migration; owner
lookups remain read-only indirections. The viewer's collision callback is null.
The scene's executor is a true singleton; particle transforms remain batches.
Simplification keeps existing storage, arithmetic and output ordering, creates
execution machinery once, and adds no per-particle tasks, copy-back, frame graph,
GPU threading, approximation or public scheduler settings.

Ryzen AI 9 HX 370 / Radeon 890M, Release, 1920×1080, fixed 16 ms simulation.
Six alternating pairs/view, 180 warmup + 600 measured frames: 36,000 complete
frames. Main CPU2; workers CPU2/0/1/3. Medians of per-run statistics:

| View | Host median before → candidate, ms | p95 before → candidate, ms |
| --- | ---: | ---: |
| 32 overview | 2.205 → 2.208 | 3.226 → 3.188 |
| 64 overview | 9.411 → 8.627 | 11.258 → 10.501 |
| 96 overview | 22.336 → 19.741 | 25.045 → 22.258 |
| 32 local | 1.378 → 1.370 | 1.799 → 1.936 |
| 96 local | 7.132 → 4.759 | 9.230 → 6.015 |

All six pairs improve at 64/96 overview and 96 local. The 96 overview tick
median falls 4.896 → 2.214 ms. The original **5% small-local p95 gate fails**
(+7.61%). One predeclared six-start follow-up uses two byte-identical serial
controls: their p95 values 1.984/1.931 ms agree within 5%; candidate is 2.104 ms,
**+6.06%/+8.98%**, failing again. No more repeats to pass. The 5% gate originated
in the experiment plan. The user explicitly chose to keep the change after
reviewing this repeated 0.12–0.17 ms increase and the large-view gains. Retain
the change as an accepted tradeoff; both original failed gates remain recorded.

Cost: each used large scene retains executor queues/graph and up to four OS
thread stacks until destruction, including after it shrinks. No particle arrays
are copied in production. Whole-process 96-overview user CPU increases
20.372 → 22.011 seconds while elapsed falls 21.156 → 19.173 seconds; median peak
RSS is 881,230 → 881,596 KiB. These include loading and are not isolated scheduler
costs or total system/GPU memory. More CPU work buys lower measured frame latency.

244 particle/animation/tile-edit tests pass. Exact-state tests cover cutoff/chunk
boundaries, dt changes, visibility, stale owners, poses, reorder, erase, clear and
repopulation. Three final ThreadSanitizer runs pass; upstream atomic-fence
instrumentation warnings limit what that proves. A separate live audit compares
every field against serial over 184 calls/view, including spawning and RNG;
all five audited images and five production capture pairs are exact. All paired
non-timing counters match. Temporary probes are removed; production hashes match
the measured candidate. The [paced client comparison](render-perf-gameplay-acceptance.md#2026-10-14-taskflow-candidate-replay)
also improves median/p95, with rare deadline misses retained.

Open: attribution of the small-local tail increase; startup latency, other machines and
content near the cutoff, multiple concurrently large scenes, and native gameplay
acceptance. No CPU ceiling or camera restriction follows from this experiment.

## Observed workload

Current Ryzen AI 9 HX 370 / Radeon 890M, Linux Release build. The fresh 96×96
local profile has 1,181 samples and zero loss: particle ticking accounts for
41.38% inclusive sampled cycles, attachment synchronization 11.58% self,
presentation 10.29% self and scalar curves 7.14% self. Shares overlap; they are
not elapsed savings. Complete overview remains dominated by model submission.

The refreshed inventory contains 5,400 systems / 9,504 emitters / 50,796 live
particles, sharing 36 definitions / 74 compiled emitter rows. Exactly 40,032
particles fit the first trial: single-emitter effects, ordinary world-space
sprites, continuous emission, no wind, collision, force events or sidecars.
There are 1,116 eligible systems, reducible to 10 groups by compiled-definition
identity and equal sampled sprite-sheet fields. Eligibility uses data fields,
never model/texture names. The common live group has 40 particles per system.

## Transform and contract tested

The diagnostic captures real particle state after 180 warmup frames plus one sample at fixed 16 ms steps,
then copies the eligible systems into two independent test arms. Both disable
emission. The reference keeps separate arrays; the trial concatenates all 22
core arrays once per group and reuses the existing `tick_particle_system` code.
The arrays persist across measured steps: no per-step gather or copy-back.
This tests consolidation alone, without introducing a new simulation kernel.

The probe owns snapshots and batches until completion; the read-only scene
keeps compiled definitions alive. Particle rows contain indices, and source
indices identify original ranges. One shared definition pointer per batch is
borrowed outside the particle loop. Equal sheet values are required because
that is the emitter state read by this eligible particle-update phase.
Malformed array lengths and unsupported inputs fail the probe; groups split
at 65,536 particles before indices can overflow. Existing endpoint clamping
and stable death compaction are reused. Pooled counts intentionally exceed
individual authored emitter capacities: emission stays disabled, so this is
**not** a replacement for the existing spawn/capacity contract.

All array bytes, counts and concatenated source order are compared after every
step through full drain at 16/33/100 ms. A separate, explicitly synthetic case
moves alternating particles to just before death to exercise compaction.
Original emitter rows remain in the reference snapshots; the pooled update
uses only their equal sheet state. Independent clocks/RNG/events, live spawning,
owner-range maintenance, edits/removal and render submission are not tested by
this transform. No image or complete-frame speedup is claimed for the batch.

## Measurements and decision, 2026-10-13

Six independent starts per scene, main thread pinned to CPU 2 after loading.
Each start has 120 alternating reference/pool pairs per case, each timing 16
updates; reset/copy and parity checks are outside the timer. Medians below are
medians of per-start statistics. These are isolated draining-snapshot costs,
not steady-state frame times; the rendered setup frame is excluded.

| Captured scene | Separate arrays, ms | Shared arrays, ms | Change | Saving, ms |
| --- | ---: | ---: | ---: | ---: |
| 32×32 local, 4,448 particles | 0.09158 | 0.08933 | -2.46% | 0.00225 |
| 96×96 local, 40,032 particles | 0.88938 | 0.81124 | -8.79% | 0.07813 |
| 64×64 overview, 17,792 particles | 0.37282 | 0.35706 | -4.23% | 0.01575 |

All six starts improve in every view. Large-local p95 of the 16-step block
averages falls 1.04230 → 0.88306 ms (-15.28%); this is not a frame-tail result.
The synthetic half-dead large-local case falls 0.49305 → 0.43042 ms (-12.70%).
The predeclared material-gain gate required at least 10% median improvement in
the actual and churn cases, in aggregate and at least four of six starts. The
actual case fails: zero starts reach 10%. The churn case passes in all six.

In the large-local probe, core-array capacity is 5,124,096 bytes for reference
snapshot copies versus 6,815,872 bytes for the growing pooled vectors. This
excludes emitter rows/scratch/graphics and is not production memory usage. The
trial did not optimize pool reservation; it demonstrates no memory saving.

Do not migrate scene/renderer ownership for this result alone. The measured
0.078 ms saving excludes the new work needed to preserve independent spawning,
capacity, ownership ranges and draw order. This rejects storage consolidation
alone as the next migration; it does not reject different particle kernels or
establish a CPU ceiling. No camera cap or approximation was introduced.

Across the 18 measured starts, 34,560 full-drain state comparisons and 4,464
post-block comparisons pass, covering 138,240 timed update steps in both arms.
Three preliminary capture/probe starts are retained but excluded from the fixed
cohort. The diagnostic and inventory source, binaries, raw reports and contract
are archived. Production source and executable were restored exactly.

## Remaining design questions

- How much per-particle presentation work can be removed or computed once from
  the compiled curves? The observed workload still evaluates size, rotation,
  frame and color after pooling. Any specialization needs exact-state tests and
  matched complete-frame measurements; sharing arrays alone does not remove it.
- Can conservative particle bounds permit deferring presentation for invisible
  particles while preserving motion, events and correct re-entry? Owner bounds
  are insufficient for world-space particles; unknown bounds must remain visible.
- If a later kernel justifies shared storage, define independent emitter clocks,
  RNG, spawn clipping, source removal and render-order ranges before migration.
  The current renderer reads per-system arrays; copying everything back each
  frame would add an unmeasured cost and is not an accepted design.

Simplification applied: reuse immutable definitions, existing update arithmetic
and the real snapshots; isolate storage consolidation before introducing a pool
allocator, generations, renderer interfaces or GPU synchronization. The probe
added none of those to production. Any GPU experiment needs its own measured
reason, buffer lifetime/synchronization contract and visual acceptance criteria.
