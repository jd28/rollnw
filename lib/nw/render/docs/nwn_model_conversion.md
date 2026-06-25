# NWN Model Conversion

NWN MDL is both a source asset format and a compatibility runtime input. The
modern renderer path only wants the source asset part. The bridge keeps the
legacy sidecar alive for behavior that has not yet been lowered into explicit
common data.

## Current Entry Points

- `nw::render::nwn::ModelInstance` loads and owns the legacy NWN node tree,
  legacy meshes, PLT/source texture policy, emitter quirks, dangly execution,
  anchor lookup, and compatibility animation behavior.
- `nw::render::nwn::import_nwn_model_asset(mdl)` lowers the source MDL into
  `ModelAsset`.
- `nw::render::nwn::import_nwn_render_model_from_asset(mdl, texture_upload)`
  lowers through `ModelAsset`, uploads geometry/textures, and returns a
  `RenderModel`.

The default static NWN preview path uses `ModelAsset -> RenderModel` where it is
safe. Compatibility paths stay available for parity and unresolved quirks.

## Data Flow

```text
NWN MDL / MTR / TXI / PLT / emitter data
  -> nwn::ModelInstance sidecar
     source node tree
     legacy draw payloads
     compatibility animation and anchors
  -> import_nwn_model_asset
     ModelAsset nodes
     materials and texture source records
     primitives
     sockets
     deformers
     skins / skeletons / animation clips where supported
     particle systems
     shadow summary
  -> validate_model_asset
  -> upload_model_asset
  -> RenderModel
  -> common ModelInstance runtime record
  -> PreparedModelDraw / PreparedModelSurfaceDraw
  -> PBR renderer or bridge adapter
```

The sidecar is not the desired modern runtime shape. It is the source and
compatibility owner during the bridge.

## Lowered Common Data

| NWN data | Common data |
|---|---|
| trimesh / danglymesh geometry | `ModelAssetPrimitive` |
| render material classification | `MaterialMode` |
| diffuse, normal, roughness, emissive sources | `ModelAssetTextureSource` |
| conservative PBR factors | `Material` |
| dummy nodes / anchors | `ModelSocket` |
| supported skin meshes | `Skin`, `Skeleton`, `SkinnedVertex` |
| supported animation clips | `AnimationClip` |
| emitter nodes | `ModelAssetParticleSystem` / particle IR |
| dangly intent | `ModelDeformer` records |
| primitive shadow eligibility | `ModelShadowSummary` and primitive flags |

The common records carry renderer intent. They do not carry source-format
decisions by name.

## What Stays In The NWN Sidecar

These remain sidecar-owned until explicit common contracts replace them:

- humanoid body-part and equipment assembly policy
- legacy PLT rendering and resource-cache policy
- legacy emitter quirks not yet represented by the particle IR
- CPU dangly deformation parity/debug path
- anchor lookup fallback and source MDL node tree
- area static batching compatibility payloads

When a sidecar payload is missing at a bridge boundary, the path should drop,
count, or apply an explicit material fallback before Vulkan submission.

## Materials

NWN diffuse-era material data is lowered conservatively:

- material pass classification becomes `MaterialMode`.
- source texture references become `ModelAssetTextureSource` rows.
- authored MTR roughness/texture data is used when present.
- diffuse-only meshes use a neutral roughness policy rather than translating
  fixed-function shininess directly into PBR.
- PLT metadata can be represented in common `Material`, but legacy PLT behavior
  still needs sidecar compatibility until the production policy is final.

Material calibration is still open; do not tune shader behavior by model or
texture name.

## Skinning And Animation

Supported NWN skin meshes lower to common skinned primitives only when their
joint data fits the renderer's packed skinning contract and resolves against the
same source hierarchy. Unsupported or malformed rows are skipped and counted.

Animation clips lower into common clips when they can be represented by the
production backend. Clips from the loaded model and its supermodel chain are
imported against the loaded model's common skeleton keyed by source-node rows.
Missing node tracks stay at bind/static pose during sampling. `c_aribeth`
coverage verifies inherited `pause1` sampling against the legacy sidecar for
rigid primitive node rows. Zero-duration clips remain an explicit open issue
rather than an importer guess.

## Sockets And Attachments

NWN dummy nodes and anchors are useful source data. The modern representation is
a socket table addressed by stable indices. Bridge code still keeps source-node
names and sidecar lookup as fallback payload while attachments, particles, and
VFX move toward cached socket indices.

## Particles And Emitters

NWN emitter nodes lower into source-neutral particle definitions and compiled
particle effects. The field-by-field map is maintained in
[nwn_emitter_map.md](nwn_emitter_map.md). Runtime particle systems should use
common owner handles and attachment bindings instead of sidecar pointers.

## Deformers

NWN dangly meshes contain useful deformation intent, but CPU vertex mutation is
not the production target. The bridge records deformer intent beside common
model data. Foliage/card-like meshes lower to `vertex_shader_sway` intent.
Non-foliage legacy spring meshes lower to `secondary_motion_chain` intent but
currently render as static source geometry on the common RenderModel path with
the `nwn_render_model_static_deformer` warning. The accepted bridge policy is
to keep those assets on the modern path and not carry NWN-style per-frame CPU
vertex rewrites forward.

## Area Records

Area records now carry `ModelInstanceKind`, scene source indices, common
instance handles, visible RenderModel batches, and common prepared-surface
indices for color and shadow submission. Area static material/depth/shadow
bridges still materialize temporary NWN sidecar payload spans at the legacy
renderer boundary, but record identity and visibility are common data. The
remaining area work is static/dynamic traversal collapse and eventual sidecar
fallback removal, not more record-protocol migration.

## Diagnostics

`NwnModelAssetImportStats`, `ModelAssetValidationStats`,
`ModelAssetUploadStats`, and texture upload stats are the boundary reports.
They should explain dropped, skipped, unsupported, repaired, or fallback rows.

Do not silently bake out failures. The eventual offline compiler needs these
same diagnostics.

## Related Issues

- [Modern Runtime Sockets And Attachments](../../../../issues/modern-runtime-sockets.md)
- [NWN Modern PBR Material Calibration](../../../../issues/nwn-modern-pbr-material-calibration.md)
- [NWN Zero-Duration Animation Clips](../../../../issues/nwn-zero-duration-animation-clips.md)
- [Remove Legacy Render Paths](../../../../issues/remove-legacy-render-paths.md)
- [Offline Model Compiler](../../../../issues/offline-model-compiler.md)
