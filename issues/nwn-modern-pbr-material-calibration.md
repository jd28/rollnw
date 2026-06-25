# NWN Modern PBR Material Calibration

## Status

Open.

## Data Observed

- `c_aribeth` imported through the live NWN `ModelAsset -> RenderModel` path
  produces 18 RenderModel surfaces and no invalid surface drops in a Vulkan
  `mudl screenshot` run. The same prepared-surface submission reports 18
  opaque surfaces, zero cutout/water/transparent surfaces, zero material-mode
  invalids, zero material mismatches, zero material fallbacks, and zero fallback
  material payloads.
- `RenderModelLoader.ImportsCAribethDanglyPrimitivesWithRenderableSources`
  verifies the common asset has one shared `c_aribeth` albedo texture, neutral
  roughness `0.78`, metallic `0`, emissive `(0, 0, 0)`, and no normal,
  roughness, or emissive texture sources.
- The same test verifies all `c_aribeth` common materials are opaque,
  alpha-one, not double-sided, and not marked as material fallback payloads.
- The same test verifies the two `c_aribeth` dangly primitives have unit-length
  normals and unit-length tangents with valid tangent handedness, and that each
  tangent is orthogonal to its normal after NWN import-time tangent validation.
- The same test verifies those two `c_aribeth` dangly primitives come from
  `torso_g` and `head_g`, preserve source indices, vertex positions, UVs, and
  unit source normal directions, and are not using fallback material payloads.
- `RenderModelAsset.MaterialTextureUploadUsesNeutralFallbacksForMissingTextureRoles`
  verifies intentionally missing texture roles bind supplied neutral fallback
  indices without marking the material as a fallback.
- `PreviewModelAnimation.SamplesNwnModelAssetAnimationByNameThroughSceneBridge`
  verifies imported NWN animation clips sample into common node-world rows, and
  at least one animated rigid primitive resolves its draw transform from that
  sampled row instead of the static bind transform.
- `PreviewModelAnimation.DefaultRenderModelAnimationFallsBackToFirstClip`
  verifies preview startup selects clip 0 when the preferred animation-name list
  does not match an animated RenderModel.
- `mudl screenshot c_bodak /tmp/rollnw_c_bodak_render_model_default.png`
  completed on the default NWN `ModelAsset -> RenderModel` path on Vulkan,
  built an Ozz RenderModel animation backend for `c_bodak`, and
  submitted 16 RenderModel surfaces with zero invalid surface drops.
- Matched `c_aribeth` Vulkan screenshot captures show legacy direct and legacy
  prepared NWN rendering are pixel-identical (`AE=0`, `RMSE=0`), while legacy
  prepared vs. NWN `ModelAsset -> RenderModel` differs (`AE=48196`, normalized
  `AE=0.052296`, `RMSE=7045.74`, normalized `RMSE=0.107511`). The prepared
  legacy path selected 36 sidecar draws with zero missing or invalid sidecars.
- `mudl screenshot c_aribeth
  /tmp/rollnw_c_aribeth_render_model_tangent_recomputed.png` completed on
  Vulkan after import-time non-orthogonal tangent rejection was added. It
  submitted 18 RenderModel surfaces, reported zero invalid surface drops, zero
  material mismatches/fallbacks/fallback payloads, and visually showed the
  expected diffuse textures on the model.
- Current screenshot and counter evidence does not identify emissive, alpha
  mode, material fallback payloads, missing texture-role fallbacks, or invalid
  submitted surfaces as the cause of odd lighting on the opt-in NWN PBR path.
- The common material protocol now carries source-authored specular strength
  as a clamped `[0, 1]` scalar from NWN `Mesh` data into `Material`,
  `SurfaceConstants`, and the static PBR shader's dielectric F0 term. glTF
  imports keep the default scalar `1.0`.
- `PreviewLoadReport` now includes geometry diagnostics per model owner:
  primitive/static/skinned/deformed/shadow counts, vertex/index counts,
  node/socket/skin/skeleton/animation/deformer/particle counts, and NWN
  import-time normal/tangent repair counts.
- `mudl screenshot c_aribeth
  /tmp/rollnw_c_aribeth_render_model_specular_strength.png` completed on
  Vulkan after the specular-strength protocol change, compiled the affected PBR
  shaders, submitted 18 RenderModel surfaces, and reported zero invalid surface
  drops, material mismatches, material fallbacks, fallback payloads, and skin
  table errors.

## Problem

The modern path intentionally does not reproduce NWN fixed-function lighting.
However, NWN assets that only contain diffuse-era material data can still look
wrong under the PBR viewer if alpha mode, tangent/normal quality, culling,
environment intensity, or legacy material assumptions are lowered incorrectly.

The renderer needs a measured calibration pass before changing shader or
material policy.

## Next Checks

- Compare actual normal direction/tangent-frame behavior against legacy output
  for `torso_g` and `head_g` if lighting still looks wrong from front/side
  captures; the import path now verifies unit, handed, orthogonal tangent
  frames for the two c_aribeth dangly primitives.
- Decide whether NWN `ModelAsset -> RenderModel` needs additional explicit
  "legacy-authored diffuse asset" PBR material policy beyond preserving the
  source roughness/specular lowering already present in `Mesh` data.
- Check whether shader-side double-sided normal handling should be driven by
  imported source policy for specific hair/dangly surfaces.
- Only after the above, decide whether the fix belongs in NWN material lowering,
  vertex/tangent preprocessing, viewer lighting defaults, or shader policy.
