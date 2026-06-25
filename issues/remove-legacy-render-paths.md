# Remove Legacy Render Paths

## Data

The common model path is now the default for the modern renderer:

- glTF previews load through `ModelAsset -> RenderModel` by default.
- Viewer and mudl no longer expose the direct glTF RenderModel importer as a
  preview routing mode; focused importer tests keep that code path available for
  reference/debug coverage.
- Static NWN model previews and single-model creature object previews load
  through `ModelAsset -> RenderModel` by default.
- Viewer and mudl submit RenderModel/PBR surfaces through the common prepared
  surface stream; the direct RenderModel submission override has been removed.
- Non-area NWN legacy previews select sidecar payloads through the common
  prepared surface stream by default, while the sidecar still owns PLT, legacy
  mesh payloads, skin, dangly, emitter, and node-tree behavior.

The remaining legacy paths are compatibility and parity paths, not the desired
modern runtime shape.

## Active Fallbacks

- `--legacy-nwn-model-path`: legacy `nwn::ModelInstance` preview loading.
- NWN RenderModel previews render `secondary_motion_chain` and
  `legacy_reference_cpu` deformed primitives as static source geometry and
  record `nwn_render_model_static_deformer`. Those records describe
  hair/robe/cape-style motion that the common renderer does not execute yet.
- NWN area static material rendering still uses legacy sidecar payloads for
  static batching and compatibility rendering.
- `ViewerSession::render_model_animation_samples_` and
  `AppState::render_model_animation_samples` remain reusable frame scratch, not
  animation state ownership. `ViewerSession::render` samples that scratch once
  per frame after optional attachment runtime sync.

## Remove Only After These Gates

- Default common paths have broad non-headless coverage in CI or an equivalent
  repeatable parity job.
- Vulkan screenshot parity has been rerun for representative glTF and NWN
  assets after each fallback removal, with AE/RMSE recorded when available.
- Direct RenderModel submission no longer covers a behavior that prepared
  submission cannot test directly.
- Live glTF/NWN adapters still expose the diagnostics needed by the offline
  compiler work; removing a runtime fallback must not remove import validation.
- NWN legacy sidecar code is removed only where the common runtime owns the
  equivalent data. Keep sidecar paths for actual unresolved legacy quirks:
  humanoid body-part assembly, PLT policy, legacy emitters, and area static
  batching.

## Current Removal Status

Direct RenderModel submission fallback removal is complete for viewer and mudl:
`--legacy-render-model-draws`, `--prepared-render-model-draws`, and
`ROLLNW_VIEWER_RENDER_MODEL_PREPARED_DRAWS` no longer select a runtime mode.
The retired CLI flags now fail loudly instead of silently selecting an obsolete
path.

Direct glTF preview importer fallback removal is complete for viewer and mudl:
`--legacy-gltf-model-path`, `--model-asset-gltf-model-path`, and
`ROLLNW_VIEWER_GLTF_MODEL_ASSET_PATH` no longer select a runtime mode. glTF
previews always use `ModelAsset -> RenderModel`; the direct importer remains
only as focused reference/debug importer code.

Direct non-area NWN sidecar draw fallback removal is complete for viewer and
mudl: `--legacy-nwn-draws`, `--prepared-nwn-draws`, and
`ROLLNW_VIEWER_NWN_LEGACY_PREPARED_DRAWS` no longer select a runtime mode.
Non-area NWN sidecar previews always use common prepared surface records; legacy
NWN model loading remains separately gated.

The redundant positive NWN model-load flag is retired for mudl:
`--render-model-nwn-model-path` now fails loudly because `ModelAsset ->
RenderModel` is already the default. `--legacy-nwn-model-path` remains for
diagnostic captures and compatibility checks.

The hidden NWN model-load env override is retired:
`ROLLNW_VIEWER_NWN_RENDER_MODEL_PATH=0` no longer changes default preview load
options. The legacy load path now requires the explicit mudl CLI flag above.

