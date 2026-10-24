# RP-03: Static main-pass batches

Status: open. [Parent](rendering-performance-overhaul.md). Depends on
[RP-02](render-perf-submission-profile.md).

`viewer/preview_model_draws.cpp` rebuilds filtered indices, model runs and static
compatibility groups each call. Asset/material data is often stable; visibility
and surviving order are not. Measure before choosing a retained layout.

[Measured](render-perf-main-grouping.md): max group construction ~0.129 ms,
filter/run construction ~0.124 ms. Defer retained groups behind RP-05's repeated
rigid primitive draws; no retained-cache implementation or speedup claimed.

[Current packing audit](render-perf-evidence/README.md) records
an unselected RP-05 candidate: up to 1,122 fewer overview opaque draws within shared
models. A structural bound alone does not justify implementation complexity.

- [ ] If justified, compile stable compatibility once and remap visible indices.
- [ ] Cover geometry/material overrides, topology, animation and shader changes;
  preserve ordering and explicit invalid-row fallback. Retain indices, not frame pointers.
- [ ] Compare grouping cost, whole frames, memory and images; exercise edits and
  scene replacement. Revert if stale output or cost outweighs the benefit.

Cost: scene tables, edit-time rebuilding and invalidation maintenance.
Done: measured improvement with parity, or a recorded no-change decision.
