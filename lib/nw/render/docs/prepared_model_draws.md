# Prepared Model Draw Protocol

This document describes the current renderer-facing model frame protocol. The
exact field layout lives in `lib/nw/render/model_draw.hpp`; this note explains
what the records mean, who owns them, and where invalid data is dropped.

The protocol is not a scene graph and not a material database. It is a batch
transform from scene-owned model instance handles to flat draw records that
renderer passes can sort, filter, validate, and submit.

## Frame Transform

The common path is:

```text
ModelInstanceHandle span
  -> PreparedModelDrawList
  -> PreparedModelDrawRangeList
  -> PreparedModelSurfaceDrawList
  -> sorted PreparedModelSurfaceDraw span
  -> RenderModel skin table
  -> backend submission adapters
```

`PreparedModelDrawList` is collected from stable instance handles. It owns a
flat `draws` array and one offset table entry per input handle plus a terminal
offset. Hidden, stale, missing, or non-emitting inputs repeat the previous
offset so handle order stays stable without creating dummy draw records.

`PreparedModelDrawRangeList` turns those offsets into non-empty ranges. Each
range must contain draws with one instance handle, one instance kind, one
payload kind, and one source model index. Mixed ranges are dropped and counted.

`PreparedModelSurfaceDrawList` flattens valid ranges into one renderer-facing
surface stream. `PreparedModelSurfaceDraw` is the smallest common model draw
command. It carries:

- stable instance and source indices
- payload kind: `render_model` or `nwn_legacy`
- source draw, material, skin, and material override indices
- material mode and material payload class
- skinned flag, skin table index, transform, normal matrix, bounds, and shadow
  eligibility

Surface draws carry indices and small value data. They do not carry raw source
pointers, material payload pointers, or skin matrices.

## Sorting And Passes

The flat surface stream is sorted in place by
`sort_prepared_model_surface_draws_by_pass`. The current ordering partitions by
material pass, payload kind, skinned state, source model, material, override,
skin, range, source draw, and handle index.

Pass consumers take spans into that sorted array with
`prepared_model_surface_draws_for_pass_indices`:

- opaque and cutout: pass indices `[0, 2)`
- water: pass indices `[2, 3)`
- transparent: pass indices `[3, 4)`
- all: the full span

This keeps pass partitioning as views into one array. The owner of the array is
responsible for keeping it alive while those spans are used.

## Skin Matrix Binding

Skin matrices are not stored in surface draws.

`collect_prepared_render_model_skin_tables` scans the mutable surface array and
builds `PreparedRenderModelSkinTable` for RenderModel surfaces only. The table
owns frame-local contiguous matrix storage and range records. A skinned surface
uses `skin_table_index` to refer to one range.

Invalid, stale, unsupported, or bind-pose-fallback surfaces keep
`skin_table_index == kInvalidPreparedModelDrawIndex` and are counted in table
stats. The renderer path must handle that as an explicit fallback or drop, not
as undefined GPU state.

## Submission Adapters

RenderModel and NWN legacy payloads share the surface stream but submit through
different adapters.

For `render_model` payloads:

- `collect_prepared_render_model_surface_runs` groups contiguous RenderModel
  surfaces by `instance_source_index`.
- `PreparedRenderModelSurfacePacketList` validates the source primitive,
  material index, material payload, material mode, skin index, and material
  override before submission.
- `render_prepared_render_model_surfaces` submits the valid packet through the
  common `ModelRenderContext`.
- Viewer adapters call `render_prepared_render_model_surface_draws`, which
  accepts only the common `ModelRenderContext`.

For `nwn_legacy` payloads:

- `PreviewPreparedModelDraws` keeps the common draw list and a sidecar array of
  `nwn::PreparedDrawItem`.
- `collect_nwn_legacy_prepared_surface_draw_items` maps common surfaces back to
  sidecar draw items.
- Viewer adapters call `render_prepared_nwn_legacy_surface_draws`, which
  accepts only `nwn::ModelRenderContext`.
- The sidecar still owns legacy-only details such as PLT payloads, legacy
  material quirks, and the prepared NWN mesh payload used by the compatibility
  renderer.

The common stream decides visibility, pass grouping, transforms, and shadow
eligibility. The legacy adapter supplies only the data that has not yet been
lowered into common records.

## Error Policy

Boundary policy is drop-and-count. Invalid data should stop at the boundary
where it is detected and increment a stats counter.

Current counted cases include:

- stale model instance handles
- hidden or missing model instances
- missing source assets
- invalid draw offsets and mixed draw ranges
- invalid source model, primitive, material, skin, or override indices
- material payload mismatches
- missing or invalid NWN sidecar draw payloads
- invalid skin matrix ranges
- unsupported or invalid material modes

No renderer pass should infer policy from model names, texture names, or node
names. Source-specific behavior must be lowered into explicit common fields or
kept in a sidecar adapter with a counted fallback.

## Ownership And Lifetime

`PreviewScene` owns model instances, source model arrays, material overrides,
and NWN sidecar data for the viewer/tool path.

`PreviewPreparedModelDraws` owns the frame common draw list, range list, and NWN
prepared sidecar draw array.

`PreparedModelSurfaceDrawList` owns the frame surface stream and RenderModel
skin table. Spans returned from pass helpers, run collectors, and packet lists
borrow from this list and must not outlive it.

`ModelRenderContext` and `nwn::ModelRenderContext` own renderer submission
state. They do not own the scene instances or the frame draw stream.

## Current Follow-Ups

- [Remove Legacy Render Paths](../../../../issues/remove-legacy-render-paths.md)
- [Offline Model Compiler](../../../../issues/offline-model-compiler.md)