The legacy glTF animation-state switch is retired for mudl:
`--legacy-gltf-animation-state`, `--unified-gltf-animation-state`, and
`ROLLNW_MUDL_GLTF_ANIMATION_STATE` no longer select a runtime mode. RenderModel
animation backend, clip, time, looping, pose, and skin matrices are stored on
common `ModelInstance` records; UI controls may still broadcast clip/time into
the selected scene instances.

Dynamic NWN legacy area records no longer use the normal direct area sidecar
submission path in `ViewerSession` or mudl. `AreaRenderFrame` exposes visible
dynamic NWN `ModelInstanceHandle` rows, and the frame renderer batches those
handles through the common prepared-surface collector before submission. Static
area material batches still bridge common surface indices to NWN sidecar draw
payloads until static area geometry is migrated. A current Vulkan smoke capture
of `test_area` through `mudl area-screenshot` matched the prior common baseline:
`magick compare -metric AE` reported `0 (0)`, and
`magick compare -metric RMSE` reported `0 (0)`. Submission counters for that
capture reported `nwn_selected=1`, `nwn_missing_sidecars=0`,
`nwn_invalid_sidecars=0`, `render_model_submitted_surfaces=38`, and
`render_model_dropped_invalid_surfaces=0`.

The legacy NWN model-load fallback is still active for unresolved source quirks.
A `4985599a5` capture of `c_aribeth` submitted 18 RenderModel surfaces on the
default path and 18 NWN sidecar surfaces on `--legacy-nwn-model-path`;
`magick compare -metric AE` reported `48196 (0.052296)` and
`magick compare -metric RMSE` reported `6985.06 (0.106585)`. Later source-row
verification showed common `pause1` sampling matches the legacy sidecar at
33 ms for the rigid primitive node transforms, so that mismatch is not evidence
that animation storage is wrong. Visual inspection isolated the visible mismatch
to `c_aribeth` dangly hair/torso surfaces. `mudl stats c_aribeth` reports zero
local animations, supermodel `a_fa`, 27 nodes, 18 meshes, and two dangly meshes.
The default RenderModel preview path now detects unsupported secondary-motion
deformer primitives and records `nwn_render_model_static_deformer` while keeping
the model on the common path as static source geometry. This is an accepted
bridge policy for non-foliage legacy dangly meshes. `--legacy-nwn-model-path`
remains for diagnostic captures and unresolved model-load compatibility cases,
not because `c_aribeth` animation or dangly bridge policy is still undecided.

The remaining tests cover option defaults and protocol boundaries:

- `MudlAppRuntime.DefaultsToPreparedCommonModelPaths` verifies the mudl runtime
  defaults to `NwnModelPreviewPath::render_model`; RenderModel prepared
  submission and non-area NWN sidecar prepared submission are no longer
  configurable app state.
- `RenderViewerPreparedDraws.PreviewSceneLoadOptionsDefaultToNwnRenderModelPath`
  verifies the remaining preview load default before any graphics device is
  required.
- CLI parser tests reject the retired RenderModel draw-path, glTF preview
  load-path, non-area NWN sidecar draw-path, and positive NWN model-load flags.

The measured input that justified removal was a repeatable non-headless parity
run for representative assets. That run captured default common output and the
matching legacy override output for:

- one skinned glTF model with animation
- one static glTF model
- one humanoid NWN creature with body parts, PLT/material policy, emitters, and
  attachments
- one NWN placeable or tile/area case that exercises transparency and legacy
  sidecar payloads

For each pair below, the git revision, command lines, output paths, AE/RMSE,
and relevant frame counters are recorded. Some pre-removal commands include
flags that are now retired; treat those as historical evidence, not current
rerun instructions. Do not infer render policy from model, texture, or node
names while building future captures.

## Measured Parity Evidence

