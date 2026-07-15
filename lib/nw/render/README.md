# nw::render

`nw::render` is the renderer-facing data and submission layer above
[`nw::gfx`](../gfx/README.md).

It is not a scene graph, gameplay system, editor framework, or source-format
owner. It turns explicit runtime data into GPU work through `nw::gfx` command
lists and backend resources.

## The Short Version

The current renderer path is:

```text
source assets
  -> common CPU records
     ModelAsset, ParticleEffectDef, sockets, deformers, animation clips
  -> runtime/upload records
     RenderModel, CompiledParticleEffect, ModelInstance, material overrides
  -> frame records
     PreparedModelDraw, PreparedModelSurfaceDraw, particle packets, light lists
  -> renderer passes
     model, particle, shadow, Forward+, fog
  -> nw::gfx
     buffers, textures, pipelines, command lists, swapchain
```

The common case is source-neutral runtime data. glTF and NWN are inputs to that
data, not renderer policy. NWN compatibility still exists where the common
runtime does not yet own equivalent data.

## Relationship To nw::gfx

`nw::gfx` owns the low-level graphics API boundary:

- devices, swapchains, buffers, textures, render targets
- shader and pipeline handles
- command recording and submission
- bindless texture indices and backend escape hatches

`nw::render` owns renderer meaning:

- model, material, animation, particle, light, shadow, and fog records
- GPU backend resources needed by renderer passes
- source-neutral frame protocols such as prepared model surface draws
- compatibility adapters for NWN sidecar data while bridge work is in progress

If a change needs to know about models, particles, PLT, shadows, Forward+, or
viewer scene data, it belongs in `nw::render` or above. If it only manages GPU
resources and command submission, it belongs in `nw::gfx`.

## Main Runtime Pieces

### RenderService

`RenderService` owns renderer singletons for the current graphics context:

- `ModelGpuBackend`
- `ForwardPlusRenderer`
- `ShadowRenderer`
- `LocalShadowRenderer`
- `ParticleRenderer`
- `FogRenderer`
- NWN compatibility GPU resources and render asset cache

It returns two model contexts:

- `ModelRenderContext`: common `RenderModel` / PBR submission context.
- `nwn::ModelRenderContext`: NWN sidecar compatibility context.

New common renderer code should use `ModelRenderContext` unless it actually
needs NWN sidecar payloads.

### Model Data

The modern model path centers on these records:

- `ModelAsset`: CPU-side source-neutral model data before GPU upload.
- `RenderModel`: uploaded model data and GPU handles.
- `ModelInstance`: scene/runtime placement, visibility, animation state,
  bounds, material overrides, and shadow summary.
- `ModelInstanceHandle`: stable scene-owned handle with generation checking.
- `PreparedModelDraw`: flat validated draw record collected from handles.
- `PreparedModelSurfaceDraw`: smallest renderer-facing model surface command.

`PreparedModelSurfaceDraw` is the shared frame protocol. It carries source kind,
source indices, pass/material data, transforms, skin index, bounds, and
shadow-caster state. The array is sorted in place by pass/material needs.

The renderer should not infer behavior from model, texture, or node names.
Source-specific quirks should be lowered into explicit common records or kept in
a compatibility sidecar with counters.

Object-level model, light, and icon references enter C++ through the
[Visual Asset Protocol](docs/visual_asset_protocol.md).
Smalls emits semantic rows; C++ loads concrete assets and turns them into
renderer-owned model instances and draw records.

### Particles

The particle system is source-neutral:

- authoring data: `ParticleEffectDef`
- compiled data: `CompiledParticleEffect`
- runtime data: `ParticleSystemInstance`
- render data: `ParticleRenderPacketList`

NWN emitters are imported into this protocol. The emitter field map lives in
[docs/nwn_emitter_map.md](docs/nwn_emitter_map.md), and the particle architecture
overview lives in [docs/particle_system.md](docs/particle_system.md).

### Lighting, Shadows, And Fog

`RenderContext` carries per-frame camera, viewport, lighting, shadow, and
Forward+ data. Area/viewer code prepares visible light lists and shadow caster
sets before renderer passes consume them.

Forward+ local light clustering, shadow rendering, local shadow maps, and fog
are renderer services. `nw::gfx` only sees their buffers, textures, pipelines,
and commands.

