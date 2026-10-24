# Rigid animated surface batches

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: open; implementation retained provisionally. [RP-05](render-perf-draw-submission.md).
Baseline `efdfbef0a`; raw plan, comparisons and diagnostics are in the
[evidence archive](../README.md).

Reuse compatible model runs. Animated unskinned/nondeforming opaque/cutout runs
upload prepared world/normal matrices in surface-major slices; root batches keep
their existing rows. Explicit transform-space enum selects identity primitive
constants for world rows. No shader/layout or persistent-cache change.
Invalid runs and failed/oversized allocations retain scalar handling; invalid
space/span inputs reject. Max adds 274,304 bytes of transient rows in 18 groups.

V1: five alternating pairs, 180 warmup / 1,200 advancing frames, 1080p, Release,
Tracy off, Ryzen AI 9 HX 370 / Radeon 890M. Medians of per-run statistics:

| Scene | Host median ms | Host p95 ms | GPU scene median ms |
| --- | ---: | ---: | ---: |
| Max city | 6.176 → 5.728 | 8.286 → 7.552 | 3.530 → 3.266 |
| City | 1.869 → 1.797 | 2.925 → 3.032 | 1.682 → 1.641 |
| Outdoor | 0.923 → 0.950 | 1.717 → 1.653 | 0.889 → 0.898 |

Max removes 2,042 draws; city 364; outdoor zero. 18,000 paired frames preserve
all non-timing fields except the explicitly checked command/submitted-run changes.
Final V2: 191 tests passed, zero skipped, using the full desktop installation.
The initial final-run attempt used incomplete repo assets (one failure, four
skips); the full-install rerun resolved all five. Expanded GPU test covers both lighting models, camera/pose
changes, overrides, visibility, deformers, transparency and shader reload with
exact scalar-reference pixels.

- [x] V2 removes redundant instance lookup, keeps group size and checks space
  after model identity. Five more pairs preserve the same 18,000 frame invariants.
  Max host median 6.227→6.025 ms (wins 5/5 pairs), opaque CPU 1.250→0.960 ms,
  GPU scene 3.559→3.363 ms. Max host p95 7.421→7.645 ms; city host medians mixed,
  outdoor opaque CPU 0.439→0.431 ms. These are separate sessions, not additive gains.
- [x] Resolve the three city edge pixels (1585–1587,767), RGB delta up to 25:
  DXC's unrolled shadow filters lost `NonUniform` on later texture loads.
  Resolve the texture once before each filter loop. The original shader fails
  an analytic GPU test at 2,048/4,096 pixels; the correction passes exactly.
  On 2026-10-08, 24 captures (three repeats × batched/split × overview, plaza,
  legacy city edge and elevated outdoor) are byte-exact, including repeats.
  655 regressions pass with zero skips; shader reloads, poses, overrides and
  fallback cases remain covered. This resolves the reported batch discrepancy
  without changing eligibility or using an asset-name exception. Historical
  scalar images can share the shader defect; they are not an absolute oracle.
- [ ] Resolve inconsistent whole-frame tails in [RP-08](render-perf-frame-pacing.md).
- [ ] Measure retained GPU arena capacity and complete [gameplay acceptance](render-perf-gameplay-acceptance.md).

Cost: max adds 274,304 transient row bytes and removes 2,777,120 uniform bytes
written per frame; this is not a measurement of total retained memory. City adds
52,608 row bytes; outdoor adds none. No new shader, pipeline, geometry or cache.
Simplification: reuse whole-run groups and prepared matrices; no second primitive
sort or persistent invalidation state. Existing asset identity comparisons remain;
rows/runs use indices and frame-owned storage. Invalid spans/spaces reject;
unsupported runs and failed/oversized allocations use the scalar path.

Self-check: batch layout/lifetime/limits unchanged; the shader correction adds
no buffers, protocol or cache. It keeps clamped taps and empty-map behavior.
Temporary C++ probes are removed. The original nine-view V1 matrix was not
repeated in full; the current four-view checks and analytic test are scoped
above. Tails, total memory and complete gameplay acceptance remain open, so
this does not close RP-05. Correction costs are recorded in
[RP-05](render-perf-draw-submission.md); historical measurements are separate,
not additive gains.
