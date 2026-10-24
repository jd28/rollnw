# Model backface culling

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Tier 1. Known coverage regression repaired; full-client performance acceptance remains open.
Full plan, intermediate candidates, failures, and self-check are preserved in the
[performance evidence archive](../README.md).

## 2026-10-21 independent coverage repair — retained

`Material::backface_culling` separates permission to discard faces from
`double_sided` shading. NWN import preserves both faces without changing shading
normals. glTF retains authored sidedness and the existing culling default. Runtime
submission consumes the common material flag; no asset-name or source-format
dispatch is added. Material remains 160 bytes; shader payloads are unchanged.

All 20 full-client reference images now match exactly across gate close/reopen,
city/outdoor actors, overview32 and orbit32, with 6,000 matching activity rows.
Both textured and animated glTF reference images also match exactly. All 236
selected tests pass without skips, including coverage-only shading, mirrors,
projection, skinning, instancing and importer semantics. Three temporary client
probe sources are restored; all 160 mudl and 192 client production files match
the frozen repair. Known asset failures and four existing Vulkan interface
warnings per client run remain; no new Vulkan errors occur.

Cost comparison against the visually regressed pre-repair build: three alternating
pairs/view, 7,200 measured frames and 3,600 matching non-timing sample pairs except
the declared pipeline-bind counters. Local32 host median is 1.432 → 1.580 ms and
GPU median 1.345 → 1.544 ms. Local96 GPU median is 2.106 → 2.246 ms. GPU time rises
in all three pairs in both views. Local96 host medians span 4.893–9.678 ms in the
baseline, so its apparent improvement is not a reliable gain. These are medians
of run statistics; this is a correctness cost, not performance acceptance.

The historical culling gains below do not describe the current NWN policy.
The known coverage defect is closed for these cases; paced gameplay/headroom,
larger full-client completeness, memory and edit acceptance remain open. Plan,
review, captures, timing variability and final manifests are staged under
`build/render-material-coverage-2026-10-21/`.

## 2026-10-20 full-client visual attribution

Five fixed-replay cases compare corrected reference `b5aa7cc78`, current
`34e296313`, and current with only model color culling disabled: gate close/reopen,
moving city/outdoor actors, complete 32 overview and orbit. All 15 starts complete;
18,000 checked rows have no draw/dispatch/allocation/bind/path/upload failures or
dropped ticks. Logical activity, camera and workload rows match across versions.

Disabling current color culling makes all **20 whole-image RGBA comparisons exact**
against the reference. Eight gate captures include closing, closed hold and
reopening. Current culling changes 152–2,944 gate pixels, 3,079–4,476 city pixels,
405–469 outdoor pixels, 1,043–1,044 overview pixels and 1,069–1,142 orbit pixels.
Live FPS text is hidden in the capture builds themselves; no image masks or
tolerances are used. The dark actor and city particle markers are common to both
versions. City inspection shows changes around flags and thin rails.

This isolates the cause of the observed changes; it does **not** establish that
their visual effect is acceptable. Inspect affected primitive winding and authored
sidedness before accepting or changing the policy. Do not select behavior by asset
name or blindly enable two-sided lighting to restore coverage: lighting and visible
faces both need verification. Repeat variability and larger views remain open.

These are validation/capture runs, not timing evidence. The no-cull variant is
diagnostic only. All 51 temporary source replacements and 192 production runtime
files are restored exactly; no production culling change is retained. See the
`2026-10-20/common-client-visuals.tar.gz` evidence entry for the reference recipe,
captures, rows, variants and failures.

## 2026-10-21 material contract inspection

Read-only inspection confirms that the common material currently uses one bit
for two different effects. `nwn/model_loader.cpp` copies
`NwnMeshImportData::two_sided_lighting` into `Material::double_sided`.
`model_color_cull_mode` uses that value to preserve or reject back faces, while
the NWN and PBR pixel shaders use it to reverse back-facing shading normals.
Setting this bit to recover missing coverage therefore also changes lighting.
The glTF importer likewise copies its source `double_sided` value to this bit.

The parsed `TrimeshNode` and binary mesh header have no separate sidedness field.
The MDL `twosidedtex` field belongs to `EmitterNode` and is imported into particle
materials; it is not a mesh coverage signal. The NWN mesh lighting bit defaults
false, with dangly-policy and explicit MTR overrides. These observations describe
the current parser/importer contract, not a claim about every source-format
extension or the affected primitives' intended winding.

Next inspect the affected primitive geometry and source material values, then
test a common material protocol that can preserve coverage without changing
lighting. A separate coverage flag would cost one material field, importer and
override propagation, and culling/grouping checks; its layout and frame cost are
unmeasured. No field or runtime policy is added by this inspection. Preserve
source-independent submission and the existing glTF sidedness behavior. Do not
use names, particle flags, or blanket two-sided lighting to select mesh coverage.

The independent model-submission comparison is running frozen binaries during
this source inspection. No build, second GPU workload, or timed-code change is
introduced. The existing 20 exact diagnostic captures remain attribution evidence;
the coverage repair and its performance consequences remain unverified.

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
