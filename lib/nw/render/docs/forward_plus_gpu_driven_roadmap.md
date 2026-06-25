# Forward+ completion & GPU-driven rendering roadmap

Scope: `lib/nw/render/`.

This is the execution plan for (1) finishing the Forward+ renderer, (2) reducing
accidental complexity in the render layer, and (3) positioning the area path for
GPU-driven rendering. It is sequenced so each phase reuses the infrastructure of
the prior one, and so the low-risk simplifications land before the large changes
that would otherwise have to touch the duplicated code.

## Status (updated 2026-06-18)

**Completion snapshot:** the renderer infrastructure work is mostly complete. A1
(Forward+ API collapse), A2 (arena unification), B1 (`cmd_draw_indexed_indirect_count`
+ `drawIndirectCount`), B2 (compute storage + compute->graphics barrier), C1
(depth pre-pass), C3 (froxel AABB culling), renderer unification, D2 (static
geometry mega-buffer), C4 (local-light shadows), A3 (pipeline descriptor cache +
semantic pipeline keys), and most of A4 (normal matrix caching) are done.

**Forward+ GPU cull default:** Forward+ itself is the default local-light path,
and C2's GPU gather variant (`ForwardPlusRenderPolicy::gpu_culling`) is now the
default policy. `mudl area-benchmark` / `mudl area-sweep` can force the CPU
reference path with `--no-forward-plus-gpu-cull`; the viewer can do the same with
`ROLLNW_VIEWER_FORWARD_PLUS_GPU_CULL=0`. The GPU path writes the same cluster
buffers the shaders consume, with compute owning per-cluster light membership.
C3 replaced the earlier screen-rect culler with view-space froxel AABB tests.

**Remaining high-value work:** C4 shadow projection policy is bounded but modest;
revisit only if a target scene needs stronger radial point-light shadows. Stage 2
PBR convergence is complete: the pinned corpus covers glTF PBR, NWN
specular/shininess, PLT/equipment, water, MTR sidecars, and area local-light
material response.

**Parked by measurement:** the D-series GPU-driven area submission work is not
the current bottleneck. `AreaRenderScene::build_static_geometry` already puts
static area geometry in one VB + one IB, so an area opaque pass is already a
small number of indirect draw calls over thousands of source mesh draws. The last
recorded `ms_4city` gameplay-camera numbers were: `area_prepare` (frustum cull +
visible set + indirect build) = **0.026 ms**, `forward_plus_prepare` = **0.042 ms**,
and whole CPU `setup` = **0.218 ms**. D1/D3/D4 should stay parked until a real
scene makes area CPU prepare material again.

## Where we are today

The renderer is now unified around `nwn/model_renderer.cpp` +
`nwn/model_gpu_backend.cpp`. Static area rendering uses batched indirect draws,
bindless textures, per-draw `PreparedStaticDrawConstants` indexed by
`SV_InstanceID`, and cached indirect/static draw data. glTF/static PBR preview
uses the same renderer family rather than a separate `RenderModelRenderer`.

Key facts that shape the remaining plan:

- **Depth pre-pass exists and is viewer-enabled by default** unless
  `ROLLNW_VIEWER_DEPTH_PREPASS` disables it.
- **Compute and indirect-count plumbing exists** in gfx and Vulkan, including
  `cmd_draw_indexed_indirect_count`, `cmd_dispatch`, and the explicit
  compute->graphics barrier.
- **Forward+ GPU culling is default-on.** The GPU path now builds
  cluster membership in compute with froxel-AABB light tests.
- **Per-instance area data is still CPU-authored per frame/cache refresh**, but
  the measured cost is currently too small to justify moving D1/D3 ahead of PBR
  and Forward+ quality work.
- **Normal matrices are cached in the NWN per-draw paths.** Remaining normal
  matrix cleanup, if any, is a narrow preview/glTF audit rather than a broad hot
  path issue.

## Dependency graph

```
A (simplify)  ──────────────┐  (A1/A2/A3 done; A4 mostly done)
                            │
B1 indirect-count (gfx) ────┼──────────────► D3 GPU cull+submit (parked)
B2 compute scaffolding ─────┼──► C2 GPU light cull (done; default)
                            │
C1 depth pre-pass ──────────┴──► C2 ──► C3 froxel AABB cull (done)
                                         C4 local-light shadows (implemented; bounded)

D1 persistent instance buffers ─┐
D2 static mega vtx/idx buffer ──┴──► D3 GPU cull + DrawIndexedIndirectCount ──► D4 HiZ occlusion
            (done)                    (parked by measurement)                    (later)
```