### Viewer Layer

`lib/nw/render/viewer` is the preview/tooling bridge used by `mudl` and renderer
tests. It owns preview-scene assembly, area visibility records, particle-owner
binding, debug drawing, screenshots, and parity toggles.

The viewer is not the production scene graph. It is where source adapters and
common renderer contracts are exercised against real NWN/glTF data until a
compiled runtime asset path exists.

### Render Asset Cache Lifecycle

`RenderService` owns one `nwn::RenderAssetCache` for its graphics context. Its
keys are resource identities, not source strings: DDS/TGA search-order lookups
use `Resref`, and texture interpretation and PLT colors are explicit key fields.
An empty resref returns the documented fallback, empty handle, or null pointer.

The common lookup path compares the observed `ResourceManager` generation,
hashes the resource/variant key, and returns the resident entry. The resource
generation advances whenever the visible registry is cleared or rebuilt. A
generation mismatch clears the cache before the next renderer context or cache
lookup can use stale data.

Within one resource generation, residency is retain-until-explicit-clear.
Individual `ViewerSession` instances do not invalidate the service cache;
applications clear it when replacing an exclusively owned scene, and renderer
shutdown clears it unconditionally. This keeps multiple live sessions correct
without assigning cache ownership to whichever session happens to load last.

`RenderAssetCache::clear()` is the batch lifecycle transform:

- Input: all cached texture rows, decoded images, particle models, and NWN
  model-loader resource analysis rows for the current epoch.
- Output: empty caches and a new nonzero epoch. Counter exhaustion fails
  loudly.
- Ownership: the cache destroys only textures it created. Fallback handles are
  caller-owned. Images and particle models are cache-owned.
- Ordering: mutation is serialized on the render thread. Clear waits for the
  graphics device to become idle before destroying GPU payloads.
- Cost: one device-idle wait plus linear work over resident texture, image, and
  particle-model entries. This is paid only at explicit clear, renderer
  shutdown, or resource-registry generation change.

Legacy `nwn::Mesh` texture handles and prepared draw-data caches record the
asset-cache epoch. They discard captured handles, bindless indices, or prepared
GPU data before reuse when the epoch differs. A mesh carries 8 bytes of epoch
state, and each prepared draw-data cache carries another 8 bytes.

`get_or_load_source_image()` and `get_or_load_particle_mesh()` return pointers
into cache-owned storage. These are frame-local borrows: callers must not retain
them across `clear()` or a resource reload. The current particle renderer and
mesh-particle bridge consume them in the same render call. Stable owning
pointers are used because the parsed `Image` and `ModelInstance` payloads must
survive hash-table rehash; they are not pointer-chased inside the cache clear
loop.

No LRU, per-session cache, reference-counted asset handle, or residency budget
is part of this contract. The current measurements and remaining decision are
tracked in [Render Asset Cache Residency Budget](../../../issues/render-asset-cache-residency-budget.md).

## Source Conversion Notes

- [Visual Asset Protocol](docs/visual_asset_protocol.md): Smalls to C++ to
  renderer model/light/icon row protocol.
- [docs/gltf.md](docs/gltf.md): glTF to `ModelAsset` / `RenderModel`.
- [docs/nwn_model_conversion.md](docs/nwn_model_conversion.md): NWN MDL to
  common model data plus compatibility sidecars.
- [docs/nwn_emitter_map.md](docs/nwn_emitter_map.md): NWN emitter fields to the
  neutral particle system.

## Existing Detailed Docs

- [docs/prepared_model_draws.md](docs/prepared_model_draws.md)
- [docs/particle_system.md](docs/particle_system.md)
- [docs/forward_plus_gpu_driven_roadmap.md](docs/forward_plus_gpu_driven_roadmap.md)

## Open Issues

- [NWN Modern PBR Material Calibration](../../../issues/nwn-modern-pbr-material-calibration.md)
- [NWN Zero-Duration Animation Clips](../../../issues/nwn-zero-duration-animation-clips.md)
- [Offline Model Compiler](../../../issues/offline-model-compiler.md)
- [Remove Legacy Render Paths](../../../issues/remove-legacy-render-paths.md)
- [Render Asset Cache Residency Budget](../../../issues/render-asset-cache-residency-budget.md)
