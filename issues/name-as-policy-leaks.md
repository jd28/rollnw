# Name-As-Policy Leaks Outside The NWN Sidecar

## Problem

AGENTS.md and the render README both forbid deciding renderer behavior from
model/texture/node name strings; the NWN compatibility sidecar is the
sanctioned escape hatch. Two audited sites make name-derived decisions that
land in *common* data or common code paths, and neither is counted in
import stats (the conversion doc requires "do not silently bake out
failures"). Found in the 2026-07 gfx/render audit; verified in source.

## Findings

- `lib/nw/render/nwn/model_loader.cpp:604-655`
  (`looks_like_water_material`/`contains_water_token`): "water"/"watr"
  token matching against node/bitmap/renderhint/material names selects
  `MaterialMode::water` via `classify_nwn_material_impl` (:1465-1468).
- `model_loader.cpp:549-580` (`has_foliage_token`,
  `select_dangly_deform_policy`): "bush"/"foliage"/"leaf"/... tokens select
  between `ModelDeformerKind::vertex_shader_sway` and
  `secondary_motion_chain`.
  Both `MaterialMode` and `ModelDeformerKind` are the common source-neutral
  enums shared by the ModelAsset/RenderModel path (per
  `docs/nwn_model_conversion.md`'s "Lowered Common Data" table) — the
  name-derived policy leaks past the sidecar into shared renderer data.
  The comment at :571-573 acknowledges the compromise, but there is no
  counter in `NwnModelAssetImportStats` recording how often it fires.
- `lib/nw/render/viewer/preview_scene.cpp:3921-3936`: wing-model meshes are
  gutted (vertex/index data zeroed) by node-name prefix match —
  `starts_with("gargoyle_") || starts_with("wing_shadow")` — undocumented,
  and inert for functionally identical content named differently. The
  table-driven pattern already exists nearby
  (`resolve_nwn_appearance_hand_item_visual_policy`).

## Fix

- Minimum: add counters (`water_name_heuristic_count`,
  `foliage_name_heuristic_count`, wing-strip count) to the import/preview
  stats so diagnostics and the future offline compiler
  (offline-model-compiler.md) can see and retire the heuristics.
- Move the wing-strip special case into an explicit table keyed by
  appearance row, following the hand-item policy pattern.
- Longer term: decide whether water/foliage classification becomes
  sidecar-only pre-pass data or explicit per-material authored data when
  MDL lowering moves to the offline compiler.

## Open Questions

- Is there any authored NWN signal other than names (renderhint values,
  material files, 2DA rows) that could replace the water/foliage tokens
  outright? If not, the heuristic stays — but counted and contained.
