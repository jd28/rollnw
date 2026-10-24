# RP-07: Dynamic shadow surface ranges

Status: closed, 2026-10-03. Implementation: `e25ee4ed0`.
[Parent](../rendering-performance-overhaul.md). Discovered in
[RP-02](../render-perf-submission-profile.md).

Before this change, `scene_shadow.cpp::shadow_candidate_surfaces` returned the entire frame surface
list for dynamic models. Each model/cascade scanned it again to select its range.
Shadow submission is 6.64% of steady-profile self cycles; instruction annotation
puts 73% of that function's own samples on range rejection/iteration.

- [x] Build flat range bounds/indices once per prepared input batch; preserve
  source order for sorted/noncontiguous inputs and reject invalid range indices.
- [x] Use bounded surfaces for directional/local shadow submission; preserve
  static cached spans, skin tables, materials and fallback behavior.
- [x] Compare full frames and shadow CPU time; check draws/images, moving
  visibility, animations and edits. Keep only a measured win.

Cost: frame-owned range indices and one linear preparation pass, replacing
repeated whole-list scans. No retained invalidation state needed.
Done: measured improvement with parity, or a documented rejection.

Tier 1 plan (Ryzen AI 9 HX 370 / Radeon 890M, Release/Tracy off):

- Patterns: `model_draw.hpp:999` emits range-ordered surfaces; preview pass sorting
  may interleave them. `scene_shadow.cpp:273` already narrows static cache inputs.
- Decision/component: add frame-owned first/end indices in `model_draw.hpp`;
  sequentially scan surfaces once, indexed writes into the range table. Existing
  range checks preserve interleaved input order; bad range indices are ignored,
  empty/malformed bounds return empty. Standard vector allocation failure propagates.
- Map/flow: prepared surfaces -> bounds table -> borrowed subspans in both
  directional/local `scene_shadow.cpp` paths -> unchanged primitive submission.
  The scene owns surfaces; bounds live only through the shadow call. No new pointers.
- Data/cost: max city has 472 advancing models. Shared frame inputs are sorted
  by pass then instance; mixed-pass models span intervening rows. Local collection
  without supplied frame data groups rows by range. Bounds refresh per batch.
  Two size_t indices
  per range (16 bytes here), O(surfaces + ranges) preparation; no shader/GPU cost.
- Simplification: narrow existing spans; no copied/reordered surfaces, new draw
  API, hash map or retained cache. Noncontiguous inputs may still scan intervening rows.
- Build/verify: implement helper and parity tests in `render_prepared_model_draws.cpp`,
  integrate both callers, build; compare frames/CPU shadows plus command/image parity,
  visibility, animation and edit regressions. Roll back if benefit is not repeatable.

ASSUMPTION: fixed-camera fixtures remain useful for this local CPU experiment;
they do not prove complete gameplay budgets.


Results: five alternating hardware pairs, 1080p, 16 ms step, 60 warmup / 360
measured frames. Both binaries enable the same scene GPU timer. Medians of
per-run statistics; compact evidence (external artifact: `../render-perf-evidence/shadow-ranges.json`).

| Max city metric | Before -> after (ms) |
| --- | ---: |
| Host median | 7.296 -> 6.865 |
| Host p95 / p99 | 8.728 -> 8.206 / 11.974 -> 11.147 |
| Shadow CPU median | 1.719 -> 1.551 |

Shadow CPU improves in all five pairs; host improves in four. City/outdoor
controls are noisy, with no reliable whole-frame improvement claimed. Compare
within this run set; the earlier 6.71 ms baseline was a separate session.

165 tests pass without skips, including 360 permutation cases against full-list
filtering and visibility/animation/edit regressions. All 5,400 advancing paired
frames and 48 frozen capture frames preserve every non-timing field. 32 captures
have no Vulkan errors/warnings or failed/dropped commands. Outdoor/near images
match exactly; all city differences lie within six-repeat same-binary variation
(232 max-city / 99 city pixels), not exact city parity.

Self-check: bounds reset per call and reject invalid inputs; original filtering,
skin/material bindings and ordering remain intact. No payload copy, persistent
state or GPU change. Cost is 16 bytes/range plus one pass and a transient vector
allocation per shadow call. Interleaved ranges can still include unrelated rows.
Remaining CPU/GPU work and gameplay budgets stay with the open cluster.
Raw reports and verification driver: `/tmp/rollnw-shadow-ranges`.
