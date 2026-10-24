# RP-13: Initialize benchmark frame color

Status: closed, 2026-10-06. Implementation: `7ebb35786`.
[Parent](../rendering-performance-overhaul.md). RP-11/RP-12 policy acceptance
and final gameplay budgets remain separate open work.

ViewerSession loads the existing color target for embedded UI composition.
mudl's session benchmark/capture and area-edit benchmark never initialize that
target; normal mudl and the client's UI renderer do. Transparent particles can
therefore blend with previous or undefined image contents. RP-11 images differ
with swapchain history, despite matching logical counters and clean validation.

- [x] Clear the standalone frame before session rendering, using existing gfx
  calls; document the caller's color-initialization contract.
- [x] Verify frozen repeated captures and corrected 16/4-image comparisons;
  run relevant render checks and area-edit smoke.
- [x] Rebaseline frame timing and label old uncleared-target evidence.
  Corrected uncapped comparisons retained; RP-11 owns further count-policy
  acceptance. No count policy changed.

208 tests, six edit/hover scenarios, 40 captures, 6,480 capture logical pairs
and 19,200 timing pairs pass their scoped checks. Outdoor is pixel-exact; max
has 256 variable pixels, zero cross-mode differences outside that variation.
Max headless host median/p95: 4.590/6.027 ms; first budgets remain unmet.
[Evidence](../render-perf-evidence/README.md).

Cost: one existing clear render pass per standalone benchmark/capture frame;
no renderer state or new public option. Done: deterministic background/history,
valid measurements and no change to embedded composition semantics.
