# glTF Conversion

glTF is the cleanest current source path into the common renderer data. It does
not define a separate runtime renderer path; it lowers into `ModelAsset`, uploads
to `RenderModel`, and then renders through the same prepared surface protocol as
other common models.

## Entry Points

- `nw::render::gltf::import_gltf_model_asset(path)` decodes glTF into a
  CPU-side `ModelAsset` without requiring a graphics context.
- `nw::render::gltf::import_gltf_model_asset_with_stats(path)` is the same
  CPU-side import path with source/import diagnostic counters.
- `nw::render::gltf::import_gltf_render_model_from_asset(path, desc)` decodes
  through `ModelAsset`, uploads geometry and textures, and returns a
  `RenderModel`.
- `nw::render::gltf::import_gltf(path, desc)` is the older direct importer kept
  for focused importer tests, not for viewer or mudl preview routing.

The viewer and mudl preview path is `ModelAsset -> RenderModel`; there is no
runtime glTF preview switch.

## Data Flow

```text
.gltf / .glb
  -> cgltf parse
  -> ModelAsset
     materials
     texture source table
     primitives
     nodes
     skins / skeletons
     animation clips
     bounds and shadow summary
  -> validate_model_asset
  -> upload_model_asset
  -> upload_model_asset_material_textures
  -> RenderModel
  -> ModelInstance
  -> PreparedModelDraw / PreparedModelSurfaceDraw
  -> PBR renderer
```

## ModelAsset Mapping

| glTF data | Common data |
|---|---|
| mesh primitives | `ModelAssetPrimitive` rows |
| positions, normals, UVs, tangents | `Vertex` or `SkinnedVertex` streams |
| indices | `ModelAssetPrimitive::indices` |
| node hierarchy | `Node` rows and primitive node indices |
| PBR metallic-roughness material | `Material` |
| base color / normal / metallic-roughness / occlusion / emissive textures | `ModelAssetTextureSource` plus `ModelAssetMaterialTextureSources` |
| skins | `Skin`, `Skeleton`, skinned primitive payloads |
| animations | `AnimationClip` tracks |
| material alpha mode | `MaterialMode` |

glTF socket markers are intentionally not inferred from node or bone names.
glTF does not define locator semantics, so a separate authoring contract must
specify the accepted node or `extras` annotation and its validation rules before
the importer emits `ModelSocket` rows. See
[gltf-socket-authoring-contract.md](../../../../issues/gltf-socket-authoring-contract.md).

The source texture table stores file paths or encoded bytes. GPU texture handles
are not created until upload.

## Materials And Textures

glTF PBR material values map directly into `Material`:

- base color factor and texture become albedo.
- metallic and roughness factors plus texture data become the common surface
  texture path.
- normal, occlusion, and emissive values map to their matching fields.
- alpha mode maps to opaque, cutout, or transparent.

`upload_model_asset_material_textures` packs metallic-roughness and occlusion
source data into the renderer's ORM-style surface texture and binds fallback
textures when a role is missing.

## Skinning And Animation

Skinned glTF primitives become `SkinnedVertex` streams and `Skin` rows when the
joint data fits the renderer skinning contract. The shader contract currently
supports at most `kModelMaxSkinBones` matrices per skin; out-of-contract skin
data is rejected or falls back before it reaches GPU submission.

Animation clips compile into the common animation structures. Runtime playback
state lives on `ModelInstance`, and Ozz is the production animation backend.
Reference sampling remains a debug fallback.

## Runtime Submission

Once uploaded, glTF-origin models are no longer special:

1. Scene code owns a `ModelInstanceHandle`.
2. The model draw collector validates handles and emits `PreparedModelDraw`
   rows.
3. Surface collection flattens draw ranges into `PreparedModelSurfaceDraw`.
4. `render_prepared_render_model_surface_draws` submits PBR surfaces through
   `ModelRenderContext`.

The renderer should not branch on "glTF" at draw time.

## Validation And Fallbacks

`GltfImportStats` counts importer-boundary drops and repairs, including repeated
or over-depth node graph references and non-finite accessor payloads.
`validate_model_asset` counts invalid primitive, material, socket, skin, and
animation rows before upload. Upload stats then count geometry and texture
upload failures.

Out-of-range or unsupported data should be rejected, dropped, or counted at the
conversion/upload boundary. It should not reach Vulkan submission as undefined
state.

## Related Issues

- [Remove Legacy Render Paths](../../../../issues/remove-legacy-render-paths.md)
- [Offline Model Compiler](../../../../issues/offline-model-compiler.md)
- [NWN Zero-Duration Animation Clips](../../../../issues/nwn-zero-duration-animation-clips.md)
