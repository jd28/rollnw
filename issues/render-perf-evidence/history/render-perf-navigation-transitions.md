# RP-15: Navigation startup and door transitions

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: open. [Parent](../../rendering-performance-overhaul.md). [Evidence](../README.md).

- [x] Fix max-city startup rejection at quantized walkmesh boundaries (`c56017957`);
  regression fails before/passes after; 85 related tests pass.
- [ ] Attribute/avoid repeated work in 1.604–1.656 s preview startup (two nav builds).
  [Interior/outdoor route startup](../README.md):
  1.599–1.613 s / 0.591–0.599 s.
- [ ] Reduce measured two-tile door rebuild cost, 2.438–3.001 ms; opening frames
  take 5.117–6.956 ms. Preserve routes, clearance and height correctness.
- [x] Replacement-door closing/reopening regression verifies affected tiles,
  visual transitions, return navigation and authored-state/actor cleanup;
  104 related tests pass (2026-10-20). [Scope](render-perf-gameplay-acceptance.md#2026-10-20-activity-checks).
- [x] Verify full-client closing/reopening and record cold-build controls against
  the corrected reference (2026-10-21). This verifies behavior and cost, not a
  navigation optimization or paced gameplay acceptance.

Cost: navigation build profiling and bounded changes. Done: matched real
startup/transition evidence and gameplay acceptance.

## 2026-10-21 startup boundary inspection

The completed common-client cohort records current city startup at
1,640.157–1,761.525 ms in its first eight completed city starts. This is cold
preview startup inside each newly loaded client, not steady-frame cost or a
measured optimization. Both pacing modes use the same initialization path.
Raw setup rows are in `build/render-client-acceptance-2026-10-21/timing/`.

All eight paced gate runs per variant preserve five two-tile rebuilds each,
40 per variant. Rebuild median is 2.446 → 2.586 ms and maximum 3.299 → 3.325 ms;
no navigation speedup is established. Corresponding host-frame median falls
6.097 → 4.661 ms and maximum 8.834 → 5.994 ms from cumulative renderer changes.
Delayed GPU query rows are not assigned to individual door transitions. Full
startup arrays and transition rows are in the cohort's `timing-results.json`.

The two full builds in `preview_session.cpp` are not interchangeable.
`build_projected_door_links` clears the door obstacle states in a copied active
mask, builds a temporary world without door links, and projects both approach
points per valid door. `start_toolset_preview` then builds the final world using
the authored obstacle mask and the newly projected links. Terrain and other
obstacles repeat, but the required navigation output differs.

Do not remove a build or reuse its result unchanged. A follow-up profile must
separate geometry preparation, door projection and final tile construction before
choosing shared preparation or bounded projection work. Those are hypotheses,
with additional tile-dependency/lifetime bookkeeping as their cost; neither is
implemented or claimed faster. Current frame/pacing acceptance remains the
immediate priority.