- `c76c09942`: skinned glTF direct-submission parity for
  `tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb`.
  - Common path:
    `./build/tools/mudl/mudl screenshot tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb /tmp/rollnw-parity/cesium-common.png --prepared-render-model-draws`
  - Direct fallback:
    `./build/tools/mudl/mudl screenshot tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb /tmp/rollnw-parity/cesium-direct.png --legacy-render-model-draws`
  - Comparator:
    `magick compare -metric AE` reported `0 (0)`;
    `magick compare -metric RMSE` reported `0 (0)`.
  - Counters: common path submitted one prepared RenderModel surface and one
    skinned surface with 19 skin matrices; direct fallback disabled prepared
    RenderModel submission while building the same single opaque surface and
    skin table. Both paths reported zero RenderModel drops.
  - Scope: this covers one skinned glTF submission case only. Static glTF, NWN
    creature, and NWN placeable/area parity gates remain open.
- `c76c09942`: static glTF direct-submission parity for
  `tests/test_data/renderer/DamagedHelmet/glTF-Binary/DamagedHelmet.glb`.
  - Common path:
    `./build/tools/mudl/mudl screenshot tests/test_data/renderer/DamagedHelmet/glTF-Binary/DamagedHelmet.glb /tmp/rollnw-parity/helmet-common.png --prepared-render-model-draws`
  - Direct fallback:
    `./build/tools/mudl/mudl screenshot tests/test_data/renderer/DamagedHelmet/glTF-Binary/DamagedHelmet.glb /tmp/rollnw-parity/helmet-direct.png --legacy-render-model-draws`
  - Comparator:
    `magick compare -metric AE` reported `0 (0)`;
    `magick compare -metric RMSE` reported `0 (0)`.
  - Counters: common path submitted one prepared RenderModel surface with zero
    skin-table entries; direct fallback disabled prepared RenderModel
    submission while building the same single opaque surface. Both paths
    reported zero RenderModel drops.
  - Scope: this covers one static glTF submission case only. NWN creature and
    NWN placeable/area parity gates remain open.
- `28b5722c3` pre-removal worktree: skinned glTF importer-path parity for
  `tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb`.
  - ModelAsset path:
    `./build/tools/mudl/mudl screenshot tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb /tmp/rollnw-parity/gltf-path-cesium-model-asset.png`
  - Direct importer fallback:
    `./build/tools/mudl/mudl screenshot tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb /tmp/rollnw-parity/gltf-path-cesium-direct-importer.png --legacy-gltf-model-path`
  - Comparator:
    `magick compare -metric AE` reported `0 (0)`;
    `magick compare -metric RMSE` reported `0 (0)`.
  - Counters: both paths submitted one prepared RenderModel surface and one
    skinned surface with 19 skin matrices; both paths reported zero RenderModel
    drops.
  - Post-removal smoke:
    `./build/tools/mudl/mudl screenshot tests/test_data/renderer/CesiumMan/glTF-Binary/CesiumMan.glb /tmp/rollnw-parity/gltf-path-cesium-post-removal.png`
    matched the pre-removal ModelAsset capture with `magick compare -metric AE`
    reporting `0 (0)` and `magick compare -metric RMSE` reporting `0 (0)`.
    Counters again reported one prepared RenderModel surface, one skinned
    surface, 19 skin matrices, and zero RenderModel drops.
  - Scope: this covers one skinned glTF preview loading case only. Direct glTF
    preview routing is removed; direct importer code remains for focused tests.
