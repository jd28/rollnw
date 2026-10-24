# RP-03: Static main-pass batches

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: implemented; final acceptance open. [Parent](../../rendering-performance-overhaul.md). Depends on
[RP-02](render-perf-submission-profile.md).

Current `render_area_model_surface_draws` retains opaque/cutout indices, runs,
groups and root-instance data when the frame uses the complete cached static
lists without record filtering. `AreaSurfaceBatchCache` belongs to the area;
static/tile generation changes rebuild it and clear releases it. Partial views,
suppression and other passes retain fresh preparation. Runtime grouping uses
common model/material data, not source-format or asset-name policy.

The earlier decision to defer this work is superseded by
[the retained implementation and measurements](render-perf-draw-submission.md#2026-10-13-retained-complete-view-static-groups).
That comparison measured overview host median reductions of 13.86–19.13% across
32/64/96, all six pairs faster per size, with 151 tests and five exact captures.
Its small-view tail result was inconclusive; this is not a claim that all final
acceptance gates passed. Later root/transform reuse has separate costs and evidence.

The subsequent packed/indexed/indirect experiment was
[rejected and removed](render-perf-draw-submission.md#2026-10-21-diverse-content-comparison--rejected-and-source-removed).
It is not outstanding implementation work for this cache.

- [x] Retain compatible complete-static batches with generation-based invalidation;
  partial visibility uses the existing fresh batch path.
- [ ] Cover geometry/material overrides, topology, animation and shader changes;
  preserve ordering and explicit invalid-row fallback. Retain indices, not frame pointers.
- [ ] Compare grouping cost, whole frames, memory and images; exercise edits and
  scene replacement. Revert if stale output or cost outweighs the benefit.

Historical retained-capacity and first-build measurements are in RP-05. The
[completed small-view common-reference cohort](render-perf-gameplay-acceptance.md#2026-10-21-completed-small-view-common-reference-comparison)
passes all declared host/interval regression guards across five cases and both
cadences, with exact activity and separate memory records. This is cumulative
evidence, not an isolated cache result. Larger-view, final lifecycle/edit and
absolute pacing acceptance remain open. No second cache implementation is needed.
Cost remains retained scene tables, rebuilds on relevant edits and invalidation
maintenance.
