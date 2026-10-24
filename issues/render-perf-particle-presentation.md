# Particle render preparation

Status: open GPU experiment; CPU axes-hoisting trial rejected. Baseline `34e296313`.
[Parent](rendering-performance-overhaul.md) / [profile](render-perf-submission-profile.md).

## Current evidence

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

## Next experiment: GPU particle preparation

The CPU currently creates 80-byte billboard records: center, right/up vectors,
UV rectangle and color. The existing vertex shader expands these into quads.
Rotation, size, atlas coordinates and color adjustment could instead execute
on the GPU. This is a hypothesis; no GPU implementation or saving is claimed.

Reuse `ComputePipelineDesc`, storage spans and `cmd_barrier_compute_to_graphics`;
Forward+ already demonstrates this compute-to-draw path. Particle preparation
currently happens inside the drawing pass, so compute needs an earlier batch
stage and frame-owned output storage. Keep existing simulation, packet order,
CPU transparency sorting and resource selection for the first trial.

- [ ] Inventory the raw input fields, packet sizes and packing/upload cost for
  ordinary billboards. Define flat input/packet/output layouts, ownership and
  checked capacity bounds before implementing the Tier 2 preparation stage.
- [ ] Compare a batched compute path against direct vertex-shader preparation.
  Compute pays dispatch, output-buffer traffic and a barrier; vertex preparation
  repeats arithmetic across quad vertices but avoids that intermediate buffer.
  Keep outputs on the GPU; avoid one dispatch per small packet or CPU readback.
- [ ] Define numerical/image tolerances and test changing cameras, rotations,
  atlas edges, transparency order, unsupported modes and allocation failures.
  CPU/shader arithmetic equivalence must be measured, not assumed.
- [ ] Measure whole frames, CPU preparation, GPU time, upload/output bytes and
  small/local controls. The last accepted 96 cohort has 17.907 ms host / 5.071 ms
  GPU medians; their overlap and this UMA platform prevent treating the difference
  as a guaranteed budget. Full-client pacing remains separate acceptance.

Evidence: `/home/josh/projects/rollnw-evidence/2026-10-17/particle-axes.tar.gz`.
SHA-256: `6cc35d502bcae2e6b7a5683ca217eb6b3982e085e8ed419c4a3037d2f8a0530d`.
Verified all 563 payload files (877,519,751 bytes; archive 151,951,268 bytes).
The bundle preserves plans, profiles, inventory/parity sources and executables,
reference/trial runtimes, all commands/results, restoration and the self-check.
