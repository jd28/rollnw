# Model backface culling

Tier 1. Implemented and measured; full-client acceptance remains open.
Full plan, intermediate candidates, failures, and self-check are preserved in the
[performance evidence archive](render-perf-evidence/README.md).

The Vulkan backend previously hardcoded no culling. Model color submission now
selects cached none/back/front variants from the material, world orientation,
and camera orientation. Mirrored transforms reverse the selected mode.
Double-sided materials, water, shadows, UI, and particle billboards preserve
both sides. Unknown/singular/nonfinite orientation conservatively disables
culling; invalid cull enums reject pipeline creation/lookup.

Prepared surfaces cache orientation alongside their world matrix. Existing
instance grouping separates differing orientations; transient batches split
contiguous runs without reordering. Uploaded batches declare homogeneous
orientation, defaulting to unknown/no-cull. Skin matrices account for reflected
binds and poses; mixed bone orientations use no culling.

## Measured result — 2026-10-12

Ryzen AI 9 HX 370 / Radeon 890M, Release Vulkan, 1920×1080, baseline 8c04e88df.
Six alternating pairs per scene, 180 warmup + 600 measured frames per run:
21,600 frames, main thread pinned to CPU 2 after load. Values are medians of
per-run medians; p95 is the median of per-run p95s. All samples retained.

| Scene | GPU scene ms | GPU change | Host frame ms | Host p95 ms |
| --- | --- | --- | --- | --- |
| 32×32 local | 1.506 → 1.318 | -12.45% | 1.551 → 1.365 | 2.471 → 2.154 |
| 96×96 local | 2.415 → 2.207 | -8.63% | 7.763 → 7.557 | 9.462 → 9.531 |
| 64×64 overview | 3.025 → 2.944 | -2.69% | 12.848 → 13.061 | 15.158 → 14.551 |

Overview host median increases 1.66%, worsening in four of six pairs; keep that
CPU tradeoff explicit. Host frame duration includes synchronization. This does
not establish a CPU ceiling or demonstrate a need for camera restrictions.
Every frame's counts/command fields match; recorded dropped draws, allocation
failures, invalid surfaces, and capacity overflows are zero.

Cost: 20 additional unique pipelines (14 → 34), 184 extra bytes in the handle
table. PipelineDesc stays 96 bytes and prepared surfaces stay 208 bytes. Stable
orientation is computed on refresh, camera policy once per scalar batch; skins
scan at most 64 already prepared matrices. No CPU triangle traversal. Native
pipeline memory/initialization cost is not isolated; whole-process RSS/load
measurements are retained in the report.

## Verification and limits

- 165 renderer/importer/batching tests pass without skips; 56 new GPU readbacks
  cover front/back, mirrored transforms, material overrides, both camera Y
  conventions, scalar/instanced paths, and reflected/mixed skin matrices.
- Separate tests and captures use Vulkan validation; timing runs disable it.
- Eleven city/outdoor/NWN/glTF captures inspected. Changes include removed
  backfaces and wing/torch lighting; no broad missing front geometry observed.
  City changed pixels: 0.237% / 0.221%; overview: 0.389%. Production matches all
  earlier culling captures exactly. PBR corpus used common fallback lighting
  because IBL was unavailable in the frozen runtime cwd.
- Existing shadow tests pass; shadow passes deliberately remain two-sided.
- Paced/full-client behavior and all asset/camera combinations remain unverified.
  Bone orientation checks do not certify arbitrary self-inverting skin poses.

Simplification reused cache ownership and indexed batches, preserved row size,
and removed repeated camera/prepared-transform classification. Format/diff checks
pass; final source and executable match the measured production variant.

Evidence: `2026-10-12/backface-culling.tar.gz` under
`/home/josh/projects/rollnw-evidence/`. Use `accepted-results.json` for production;
`final-results.json` is the intermediate compact candidate, retained separately.
The archive includes the full pre-implementation plan, raw cohorts, binaries,
resources, captures, patches, test/build logs, drivers and final self-check.
