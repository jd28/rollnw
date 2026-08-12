# Forward+ And GPU-Driven Rendering

Scope: `lib/nw/render/`.

## Current State

The renderer uses one source-neutral model path:

```text
RenderModel + ModelInstance arrays
  -> prepared draw/range/surface arrays
  -> ModelGpuBackend PBR color or shadow pipeline
  -> nw::gfx command list
```

NWN and glTF differ only before `RenderModel`. There is no NWN-specific GPU
backend, static mega-buffer renderer, direct source-model submission, or depth
pre-pass path.

Forward+ local-light clustering is the default. Its GPU policy uploads the
filtered light batch, dispatches `forward_plus_cull.cs.hlsl`, and writes the
cluster headers and fixed-stride light-index buffer consumed by the PBR shader.
The CPU policy remains the explicit reference path.

The model backend has ten pipeline slots: static/skinned crossed with
opaque/cutout/transparent color passes and opaque/cutout shadow passes. Water
uses the common transparent PBR submission policy. Pipeline creation is
centralized through `PipelineCache`.

## Observed Cost

The last recorded `ms_4city` gameplay-camera measurements before the renderer
path collapse were:

- area visibility/prepare: 0.026 ms
- Forward+ prepare: 0.042 ms
- total CPU setup: 0.218 ms

Those observations do not measure the current implementation and are not a
performance claim for this revision. Re-run the same area benchmark before
prioritizing CPU submission work.

The current common frame transforms are:

- linear collection over visible instance handles
- linear range and surface construction
- `O(n log n)` surface sorting
- linear source-run and skin-table construction
- one submission pass per selected material-pass span

## Completed Infrastructure

- Forward+ API and frame-storage arena consolidation
- descriptor-keyed pipeline cache
- indirect-count and compute dispatch support in `nw::gfx`
- explicit compute-to-graphics barriers
- GPU froxel-AABB local-light culling
- local-light shadow selection and rendering
- common cached instance/node transforms and normal matrices
- one prepared-surface protocol for preview, area, VFX, and particle meshes

## Parked Work

### Depth Pre-Pass

There is no model depth pre-pass now. Reintroducing one adds another full
geometry submission plus pipeline state and synchronization. Do it only after a
representative overdraw-heavy scene shows a measurable opaque-pass benefit.
Success requires unchanged color output and lower measured GPU frame cost.

### Persistent Instance Buffers

Prepared model data is currently frame-owned CPU data uploaded through the
existing frame arenas. Persistent GPU instance tables would add dirty tracking,
lifetime synchronization, and partial update logic. Keep this parked until
profiled CPU packing or upload cost is material.

### GPU Frustum And Occlusion Culling

A GPU culler would read persistent bounds, compact indexed draw commands, and
feed `cmd_draw_indexed_indirect_count`. It depends on a justified persistent
instance protocol. A HiZ pass adds a depth pyramid and another synchronization
boundary. Neither is justified by the recorded CPU measurements.

## Validation Contract

Any future phase must use the same real scene before and after:

- screenshot comparison for unintentional color/depth changes
- CPU area-prepare and total setup timings
- GPU pass timers
- command, indirect-call, and dispatch counts
- validation-layer output
- visible-set and light-cluster counts against the CPU reference

Out-of-range indices, exhausted buffers, and invalid GPU ranges must be rejected
or dropped with counters. Do not add a runtime path selector until two
implementations actually need to coexist for a measured comparison.