- `28b5722c3` pre-removal worktree: static glTF importer-path parity for
  `tests/test_data/renderer/DamagedHelmet/glTF-Binary/DamagedHelmet.glb`.
  - ModelAsset path:
    `./build/tools/mudl/mudl screenshot tests/test_data/renderer/DamagedHelmet/glTF-Binary/DamagedHelmet.glb /tmp/rollnw-parity/gltf-path-helmet-model-asset.png`
  - Direct importer fallback:
    `./build/tools/mudl/mudl screenshot tests/test_data/renderer/DamagedHelmet/glTF-Binary/DamagedHelmet.glb /tmp/rollnw-parity/gltf-path-helmet-direct-importer.png --legacy-gltf-model-path`
  - Comparator:
    `magick compare -metric AE` reported `0 (0)`;
    `magick compare -metric RMSE` reported `0 (0)`.
  - Counters: both paths submitted one prepared RenderModel surface with zero
    skin-table entries; both paths reported zero RenderModel drops.
  - Scope: this covers one static glTF preview loading case only. Direct glTF
    preview routing is removed; direct importer code remains for focused tests.
- `c76c09942`: NWN-converted RenderModel direct-submission parity for
  `c_aribeth`.
  - Common path:
    `./build/tools/mudl/mudl screenshot c_aribeth /tmp/rollnw-parity/aribeth-common.png --render-model-nwn-model-path --prepared-render-model-draws`
  - Direct fallback:
    `./build/tools/mudl/mudl screenshot c_aribeth /tmp/rollnw-parity/aribeth-direct.png --render-model-nwn-model-path --legacy-render-model-draws`
  - Comparator:
    `magick compare -metric AE` reported `0 (0)`;
    `magick compare -metric RMSE` reported `0 (0)`.
  - Counters: common path submitted 18 prepared RenderModel surfaces in one
    opaque/cutout run; direct fallback disabled prepared RenderModel submission
    while building the same 18 opaque surface rows. Both paths reported zero
    RenderModel drops and no selected NWN sidecar rows.
  - Scope: this covers common NWN asset loading plus RenderModel submission
    parity for one humanoid creature. It does not prove parity against the old
    `nwn::ModelInstance` renderer, and it does not cover NWN placeable/area
    transparency or sidecar-only payloads.
- `c76c09942`: NWN-converted RenderModel direct-submission parity for
  `plc_archtarg`.
  - Common path:
    `./build/tools/mudl/mudl screenshot plc_archtarg /tmp/rollnw-parity/plc-archtarg-common.png --render-model-nwn-model-path --prepared-render-model-draws`
  - Direct fallback:
    `./build/tools/mudl/mudl screenshot plc_archtarg /tmp/rollnw-parity/plc-archtarg-direct.png --render-model-nwn-model-path --legacy-render-model-draws`
  - Comparator:
    `magick compare -metric AE` reported `0 (0)`;
    `magick compare -metric RMSE` reported `0 (0)`.
  - Counters: common path submitted 4 prepared RenderModel surfaces in one
    opaque/cutout run; direct fallback disabled prepared RenderModel submission
    while building the same 4 opaque surface rows. Both paths reported zero
    RenderModel drops and no selected NWN sidecar rows. The session primed one
    particle system with zero live particles before capture.
  - Scope: this covers common NWN asset loading plus RenderModel submission
    parity for one placeable-style model. It does not prove area traversal or
    sidecar-only transparency parity.
- `c76c09942`: area RenderModel direct-submission parity for `test_area` from
  `tests/test_data/user/modules/module_as_dir`.
  - Common path:
    `./build/tools/mudl/mudl area-screenshot test_area /tmp/rollnw-parity/test-area-common.png --module tests/test_data/user/modules/module_as_dir --prepared-render-model-draws`
  - Direct fallback:
    `./build/tools/mudl/mudl area-screenshot test_area /tmp/rollnw-parity/test-area-direct.png --module tests/test_data/user/modules/module_as_dir --legacy-render-model-draws`
  - Comparator:
    `magick compare -metric AE` reported `0 (0)`;
    `magick compare -metric RMSE` reported `0 (0)`.
  - Counters: the common path loaded 260 area model records, 185 static-geometry
    meshes, and 185 primed particle systems, then submitted 38 prepared
    RenderModel surfaces in one opaque/cutout run with zero RenderModel drops.
    The direct fallback loaded the same area/cache data and disabled prepared
    RenderModel submission for the capture.
  - Scope: this covers one area traversal and mixed static/dynamic scene
    capture. It proves only direct RenderModel submission parity for this input;
    NWN sidecar-specific fallback removal remains separately gated.
