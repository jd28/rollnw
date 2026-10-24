# Rigid animated shadow batches

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: implemented, provisional. [RP-05](render-perf-draw-submission.md).
Baseline `b86281f9b`. [Evidence](../README.md).

Corrected probe: compatible rigid casters can remove 1,177 directional shadow
draws in max city, 329 in city, zero in outdoor/local lights. Group by actual
asset and primitive/material compatibility; no asset-name policies. The first
probe omitted the range filter on interleaved spans; its counts are excluded.

Reuse the static cache, then group eligible frame candidates using exact caster
indices partitioned by prepared range. Copy current world/normal matrices into
surface-major instance rows; use existing instanced shader with identity primitive
constants. Preserve per-pass culling and scalar handling for skins, deformers,
overrides, invalid input, single visible instances and failed/oversized allocation.

Cost: two linear source scans, 4 bytes/caster index + 16 bytes/prepared range,
transient groups and 128 bytes/visible batched surface. Probe predicts 162,176
row bytes in max city, 48,512 city, zero outdoor. CPU metadata lives for the shadow
call; GPU rows until frame retirement. No new shader, pipeline or retained cache.
Indices address surfaces/candidates; existing shared pointers only compare asset
identity. Invalid source ranges are omitted; oversized source volumes stay scalar.

- [x] GPU regression: 28 byte-exact scalar depth comparisons across two cascades,
  two local maps, seven pose/edit/reload stages; includes interleaved passes,
  partially visible groups, transparency, deformers and invalid spans/spaces.
- [x] Final source: 192 tests, zero skips; 58 captures / nine views, all exact
  or within same-version repeat variability; 18,000 timing / 87 capture frame
  pairs preserve logical counters and the 1,177/329/0 draw reductions.
- [x] [Simplification/self-check](../README.md):
  explicit index/eligibility/grouping stages; 164 net production lines added.

Five alternating pairs, medians of run statistics, ms:

| Scene | Host median | Host p95 | GPU scene median |
| --- | --- | --- | --- |
| Max city | 5.567→5.397 | 6.884→6.905 | 3.348→3.141 |
| City | 1.740→1.689 | 2.387→2.321 | 1.652→1.596 |
| Outdoor | 0.909→0.918 | 1.338→1.371 | 0.884→0.894 |

Max shadow CPU 1.172→0.927 ms; local shadow CPU 0.277→0.295 ms. Keep the
measured reduction provisionally; [tails](render-perf-frame-pacing.md) and
gameplay acceptance remain open. The prior main-pass edge pixels were unchanged
by this batching step; the subsequent [shadow sampling correction](render-perf-draw-submission.md)
resolves them.
Storage arenas retain 2×4 MiB before/after; sampled new index/range capacities
are 12,356/2,864/620 bytes per call. Total/peak memory remains unverified.
