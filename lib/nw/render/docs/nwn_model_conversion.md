# NWN Model Conversion

NWN MDL is a source asset format. It is parsed and lowered into the same
source-neutral model data used by every renderer pass; the renderer does not
retain or submit an NWN node tree.

## Data Transform

```text
binary or ASCII MDL
  -> nw::model::Mdl
  -> import_nwn_model_asset
  -> ModelAsset validation
  -> upload_model_asset
  -> RenderModel
  -> ModelInstance handles
  -> PreparedModelDraw / PreparedModelSurfaceDraw arrays
  -> common PBR and shadow submission
```

`import_nwn_render_model` is the convenience composition of the asset import
and upload steps. Preview scenes, areas, creatures, VFX actor models, and mesh
particles all use this transform.

The common case is a batch of model instances that reference uploaded
`RenderModel` rows by index. One model asset may be referenced by many
instances. Frame collection walks the handle batch linearly and emits flat draw,
range, and surface arrays.

## Lowered Data

The importer writes explicit common records for:

- geometry, primitive bounds, material and shadow state
- texture sources, PLT parameters, and limited MTR material inputs
- hierarchy nodes, sockets, skeletons, skins, and animation clips
- authored lights and particle systems
- deformer intent

NWN diffuse textures are color data and use an sRGB texture view so lighting
operates on linear values. PLT textures are index/layer data, not color; the
upload boundary keeps those payloads in linear RGBA8 and the shader converts
the selected palette color to linear space.

Names may be used only inside the source importer when the format provides no
authored field. Water and foliage name heuristics are counted in
`NwnModelAssetImportStats`; downstream renderer code receives only the
resulting material/deformer value.

Dangly meshes lower to either `secondary_motion_chain` or
`vertex_shader_sway`. Valid foliage sway runs on the common GPU path.
Secondary-motion chains that cannot be represented are counted as
`unsupported_deformer_count` and render as static source geometry. No second
renderer is selected.

## Bounds And Failure Policy

Input limits are the common model limits in `model_asset.hpp`, including the
64-joint skin bound and bounded primitive, texture-source, particle-system, and
deformer tables.

Invalid rows are rejected or dropped at the boundary that can identify them:

- invalid or empty mesh geometry increments the matching skipped-mesh counter
- invalid skin lanes, joint mappings, or bounds drop that skin primitive
- missing texture inputs bind the documented fallback and increment import
  diagnostics
- table overflow drops the excess row and increments its overflow counter
- unsupported source behavior remains static and increments an explicit
  unsupported counter
- invalid prepared indices, stale handles, and payload mismatches are dropped
  and counted before submission

`NwnModelAssetImportStats::complete()` reports whether the import crossed any
of the defined loss boundaries. Normal/tangent repair and counted source-name
heuristics are visible diagnostics but are not separate render paths.

## Ownership And Lifetime

- `nw::model::Mdl` owns parsed source data for the duration of import.
- `ModelAsset` owns source-neutral CPU arrays until upload completes.
- `RenderModel` owns uploaded resource handles and immutable runtime metadata.
- `PreviewScene` owns model arrays and generation-checked `ModelInstance`
  handles.
- Prepared draw, range, surface, skin-table, and packet arrays are frame-owned.
  Returned spans borrow those arrays and must not outlive them.
- `nwn::RenderAssetCache` caches NWN texture decoding and uploaded particle
  `RenderModel` assets for one renderer resource generation.

The import cost is linear in source nodes, vertices, indices, controllers, and
texture inputs. Upload cost is proportional to emitted GPU payload bytes. Frame
cost is linear in visible instances and emitted surfaces. These are complexity
statements, not measured performance results.

## Related Documents

- [NWN Model Findings](nwn_model_findings.md)
- [Prepared Model Draw Protocol](prepared_model_draws.md)
- [NWN Emitter Map](nwn_emitter_map.md)
- [Offline Model Compiler](../../../../issues/offline-model-compiler.md)