- `4e11e5731` pre-removal worktree: non-area NWN sidecar draw-path parity for
  legacy-loaded `c_aribeth`.
  - Prepared sidecar path:
    `./build/tools/mudl/mudl screenshot c_aribeth /tmp/rollnw-parity/nwn-sidecar-aribeth-prepared.png --legacy-nwn-model-path --prepared-nwn-draws`
  - Direct sidecar fallback:
    `./build/tools/mudl/mudl screenshot c_aribeth /tmp/rollnw-parity/nwn-sidecar-aribeth-direct.png --legacy-nwn-model-path --legacy-nwn-draws`
  - Comparator:
    `magick compare -metric AE` reported `0 (0)`;
    `magick compare -metric RMSE` reported `0 (0)`.
  - Counters: prepared sidecar path selected 18 NWN sidecar draws with zero
    missing/invalid sidecars; direct fallback bypassed prepared sidecar
    submission while collecting the same 18 opaque prepared surface rows.
  - Post-removal smoke:
    `./build/tools/mudl/mudl screenshot c_aribeth /tmp/rollnw-parity/nwn-sidecar-aribeth-post-removal.png --legacy-nwn-model-path`
    selected the same 18 NWN sidecar draws with zero missing/invalid sidecars
    and matched the prepared capture with `magick compare -metric AE`
    reporting `0 (0)` and `magick compare -metric RMSE` reporting `0 (0)`.
  - Scope: this covers one humanoid legacy-loaded NWN preview. Legacy NWN model
    loading itself remains a separate active fallback.
- `4e11e5731` pre-removal worktree: non-area NWN sidecar draw-path parity for
  legacy-loaded `plc_archtarg`.
  - Prepared sidecar path:
    `./build/tools/mudl/mudl screenshot plc_archtarg /tmp/rollnw-parity/nwn-sidecar-plc-archtarg-prepared.png --legacy-nwn-model-path --prepared-nwn-draws`
  - Direct sidecar fallback:
    `./build/tools/mudl/mudl screenshot plc_archtarg /tmp/rollnw-parity/nwn-sidecar-plc-archtarg-direct.png --legacy-nwn-model-path --legacy-nwn-draws`
  - Comparator:
    `magick compare -metric AE` reported `0 (0)`;
    `magick compare -metric RMSE` reported `0 (0)`.
  - Counters: prepared sidecar path selected 4 NWN sidecar draws with zero
    missing/invalid sidecars; direct fallback bypassed prepared sidecar
    submission while collecting the same 4 opaque prepared surface rows. The
    session primed one particle system with zero live particles before capture.
  - Post-removal smoke:
    `./build/tools/mudl/mudl screenshot plc_archtarg /tmp/rollnw-parity/nwn-sidecar-plc-archtarg-post-removal.png --legacy-nwn-model-path`
    selected the same 4 NWN sidecar draws with zero missing/invalid sidecars
    and matched the prepared capture with `magick compare -metric AE`
    reporting `0 (0)` and `magick compare -metric RMSE` reporting `0 (0)`.
  - Scope: this covers one placeable-style legacy-loaded NWN preview. Area
    static batching and legacy NWN model loading remain separate active
    fallback boundaries.

## Current Collapse Gates

- Modern `RenderModel` / PBR rendering functions consume only
  `nw::render::ModelRenderContext` and must not require
  `nw::render::nwn::ModelRenderContext` or `RenderAssetCache`.
- Submission boundaries drop and count out-of-range indices, invalid asset
  indices, primitive/material mismatches, stale handles, and missing sidecar
  payloads before Vulkan submission.
- Remaining NWN fallback paths stay command/env gated until their measured
  parity role is replaced by focused tests or CI captures.

