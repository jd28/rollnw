# Preview Scene Split Seams

## Problem

`viewer/preview_scene.cpp` is ~8k lines / 270KB — the largest file in the
renderer — not because any one algorithm is hard, but because four
separable concerns accreted next to the scene-bridge code. Each already
has a sibling file owning the other half of its concern. The duplication
this causes is not hypothetical: the phenotype bug
(viewer-creature-phenotype.md) landed twice because the assembly and
report paths re-implement the same flow. From the 2026-07 gfx/render
audit.

## Split Seams

- Load report/diagnostics (~lines 52-970 and 5336-6055):
  `PreviewLoadReport`/`PreviewLoadReportBuilder`, material/geometry/
  particle report construction, resource-dependency scanning. A complete
  reporting subsystem co-located with what it reports on.
  Seam: `viewer/preview_load_report.{hpp,cpp}`. While extracting, unify
  `add_dynamic_creature_report` over the real assembly path instead of
  its parallel re-implementation.
- NWN humanoid creature composition (~lines 2566-4098): body-part/
  armor-token/robe/cloak/PLT resolution. This is common asset-
  materialization domain logic, already flagged as parked debt by
  `preview_scene.hpp:52-57`; per the README's own contract its home is
  `nw::render` proper once PLT/socket/cross-skeleton policy is lowered
  into explicit common data (modern-runtime-sockets.md). Until then it
  can still move to its own viewer TU.
- Debug shape/spawn/trigger/encounter geometry assembly (interleaved
  ~4334-5032): builds exactly the buffers `SceneDebugRenderer` consumes;
  the renderer half already lives in `scene_debug.cpp`, the data-assembly
  half does not.
- Local-light record assembly / tile-light-slot glue (same region):
  parallels the already-separate `area_lighting.cpp` but lives inline.

## Notes

- Pure mechanical extraction: no behavior change, no new abstractions —
  cost is one round of include/CMake churn per seam, paid once.
- Order by value: load-report first (removes the duplication hazard),
  then debug-geometry and local-light (small, clean), humanoid
  composition last (largest, and its final destination depends on the
  sockets work).
