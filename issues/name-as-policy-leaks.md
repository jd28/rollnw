# Name-As-Policy Leaks Outside The NWN Sidecar

Status: resolved in working tree.

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

- Done: added counters (`water_name_heuristic_count`,
  `foliage_name_heuristic_count`) to the NWN import stats and preview
  geometry report, and surfaced them in `mudl` JSON/debug UI plus preview
  info events/logging.
- Done: moved the wing-strip special case into an explicit NWN-side row
  policy. The live preview path and load-report path now report
  `nwn_wing_attachment_policy` with `stripped_non_render_meshes`, and the
  strip/count uses authored mesh `render` metadata instead of node-name
  prefixes.
- Done: removed the `gargoyle_` / `wing_shadow` prefix policy from the
  viewer path.
- Done: added targeted tests for water/foliage counters and wing row policy.
- Longer term: decide whether water/foliage classification becomes
  sidecar-only pre-pass data or explicit per-material authored data when
  MDL lowering moves to the offline compiler.

## Open Questions

- Is there any authored NWN signal other than names (renderhint values,
  material files, 2DA rows) that could replace the water/foliage tokens
  outright? If not, the heuristic stays — but counted and contained.
