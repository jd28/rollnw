# Retain item preview models for palette-only edits

Status: implemented and verified, 2026-10-01.

Follow-up: [retain palettes in file-loaded Item scenes](closed/rollnw-client-file-item-palette-refresh.md)
removes the remaining original-scene reconstruction and restores palette
eligibility after standalone geometry rebuilds.

## Observed problem

A native Cloth 1 edit on `cloth028.uti` takes 24.14 ms median from client
dispatch to CPU presentation return after retaining the Item editor DOM.
`load_live_object` inside the mannequin viewport accounts for 13.32 ms;
mannequin construction and icon refresh account for another 0.91 ms.
See [matched desktop measurements](rollnw-client-area-edit-transition-measurement.md#desktop-results-2026-10-01).

PLT decoding already runs in `lib/nw/render/shaders/render_nwn_static.ps.hlsl`
and the PBR shader. `Material::plt_colors0/1/2` become per-draw surface
constants through `model_renderer.cpp`. Changing those palette rows does not
intrinsically require replacing the models or their textures.

`tools/client/item_preview.cpp::refresh_item_preview` currently destroys the
mannequin and clones its equipped Item through JSON on any mutation epoch.
`tools/client/viewer_viewport.cpp::render_item_preview` then reloads the complete
live object on identity/revision change. `ViewerSession::set_scene` also waits
for prior GPU resource use before replacing the scene. The trace includes that
reload boundary; it does not separate GPU waiting from model construction.

## Bounded next investigation

Retain the preview tree when only palette data changes, then update existing
material overrides. Existing override stores and surface constants provide the
rendering machinery. The missing contract is a reliable connection from an
authored visual row to its loaded model instance, including skipped models,
body-part overrides and inherited creature colors. Model names are not a
substitute for this contract.

Input is one displayed Item's six global colors and 19 by six part colors
(255 means inherit), plus its actual mannequin visual rows. Output is updated
palette constants for all affected loaded materials. Geometry, source creature,
gender, resources or ownership changes must retain the existing rebuild path.
Stale identities and invalid mappings must reject the retained update without
partially publishing it. An unchanged unrelated edit should not reload the
mannequin either.

Cost to evaluate: on the single desktop UI thread, a linear batch over affected
visual/model/material rows, plus retained index mapping and previous visual
shape data. Exact storage and latency are **unverified** until the mapping is
designed against real loaded rows. The measured 13.32 ms identifies the current
cost, not a promise that all of it can be removed. Do not introduce an asset
cache, worker, or renderer-wide mutation protocol without additional evidence.

## Done criteria

- Native global and part-color edits/undo retain mannequin/model identities;
  surface constants and the rendered colors change correctly.
- Existing body-part inheritance, male/female previews, source-creature
  isolation, inventory icon and exact serialized undo remain correct.
- Model-part, base-item, gender, source and resource changes rebuild correctly;
  missing resources and stale objects use explicit failure/fallback behavior.
- Repeat the existing matched replay and report median/p95 and model reload
  counts. Check screenshot output and the relevant ownership/appearance tests.

The following plan records the implementation that followed the initial
desktop measurement and retained-editor change. Results are below.


## Implementation plan (2026-10-01, Tier 1)

### Patterns & Conventions Found

- `item_preview.cpp:29` owns one private preview tree; `:92` clones the Item.
  `toolset/item_preview.smalls::configure_mannequin` copies only visual source
  appearance/body/color/gender data. Reuse that provider and ObjectDocument.
- `preview_scene.cpp:1935` creates PLT material overrides at model load time;
  body, attachment and equipment loaders already know the exact source row.
- `model_renderer.cpp:175` publishes these overrides as per-draw constants.
  `ViewerSession::set_scene` waits before destroying old GPU resources;
  palette writes affect CPU material data consumed by subsequent frame draws.
- Existing contiguous ModelInstance/MaterialOverride stores, snapshot conventions
  and span-based refresh APIs suffice. No new cache or dependency is required.

### Architecture Decision

Retain the mannequin when identity, source, resource generation, gender, base
item and model parts match. Copy only its Item visual arrays, reconfigure the
source appearance, and regenerate the existing visual rows. Capture a visual
snapshot and dense model-to-visual-row indices when loading a standalone scene.
Validate unchanged non-palette data and all override destinations before writing
any new colors. Geometry or invalid state uses the existing full reload.

Inputs: six global bytes, 19 model-part integers and 114 part-color bytes;
part color 255 inherits. Render input is the resolved ObjectVisualState array,
including masks, skipped/missing models, attachment rows, lights and hold
animation. No model names select policy. The observed common workload is one
color edit followed by undo; ASSUMPTION: this is representative of interactive
palette browsing — affects the benchmark, not correctness. Object identities,
resources and model geometry are stable across a color edit; palettes vary.
Bounds and palette-mask validity follow existing component/shader contracts.
Invalid/stale data rejects the retained path without partial material writes.

### Component Design / Implementation Map

- `item_preview.cpp`, `toolset/item_preview.smalls`: reuse the owned mannequin
  and copied Item for unchanged geometry; no unrelated Item data is needed by
  this private visual consumer. Gender/source/resource changes use reconstruction.
- `preview_scene.hpp/.cpp`: a scene-owned visual snapshot plus one uint32 row
  index per model; base-color and unavailable mappings have explicit sentinels.
  New plural palette refresh validates the complete array, then updates existing
  override rows. Adding/removing models invalidates the snapshot. No mapping is
  inferred by model name or retained across area compaction.
- `session.hpp/.cpp`, `viewer_viewport.cpp`: try retained palettes at the existing
  visual refresh boundary and reload when rejected. Preserve camera/animation.
- Existing browser-workspace and renderer tests: assert identities, constants,
  inheritance/undo, source isolation and geometry/resource/error fallback.

### Data Flow / Cost

Item edit -> copy native visual arrays into private equipped Item -> existing
profile regenerates mannequin visual rows -> compare geometry snapshot ->
validate model/material destinations -> write palette constants -> normal draw.
The real platform is the Linux desktop UI thread, contiguous CPU stores, and an
asynchronous Vulkan GPU. No GPU resource destruction or queue wait is needed for
CPU constants. Cost is O(visual rows + model materials), with two sequential
passes and predictable eligibility checks, plus snapshot bytes and four bytes
per mapped model. Existing store handles are generation-checked indices, not new
pointer ownership. Temporary object/row references are synchronous borrows from
existing APIs. One displayed mannequin/session is a genuine singleton; the
palette transform handles all its material rows as a batch.

### Build Sequence / Critical Details

1. Record/reuse the already measured retained-DOM baseline and preserve a matched
   replay binary. Implement retained preview inputs, then palette publication.
2. Run ownership, color/undo and real-GPU renderer regressions, including rejected
   batches and resource changes; inspect rendered output.
3. Repeat native desktop edit/undo measurements, retaining raw traces and reload
   counts; remove temporary replay hooks and build the final product.

Simplification: skip model construction entirely for palette-only changes;
record source indices once at load time. Reuse current shaders and stores. No
approximation, lookup cache, per-frame polling, worker or area-wide remapping is
needed. Done is unchanged visuals/undo plus retained identities and a measured
reduction in reload calls/latency. Any stale colors, partial rejected update,
source mutation or incorrect geometry retention disproves the approach. Plan B
is the existing full reload when the retained contract cannot be satisfied.


## Results and final self-check

The private mannequin and equipped Item now survive color edits. Their native
visual arrays feed the existing profile; resolved visual rows update existing
material overrides without model construction or a GPU resource-destruction
wait. Geometry, source identity, gender, resource generation, visual-mode changes
and invalid/stale mappings keep the full-reload path. Reconfiguration also resets
fallback wings, tail and colors so reused actors match fresh actors.

Matched native desktop measurements used the same Release build, Radeon 890M,
3841x2028 window, fixtures and uncapped replay as the previous investigation.
Both variants retain the Item DOM. The baseline executable predates palette
retention. Process order was baseline, candidate, candidate, baseline, with
three warmup pairs and 20 measured edit/undo pairs per scenario per process.
Each result below has 40 measured samples; p95 uses nearest rank.

| CPU interval / count | Before | After |
|---|---:|---:|
| Color edit dispatch-to-presentation return, median | 21.72 ms | 8.60 ms |
| Color edit, p95 | 28.31 ms | 12.43 ms |
| Undo, median | 21.11 ms | 8.16 ms |
| Undo, p95 | 24.27 ms | 14.32 ms |
| Mannequin/icon refresh, edit median | 0.81 ms | 0.53 ms |
| Item viewport, edit median | 12.40 ms | 0.046 ms |
| Model reload calls per color edit or undo | 1 | 0 |

The edit median fell by 13.12 ms (60.4%). Individual process edit medians were
21.56/21.93 ms before and 8.35/8.78 ms after. Use this matched comparison, rather
than comparing the new number directly to the older 24.14 ms run. The viewport
interval includes palette validation/publication and normal draw preparation;
it is not a shader-duration measurement. The remaining command, main scene,
editor and layout work still contributes to total latency. No performance
improvement is claimed for unchanged area, class or equipment scenarios.

[Raw traces, summaries, screenshots, test results and reproduction patches](item-preview-palette-evidence/README.md)
are retained. All 1,288 replay actions completed, including warmup. Every undo
restored the serialized root exactly; input state hashes match across all runs,
selection remained stable, and the Item DOM stayed retained. The two screenshots
show the same edited palette and icon; small pose differences are expected now
that a color edit preserves ongoing mannequin animation instead of restarting it.

All 105 focused client/data tests and six full-resource graphics tests passed.
Coverage includes global/part colors and undo, copied-item/source isolation,
geometry/gender/resource/base-item changes, missing native rows, fallback source
appearance, invalid masks, unmapped/skipped/repeated rows, stale material handles,
atomic rejection, camera/model/instance retention and visual-mode invalidation.
The initial renderer attempt used dedicated-server data, which lacks the required
models; repeating with the full installed game assets passed without skips.
Final Release build and clang-format/diff checks passed. Replay and screenshot
hooks are absent from production sources.

Retained storage is one uint32 per loaded model plus a copied visual snapshot.
The tested mannequin has 18 models and 17 visual model rows: 72 bytes of logical
mapping entries and 1,224 bytes of copied model rows (72 bytes each). The visual
snapshot object is 88 bytes, with additional normal vector control/capacity and
generation bookkeeping. No new GPU buffers or textures are needed for palette
publication. Refresh uses two linear model/material passes after geometry
validation; there is no allocation in that publication function. The Item/icon
provider remains a separate existing refresh and can allocate.

Simplification removed per-color model reconstruction and JSON Item cloning;
it reused profile resolution, shaders and indexed override stores. No worker,
asset cache, approximate color path or area-wide mapping was added. Scene
composition/topology changes invalidate the standalone snapshot. The common
valid-color path has predictable eligibility checks; invalid batches publish no
partial colors. Existing object/asset pointers are synchronous API borrows,
while retained mappings use indices. All stated done criteria were checked.
Cold first-edit latency, physical display/compositor timing and Windows remain
unmeasured; the reported timings are warm CPU dispatch-to-submission-return data.
