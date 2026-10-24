# Exact draw-transform reuse

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: retained; all declared gates passed. Baseline `e070e8c2a`.

Draw preparation recomputed normal matrices and world bounds even when their
inputs were unchanged. Two fresh profiles put this path at 12.62/13.29% of
main-thread self cycles (6,680 samples, zero lost). The live inventory found all
32,688 dynamic rows unchanged in every subsequent frame of a 182-frame 96×96
overview run; the smaller overviews and local controls behaved similarly.

The prepared-draw owner now retains these numeric results by output position.
Reuse compares the complete world matrix and local bounds byte-for-byte.
Changed/new rows use the original arithmetic; current material, visibility,
shadow and ordering metadata remain fresh. No model identity or asset lifetime
is retained. Existing batch/vector ownership and clear/reuse conventions apply.

## Matched results

Linux Release/O3 Vulkan, Ryzen AI 9 HX370 / Radeon 890M, 1080p, unchanged
cameras/content/step/capacities/affinity. Six alternating pairs per view, 180
warmup + 600 measured frames. Values are medians of run medians, milliseconds;
paired percentages are medians of within-pair changes.

| View | Setup before → after | Host before → after | Paired host change | GPU before → after |
|---|---:|---:|---:|---:|
| Overview 32 | 0.415 → 0.358 | 2.190 → 2.053 | -5.50% | 0.898 → 0.896 |
| Overview 64 | 1.976 → 1.707 | 8.029 → 7.810 | -2.75% | 2.791 → 2.831 |
| Overview 96 | 4.825 → 4.305 | 18.704 → 17.907 | -4.00% | 5.107 → 5.071 |
| Local 32 | 0.119 → 0.122 | 1.363 → 1.374 | +1.64% | 1.316 → 1.329 |
| Local 96 | 0.588 → 0.564 | 4.937 → 4.840 | -2.56% | 2.064 → 2.074 |

The 96 overview improves in all six pairs. Setup improves 10.77%; host saves
0.797 ms between aggregate medians, and p95 falls 22.080 → 20.653 ms.
Small-local host median/p95 rise 0.78%/3.45%, within the predeclared 5% limit.
All five host median/p95 and GPU median limits pass. Absolute timings differ
from the preceding session: compare variants within this cohort, not across
sessions. This does not establish consistent 60 fps or a CPU ceiling.

Cost: 176 bytes per retained row; 5,753,088 bytes (5.49 MiB) of active cache
payload at 96, plus vector capacity slack/header. Capacity persists across clear;
production vector capacity was not separately instrumented. Process peak-RSS
medians are 883,418 → 887,996 KiB; this includes startup/report storage and
allocator variation, not just the cache. Every row still pays exact comparisons.

## Verification and limits

- 125 targeted renderer/prepared-draw/shader tests pass, no skips or Vulkan VUIDs.
  The new regression covers root/pose/bounds/material/shadow changes, reordered
  and resized inputs, invalid geometry, hidden rows and explicit clear/reuse.
- Five capture pairs are pixel-identical. All 4,500 screen and 18,000 final
  frame pairs match every non-timing field with no submission/validation failures.
- Production source and executable match the measured candidate exactly.
- Initial baseline setup stalled in desktop-indexer status queries before any
  renderer frames. That attempt supplies no timing data. The indexer was resumed.
  Timed runs subsequently used a verified OS stop and a cleanup resume; no
  builds/tests/profiles overlapped timings. No timing runs were discarded.
- Moving-camera/fully-changing-pose latency, first-fill/edit latency percentiles,
  other hardware/backends and full-client gameplay remain unverified. Correctness
  under changed inputs is tested; those tests are not latency measurements.

Simplification: retain only pure arithmetic outputs; no pose dirtiness protocol,
new worker/dependency, camera restriction or approximation. Full-scene collection
delegates to the existing span batch. Existing validation/error paths are retained.

The full pre-implementation plan, self-check, probes, frozen runtimes/fixtures,
profiles, commands, raw reports, comparisons and tests are in the evidence bundle.

Archive: `/home/josh/projects/rollnw-evidence/2026-10-16/draw-transforms.tar.gz`.
SHA-256: `f215123a2c456b0648645f57dcfda8a58c1f0c6f9651617fb41773d491c2f9cd`.
Verified all 1,189 payload files (1,829,665,892 bytes; 257,488,871 compressed).
