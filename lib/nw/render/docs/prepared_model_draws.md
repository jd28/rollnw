# Prepared Model Draw Protocol

This is the renderer-facing frame protocol for all models. The exact layouts
live in `model_draw.hpp`; this document defines their meaning, ownership, and
failure behavior.

## Batch Transform

```text
ModelInstanceHandle span
  -> PreparedModelDrawList
  -> PreparedModelDrawRangeList
  -> PreparedModelSurfaceDrawList
  -> sorted surface span
  -> PreparedRenderModelSkinTable
  -> source-indexed runs and validated packets
  -> ModelRenderContext submission
```

`PreparedModelDrawList` owns a flat draw array and an offset table with one
entry per input handle plus a terminal entry. Hidden, stale, missing, or
non-emitting inputs repeat the prior offset. This preserves input ordering
without dummy draws.

`PreparedModelDrawRangeList` emits one range for each non-empty handle. Every
draw in a range must share the instance handle and source model index. Invalid
offsets and mixed ranges are dropped and counted.

`PreparedModelSurfaceDrawList` flattens valid ranges. Each surface carries:

- instance, range, draw, source model, primitive, material, skin, and override
  indices
- material mode and payload class
- skinned and shadow flags
- world and normal transforms
- world-space bounds
- a frame-local skin-table index when assigned

Records contain indices and value data, never pointers into source models.

## Sorting And Passes

`sort_prepared_model_surface_draws_by_pass` sorts the flat array by material
pass, skinned state, source model, material, override, skin, range, primitive,
and handle index.

Pass helpers return borrowed spans into that one sorted array:

- opaque and cutout: `[0, 2)`
- water: `[2, 3)`
- transparent: `[3, 4)`
- all: the full span

The caller must retain the surface list while any pass span is used.

## Skin Tables And Submission

`collect_prepared_render_model_skin_tables` scans the surface batch and writes
one contiguous matrix array plus indexed ranges. Surfaces that are unskinned,
stale, invalid, over the joint bound, or intentionally using bind pose retain
`kInvalidPreparedModelDrawIndex`; each case is counted.

`collect_prepared_render_model_surface_runs` groups adjacent surfaces by
`instance_source_index`. Packet collection validates the referenced
`RenderModel` primitive, material, skin, override, and payload before common
PBR or shadow submission.

## Error Policy

Every invalid boundary is drop-and-count. Counted cases include:

- stale or hidden instances and missing source models
- invalid offsets, mixed ranges, and invalid source indices
- primitive, material, skin, override, or matrix-range mismatches
- material payload or shadow-state mismatches
- unsupported material modes

No pass infers behavior from model, texture, or node names. Source-format
policy must already have been lowered into common fields before this protocol.

## Ownership, Cost, And Constraints

`PreviewScene` owns source models, instances, and material overrides.
`PreviewPreparedModelDraws` owns draw and range arrays.
`PreparedModelSurfaceDrawList` owns surfaces and the skin table. Packet and
run spans borrow these arrays. `ModelRenderContext` owns GPU submission state,
not scene or frame records.

Collection, range validation, skin assignment, and run construction are linear
in their input counts. Sorting is `O(surface_count log surface_count)`. Memory
is linear in visible draws, surfaces, and referenced skin matrices. These are
algorithmic costs; runtime performance has not been measured here.

## Follow-Up

- [Offline Model Compiler](../../../../issues/offline-model-compiler.md)