## NWN Context Audit

Current `nw::render::nwn::ModelRenderContext` use is split by boundary:

- compatibility renderer calls: legacy model color draws, legacy shadow draws,
  legacy prepared static area draws, and sidecar prepared-surface submission
  when `nwn_legacy` payloads are enabled
- particle bridge calls: ordinary particle submission does not take an NWN
  render context; mesh particles route through a local sidecar bridge helper,
  request the sidecar context lazily, load and submit legacy NWN model sidecars
  through the asset cache, and count mesh packets, submitted particles, missing
  mesh resrefs/models, invalid packet materials, and invalid particle indices at
  that bridge
- cache/resource bridge calls: preview particle texture lookup and static area
  prepared draw-data refresh still use `RenderAssetCache` or NWN static draw
  payloads

Common RenderModel/PBR color and shadow submission uses
`nw::render::ModelRenderContext` only. Prepared surface submission is split by
payload kind: RenderModel surfaces use the common context helper and NWN legacy
surfaces use the sidecar context helper. Non-area NWN sidecar prepared
submission uses one sidecar helper shared by viewer and `mudl`; the direct
non-area sidecar fallback has been removed. Area direct fallback submission is
split by payload kind with the sidecar request owned by the legacy helper.
Public scene/local shadow submission uses `RenderService` plus the common
context; the NWN context remains inside legacy sidecar shadow bridge calls.
Viewer and `mudl` render loops no longer construct the NWN sidecar context
directly; non-area sidecar prepared submission requests it only when legacy
sidecar rows survive to submission. Non-area and local shadow orchestration
also request the sidecar context only when legacy shadow draws survive to
submission.
The public non-area prepared-surface API exposes the pass-filtered,
surface-indexed sidecar selection transform for tests and diagnostics, while
submission takes `RenderService` and owns sidecar context creation internally.
The area direct legacy fallback follows the same shape for tests and
diagnostics: selected legacy records can still be collected/counted without a
sidecar context, while submission takes `RenderService`. Area static material
helpers own the sidecar request because area static batching is still a legacy
sidecar path, but they request it lazily only after the common-surface bridge
selects surviving legacy sidecar rows; their raw sidecar helpers are
implementation-local. Normal viewer/mudl dynamic legacy and RenderModel
submission is now through the common prepared surface stream. Area
static material cache/indirect/depth/submission and shadow cache/submission
bridging now report selected sidecar draws plus dropped non-NWN, invalid-index,
missing-sidecar, and invalid-sidecar surface rows.

## Next Actions

- Keep the current reusable scratch fields until their consumers are removed:
  prepared draw lists feed submission/shadow/debug reports, NWN prepared pointer
  lists feed prepared sidecar submission, animation sample vectors feed
  per-frame sampling reports, and `PreparedDrawScratch` feeds legacy sidecar
  bridge batching. None of these are animation-state ownership.
- Reduce remaining `nw::render::nwn::ModelRenderContext` use by boundary:
  compatibility renderer, particle bridge, area bridge, or removable. Prepared
  surface color submission and area direct fallback are isolated by
  helper/payload kind; public particle and shadow submission no longer take an
  NWN context, and top-level render loops no longer build the sidecar context
  for model submission. Area static batching still has legacy sidecar
  submission/cache boundaries, but their context requests are lazy and counted.
- The current common area record protocol slice is complete. The remaining area
  work is a later traversal collapse, not another sidecar-pointer bridge
  migration.
- Keep the offline compiler work separate in
  `issues/offline-model-compiler.md`; compiled assets are the long-term runtime
  ownership model, but live adapters remain import/debug/compat inputs until the
  compiler exists.

## Do Not Carry Forward

- Renderer behavior inferred from model, texture, or node names.
- Per-frame source-format decisions in modern draw submission.
- Raw sidecar pointers as the stable runtime identity for new model paths.
- Silent fallback removal without counters, parity data, or equivalent tests.