Current recommended order: keep C4 as a visual-quality follow-up only. Leave
D1/D3/D4 parked until an area benchmark shows CPU prepare cost is material.

## Validation strategy (applies to every phase)

- **Screenshot parity**: render the same area/object before and after via
  `ViewerSession` screenshots; diff. No visual change for A/B/D phases; intentional
  (correct) change only for C phases.
- **Benchmarks**: the existing `mudl` area benchmark baseline + validation sweep
  (see recent commits "mudl area benchmark baseline helper", "mudl area validation
  sweep"). Record CPU prepare time and frame time per phase.
- **GPU timers**: `ScopedGpuTimer` + `get_completed_gpu_timer_results` — add a scope
  per new pass (depth prepass, cluster build, light cull, gpu cull).
- **CommandStats**: assert `draw_count` / `indirect_draw_call_count` /
  `dispatch_count` move in the expected direction.
- **Forward+ debug HUD**: `ForwardPlusDebugMode::cluster_light_count` /
  `depth_slice` to eyeball cluster occupancy and catch regressions in the GPU cull.

---

## Workstream A — Consolidation

Pure simplification. A1, A2, and A3 are done; the NWN side of A4 is done, with
only a narrow glTF/static preview normal-matrix audit remaining.

### A1. Collapse the Forward+ API surface
**Status: done.**
**Problem.** Every operation exists as a free function *and* a thin member that
forwards to it, *and* again as span / non-span overloads, wired with a `friend`
(`forward_plus_renderer.hpp:65-155`, `.cpp:628-706`). ~8 public entry points for
one operation.
**Change.** One method each: `prepare(frame, cmd, ctx, viewport, light_indices, policy)`
where an **empty `std::span` means "all lights"** (drop the `use_light_indices`
bool). Keep members (the renderer owns the arenas); delete the free functions and
the `friend`.
**Files.** `forward_plus_renderer.hpp/.cpp`; update callers in `viewer/session.cpp`
(the two `prepare_render_context` call sites).
**Validation.** Screenshot parity; ~150 fewer lines.

### A2. Unify the frame arena allocators
**Status: done.**
**Problem.** `FrameStorageArena` (`frame_storage_arena.*`) and
`ModelGpuBackend::FrameArena` (`model_gpu_backend.hpp:79`) are the same
double-buffered mapped-storage allocator implemented twice.
**Change.** Keep one — the gpu-backend version generalizes (it parameterizes
`BufferUsage` for indirect buffers). Promote it to `frame_storage_arena.*`, delete
the other. `ForwardPlusRenderer` and `ModelGpuBackend` use the shared type.
**Files.** `frame_storage_arena.*`, `model_gpu_backend.*`, `forward_plus_renderer.*`.

### A3. Pipeline cache keyed by descriptor
**Status: done.**
**Problem.** `ModelGpuBackend` holds ~20 named pipeline handles
(`model_gpu_backend.hpp`) — a hand-built combinatorial matrix of
{static,batched,skinned,PBR} × {opaque,cutout,transparent,water} ×
{onscreen,offscreen} × {shadow}, each created and destroyed explicitly.
**Implemented change.** Added `PipelineCache` keyed by `PipelineDesc`, centralized
reusable descriptor construction in `model_pipeline_desc.*`, and replaced the
public named getter matrix with `ModelPipelineKey { mesh_kind, material, target,
pass }` → `Handle<Pipeline>`. Internal slots remain as the backend's compact
storage/indexing detail.
**Files.** `pipeline_cache.*`, `model_pipeline_desc.*`,
`nwn/model_gpu_backend.*`, `nwn/model_renderer.cpp`, `viewer/scene_shadow.cpp`.
**Validation.** `nw-render-nwn` and `mudl` build cleanly; screenshot parity is
still the runtime check for any future pipeline-family edits.

### A4. Cache normal matrices in the per-draw paths
**Status: mostly done.** NWN meshes cache normal matrices by model/root transform
revision. Remaining work is only an audit of any glTF/static preview primitive path
that still computes `transpose(inverse(transform))` per draw.
**Files.** `nwn/model_loader.*`, `nwn/model_renderer.cpp`.

---

## Workstream B — GPU infrastructure (enabling)

### B1. `cmd_draw_indexed_indirect_count` in gfx
**Status: done.**
**Why.** GPU-driven culling produces the draw count on the GPU; D3 needs a submit
path that reads that count without a CPU round trip.
**Implemented change.** Added `cmd_draw_indexed_indirect_count(cmd, commands, count_buffer,
count_offset, max_draw_count, stride)` backed by `vkCmdDrawIndexedIndirectCount`
(already loaded). The count buffer uses `StorageSpan`, and the Vulkan context enables
the `drawIndirectCount` feature.
**Files.** `gfx.hpp`, `backends/vulkan/vulkan_context.cpp`.

### B2. Compute pass scaffolding + sync conventions
**Status: done.**
**Why.** Renderer compute passes need a clear compute-write -> graphics-read sync
contract.
**Implemented change.** Added the storage/dispatch path and an explicit
compute->graphics barrier used by the Forward+ GPU cull path.
**Files.** `gfx.hpp`, `vulkan_context.cpp`, Forward+ compute shader path.
**Risk.** Synchronization bugs are still the main risk for future compute passes;
validate new passes with fixed-scene parity and validation layers.

---

## Workstream C — Finish Forward+

### C1. Depth pre-pass
**Status: done.**
**Why.** Tight per-froxel depth bounds for clustering, and kills opaque overdraw of
the expensive PBR+light loop.
**Implemented change.** Depth-only pass before opaque, reusing the batched static geometry with
a depth-only pipeline (templates already exist: shadow depth-only +
`static_offscreen` pipelines). Then set the main opaque pass to depth-compare
`EQUAL`/`LESS_EQUAL` with depth-write off (or keep `LESS` and just benefit from
early-Z — measure both). Add a `ScopedGpuTimer("depth_prepass")`.
**Files.** `nwn/model_gpu_backend.*` (depth-only pipeline via A3 cache),
`viewer/session.cpp` (pass ordering, in `render()`), `viewer/area_render_scene.*`.
**Validation.** Screenshot parity (depth prepass must not change color output);
record opaque GPU time before/after in overdraw-heavy areas.

### C2. GPU light culling (the defining Forward+ piece)
**Status: implemented / GPU default.** Forward+ local-light binning defaults to
the GPU path. It uploads filtered light data, dispatches
`forward_plus_cull.cs.hlsl`, and lets compute write the cluster headers and
fixed-stride cluster light index buffer consumed by the lighting shaders.
**Why.** Move per-cluster light membership out of CPU scatter/re-upload while
keeping the CPU implementation as a reference/fallback path.
**Current shape.** The compute pass reconstructs each cluster's view-space
froxel AABB from the inverse projection and tests local-light spheres against
that AABB. The GPU path reports index buffer capacity separately from actual
occupied indices because it does not read the cluster buffers back to the CPU.
**Files.** `forward_plus_cull.cs.hlsl`; `forward_plus_renderer.cpp`
(dispatch + buffer mgmt, replaces CPU-produced light bounds for the GPU path);
`render_context.hpp` consumption is unchanged.
**Validation.** Fixed-scene `mudl area-benchmark` parity was run with
`--forward-plus-gpu-cull` / `--no-forward-plus-gpu-cull`. No-debug screenshots
matched byte-for-byte on `alue_bandithideo` and `aleu_beetlecave`; `aleu_welcome`
and `aleu_pathtoprov` differed by only 3 and 9 pixels respectively at 1280x720.
A 10-area minimal sweep from The Awakening passed on both CPU and GPU paths with
zero validation warnings/errors. Track `forward_plus_prepare`, `gpu_culling_frames`,
`cluster_light_index_capacity`, and GPU timer `forward_plus_cull`.

### C3. Froxel AABB culling refinement
**Status: done.**
**Why.** Replace the conservative screen-rect + light-radius depth-range scatter
with true sphere-vs-cluster-AABB tests now that froxel AABBs exist (C1/C2).
Expected effect: tighter light lists and lower per-pixel light loop work; verify
with cluster occupancy and GPU timer data.
**Implemented change.** `ForwardPlusCullParams` now carries the inverse
projection. `forward_plus_cull.cs.hlsl` reconstructs each cluster's near/far
view-space corners, builds a cluster AABB, and uses sphere-vs-AABB intersection
for light membership.
**Files.** `forward_plus_renderer.*`; `forward_plus_cull.cs.hlsl`.
**Validation.** DXC standalone compile passed. Focused `RenderForwardPlus.*` and
mudl GPU-cull CLI tests passed. On `ms_icecave_001` at 1280x720, CPU and GPU
no-debug screenshots matched byte-for-byte. CPU reference: 960 clusters, 48
lights, 2076 cluster-light indices, `forward_plus_prepare` 0.0080 ms avg. GPU
path: `gpu_culling_frames` 1.0, fixed-stride capacity 122880, `forward_plus_cull`
0.0136 ms avg, `forward_plus_prepare` 0.0015 ms avg.

### C4. Local-light shadows
**Status: implemented; projection quality is bounded by design.** Static prepared
shadow draws are now culled per local-light ortho frustum before submission.
Area scenes select from the visible/contributing light set when available, then
rank candidates by energy plus camera-target relevance.
**Why.** Forward+ point/area lights need their own shadow visibility instead of
only relying on directional cascades.

**Design (the part that avoids the NWN:EE "aura of shadows"):** the aura is *not*
a count problem — it's a weighting bug. Physically, N lights cast N shadows, but
each shadow only darkens *that light's* contribution to a pixel; the other lights
still fill it in, so shadows self-balance to one dominant soft shadow + faint
others. So the rule is: **each shadow attenuates only its own light's term in
`accumulate_local_light`** (multiply that light's `color*intensity*attenuation` by
its shadow visibility), never the whole surface. Get that right and 4 casters look
natural; get it wrong (shadow the total) and you get the aura.

**Budget / selection:** top-K≈4 by visible light importance (intensity × radius,
weighted toward the camera target), with authored `casts_shadow` treated as a
priority boost rather than a hard gate. K is a *cost* cap, not an aesthetic one.
**Projection:** current
implementation uses one orthographic top-down depth map per caster; dual-paraboloid
or cube maps remain deferred.

**Status: implemented (2026-06-15).** End-to-end: selection → slot → depth map →
per-light shader weighting. Pieces:
1. **Selection** — `resolve_local_shadows` (`viewer/scene_shadow.cpp`): top-K
   (`kLocalShadowCount = 4`) lights by importance (`intensity × radius`, weighted
   toward the camera target), writes `LocalLight::shadow_slot`. Area rendering
   passes the filtered visible-light index list so off-screen area lights do not
   steal slots from contributing lights.
   Selection is **importance-based, not flag-gated**: NWN tile/lantern lights almost
   never set the authored `casts_shadow` flag, so requiring it meant nothing ever
   cast. `casts_shadow` is now a priority *boost*; the brightest lights cast.
2. **Projection** — **orthographic top-down from just above the light** (`+0.25` lift),
   ortho box `[-r,r]²`. Two corrections over the original perspective-cone sketch:
   (a) a perspective cone from the low, wide-radius tile light covered only a tiny
   patch directly under it; the ortho box keeps the whole illuminated disk in-frustum.
   (b) The eye must sit *at* the light, not high above the room — an eye lifted a full
   radius captured ceilings/rafters above the light and cast dramatic but **spurious**
   beam shadows the light itself never sees. Eye-at-light limits casters to geometry
   actually below the lamp.
3. **`LocalShadowRenderer`** (`local_shadow_renderer.*`): K separate D32F maps
   (one bindless `Texture2D` per slot — the bindless table is `Texture2D[]`, so a
   layered atlas wouldn't fit). Area static casters render via the batched
   `render_prepared_shadow_draws` path (their geometry is in the static mega-buffer,
   **not** per-`ModelInstance` — `render_shadow_model_instance` draws nothing for
   them); dynamic/preview models use the per-model path.
4. **GPU** — `ForwardPlusLightGpu.params.z = slot + 1` (0 = none); the local maps'
   matrices + bindless indices ride in the existing `ShadowConstants` cbuffer
   (extended, b4 — avoids a new positional binding) via `upload_shadow_uniforms`.
5. **Shader** — `local_shadow_visibility` in `shadow.inc.hlsl` samples the receiver
   at its unoffset world position; `accumulate_local_light` (`render_pbr.ps`,
   `render_pbr_batched.ps`) multiplies **only that light's term** — **including
   the ambient-contribution branch** (see below).

**Key finding — shadow the ambient-contribution term.** The first cut shadowed only
the *diffuse* local term and produced almost nothing on real NWN interiors. Measured
on `prov_clothier`: zeroing every diffuse local light changes mean frame luminance
20.8 → 20.5 (~nothing), while zeroing the *ambient-contribution* local lights changes
it 20.8 → 1.0 — i.e. NWN interiors are lit ~entirely through the ambient-contribution
path (a soft, vertical-flattened point light), not the diffuse path. So for an occluder
to cast anything, that path must be shadowable. It now is, still per-light (each lamp
shadows only its own term, others fill in → no aura). Result: real, localized object
shadows under furniture, no ceiling artifacts, no acne.

**Follow-up harness (2026-06-18).** `mudl area-benchmark` and `area-sweep` can
disable only local-light shadow maps with `--no-local-shadows`, while keeping
directional shadows and Forward+ unchanged. Benchmark JSON now reports
`local_shadow` CPU timing, local-shadow draw counts, selected caster-light count,
submitted/cull counts, and `local_shadows_rendered`. The harness proved the old
receiver normal/slope bias erased visible local-shadow occlusion in `prov_clothier`
(AE 0 despite populated shadow maps); slot ids and frustum coverage were verified
with shader probes. The fix keeps the receiver sample unoffset and limits area
slot selection to visible/contributing lights when the area cache provides them.

**Still-open follow-up.** Shadows are correct but modest. A single ortho map per
lamp gives straight-down, not radial, shadows. Also note that the static glTF PBR
preview shader currently does not bind local-shadow state because its b4 cbuffer is
occupied by `SurfaceConstants`; add a separate protocol only if glTF scenes need
Forward+ local-shadow parity.

---

## Workstream P — PBR quality convergence

**Status: done.** The renderer has a pinned PBR corpus + ledger, explicit static
PBR environment policy, per-entry static PBR environment overrides,
rough-metal compensation, NWN specular/shininess mapping, PLT/equipment coverage,
water coverage, and limited NWN:EE `.mtr` sidecars mapped into the existing
renderer material protocol. glTF metallic-roughness pipelines exist and use
reference IBL for static PBR previews; NWN models and areas use scene-authored
lighting.

**Why.** The visible gap was material quality, not draw submission. The common
cases validated are metal/roughness glTF assets, NWN opaque/cutout materials,
water, PLT-tinted materials, MTR sidecars, and interiors dominated by authored
local lights.

**Change.**
1. Added explicit static PBR environment policy. Static PBR scenes use
   `reference_ibl` with `mudl --pbr-environment <ktx>` source selection and
   `--no-pbr-ibl` for fast shader-fallback runs; NWN models and areas use
   `scene_authored` so area weather, wind, water, fog, and day/night lighting are
   not coupled to generated glTF-style IBL.
   `mudl corpus` entries can now pin `static_pbr_environment.environment_path`
   and `ibl_requested` per asset; real sky/background rendering remains separate
   from this static PBR reference-lighting path.
2. Added rough-metal environment energy compensation; still verify against the
   material-sphere corpus before tuning real assets further.
3. Mapped NWN specular/shininess into the legacy PBR shader constants and kept
   diffuse/emissive/alpha/PLT behavior on the existing renderer material fields.
4. Added limited NWN:EE `.mtr` handling where assets provide material sidecars:
   resolve them through `materialname`, `texture0`, or `bitmap`; apply
   `renderhint`, diffuse overrides, and renderer-relevant scalar/color fields;
   and map normal/specular/roughness/emissive texture slots into the legacy
   material constants instead of recreating arbitrary EE shader behavior.
   `parameter float Specularity` maps to legacy specular strength, and
   `transparency` / `twosided` flags map to the existing transparent pass and
   two-sided lighting path. MDL `texture0` now has precedence over `bitmap`
   for the base diffuse texture; MTR sidecars that omit `texture0` preserve
   the MDL diffuse/PLT base texture while still supplying normal/specular/
   roughness maps. Custom EE shaders, height maps, and full metallic mapping
   remain out of scope until real assets require them.
5. Recorded fixed screenshots with
   `tests/test_data/user/development/pbr_corpus.json` before changing defaults.
   This covers representative glTF assets, NWN metal/specular fixtures,
   PLT/equipment, water-textured tile geometry, and an area local-light case.
   The 2026-06-18 full-IBL run updated
   `tests/test_data/user/development/visual_audit_ledger.json`; two full
   `/tmp` runs were byte-identical across PNG and JSON outputs.
   Added per-entry `mudl corpus` camera overrides and pinned the material-sphere
   and area local-light entries to useful diagnostic frames before renderer
   defaults are tuned against them.
6. Closed Stage 2 with two full corpus runs on 2026-06-18:
   `/tmp/rollnw-pbr-stage2` and `/tmp/rollnw-pbr-stage2-repeat`. Both processed
   7 entries with 0 failures / 0 unsupported, and `diff -qr` reported identical
   PNG and JSON outputs.

**Validation.** Stage completion is gated by the pinned screenshot corpus:
`./build/tools/mudl/mudl corpus tests/test_data/user/development/pbr_corpus.json
--output <dir> --module ./tests/test_data/user/modules/DockerDemo.mod --user
./tests/test_data/user --ledger <ledger.json>`. Use the material-sphere reference
scene for any future material tuning. No new environment passes were added during
closure, so there are no new GPU timer claims.

---

## Workstream D — GPU-driven submission

### D1. Persistent GPU instance buffers
**Status: parked by measurement.**
**Why.** Per-draw `PreparedStaticDrawConstants` is re-packed CPU-side every frame
and cached by signature (`refresh_prepared_static_draw_data`,
`allocate_static_draw_data` + memcpy). GPU-driven culling needs instance
transforms / bounds / material indices to live in **persistent** GPU buffers,
updated only on change.
**Change.** Promote the per-frame draw-data arena to a persistent buffer with
delta updates keyed on the existing signature; upload per-instance bounds alongside
(from `area_render_scene::bounds()`). The packing shape is already correct — change
the lifetime and the producer.
**Files.** `nwn/model_gpu_backend.*`, `viewer/area_render_scene.*`.

### D2. Single static vertex/index mega-buffer
**Status: done.** `AreaRenderScene::build_static_geometry` builds shared static
vertex/index buffers for area geometry and stores per-draw offsets for batched
indirect rendering.
**Why.** `build_prepared_indirect_draw_commands` already **bails when draws span
multiple vertex/index buffers** (`model_renderer.cpp` ~line 628) — batched indirect
already assumes shared geometry buffers. If all static area geometry lived in one
VB/IB addressed by `vertex_offset`/`first_index`, a single multidraw covers a whole
pass and the per-geometry-group rebinds disappear.
**Change.** Sub-allocate static geometry into one big VB and one big IB at load /
area-build time; store `vertex_offset`/`first_index` per mesh (already in
`PreparedDrawGeometry`).
**Files.** model upload (`nwn/model_loader.cpp` / asset cache),
`viewer/area_render_scene.*`.

### D3. GPU frustum cull + indirect command build
**Status: parked by measurement.**
**Why.** Move `build_prepared_indirect_draw_commands` (CPU,
`model_renderer.cpp:594`) to compute: read per-instance bounds (D1), test the
frustum, append `IndexedIndirectDrawCommand`s via an atomic counter, feed the count
buffer consumed by `cmd_draw_indexed_indirect_count` (B1). CPU cull stays as the
low-end fallback.
**Files.** new `shaders/area_cull.cs.hlsl`; `nwn/model_renderer.*`,
`viewer/area_render_scene.*`. Depends on B1 + D1 + D2.
**Validation.** Visible-set parity vs CPU cull (compare emitted command counts);
track area CPU prepare before/after on scenes large enough to make the current path
material.

### D4. HiZ two-phase occlusion (later)
**Status: later / not justified yet.** Only worth it once D1-D3 land and scenes are
dense enough to be occlusion-bound.
Build a HiZ pyramid from the depth prepass (C1); two-phase cull in the D3 compute.

---

## Next steps

1. **C4 shadow follow-up** — revisit projection only if top-down local shadows are
   not strong enough for a target scene; otherwise leave the bounded K=4 ortho
   path alone.

D1/D3/D4 stay parked until benchmarks show the area CPU prepare path is worth
moving to GPU.
