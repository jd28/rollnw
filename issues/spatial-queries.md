# Spatial queries: "what is near this point"

Status (2026-11-20): plan only, nothing implemented. Unblocks the largest entry
in [blocked combat subsystems](nwn1-combat-blocked-subsystems.md) — attacks of
opportunity, reach, Cleave, Mobility and the firing-into-melee penalty all wait
on one missing capability: **there is no way to ask which objects are near a
point.** Perception, area-effect spells and AI will all want the same thing.

Scope is the query capability and its index, not the features that consume it.
AoO additionally needs threat tracking and a reaction slot per round; that is a
separate design on top of this.

## What already exists

Worth being precise, because most of the groundwork is in place and the gap is
narrower than it looks.

- **Position and area are already a component.** `ObjectSpatialState`
  (`lib/nw/objects/ObjectComponentSystem.hpp`) carries `area`, `position`,
  `orientation`, `scale`, `velocity`, `angular_velocity`, stored as a dense
  `Vector<ObjectSpatialState>` behind a hash index. `set_position()` and
  `set_area()` are each a single field write, so they are clean choke points
  for index maintenance.
- **Areas already have per-type object collections** — `creatures`, `doors`,
  `placeables` and six more (`lib/nw/objects/Area.hpp`). On `main` these are an
  *authoring roster*: populated at GIT/JSON deserialization, torn down on
  `clear()`, with no live add/remove. Membership is currently tested by linear
  scan (`std::count(area->creatures...)` in `item_transfers.cpp`).
- **The `server` branch already has live membership.** `AreaMembership.hpp`
  defines the nine membership types, a prepare/publish transaction
  (`prepare_area_memberships` / `publish_area_memberships`) and per-collection
  `membership_revisions` on `Area`. That is the "area-wide list" layer, already
  designed. **This plan must build on it, not duplicate it.**
- **Tiles are 10.0 world units**, consistently: `k_tile_size` in
  `render/area_tile_grid.cpp` and `nav_tile_size` in `nav/NavTileBuild.hpp`.
  Tiles are dense row-major `width * height`, and navigation already keys them
  as `y * area_width + x` for route invalidation. Any tile indexing should
  reuse that key convention rather than invent a second one.
- **There is a 60 Hz action tick**, `nw::gameplay`'s documented
  submit -> prepare -> movement -> complete protocol, with movement requests and
  per-actor rows. Movement is therefore observable each tick. It is driven by the
  owner (`tools/client/preview_session.cpp`, `tests/gameplay_actions.cpp`);
  there is **no per-area simulation tick** yet.
- **Nothing resembling a neighbour query exists** anywhere, in C++ or smalls.
  `core.area` exposes width/height/tiles/lighting; `core.object` exposes
  get/set position with no notification. Detour is vendored but only Recast and
  Detour core are used — `DetourCrowd`, and so `dtProximityGrid`, is not linked.

## Query shapes the consumers actually need

The design follows from the radii, so they are worth listing before choosing a
structure.

| Consumer | Query | Typical radius | Frequency |
| --- | --- | --- | --- |
| Attacks of opportunity, reach | creatures within R of P | ~2 m melee, ~3 m reach weapon | very high — per movement step |
| Cleave / Great Cleave target pick | nearest other hostile within reach | ~2 m | per killing blow |
| Firing into melee (-4) | is any creature within melee reach of T, other than the shooter | ~2 m | per ranged attack |
| Mobility (+4 AC vs AoO) | consumes AoO, no query of its own | — | — |
| Perception (Listen / Spot) | creatures within earshot/sight | 10–40 m | low, periodic |
| Area-effect spells | objects within R of P | 2–20 m | low |
| Triggers / encounters | point inside a polygon | — | different problem — geometry, not radius |

**The dominant case is a ~2 m radius, queried very often.** That single fact
decides the next section.

## Recommended structure: per-area array sorted by position.x

This is the sweep-and-prune approach, and it is what NWN itself did — it kept
the area object list sorted by `position.x`.

- One dense array per area: `{ float x; ObjectHandle object; }` plus whatever
  of `y`/`z` is worth caching to avoid chasing the component row during a scan.
- **Query**: binary search the lower bound for `P.x - R`, walk forward while
  `x <= P.x + R`, reject on `|dy|` and `|dz|`, then compare squared distance.
- **Maintenance is nearly free because of temporal coherence.** Positions move
  a little per tick, so the array stays almost sorted, and insertion sort on
  almost-sorted data is effectively linear. That is the whole sweep-and-prune
  argument, and it is why NWN could afford to keep the list ordered.

Why it fits here specifically:

- **A 10 m tile is a poor filter for a 2 m query.** A tile bucket forces a
  query to touch up to four tiles and then scan every occupant of each, which
  for combat radii is most of the work with none of the savings. A sorted axis
  narrows to the actual radius automatically, with no cell-size tuning.
- **Per-area ownership is the direction the server work is already going.**
  `server-parallel-area-feasibility` states area ownership is required
  regardless, for instancing, local transfers and clear mutation boundaries. A
  per-area index inherits those boundaries instead of fighting them.
- **It is deterministic.** Sorting on `(x, handle id)` gives a total order, so
  query results are reproducible — which matters for checkpoint replay.
- **Nothing is retained that has to be rebuilt carefully.** The index is purely
  derived from spatial rows, so load, checkpoint restore and area instancing all
  just rebuild it.

Rough cost: an area with 200 creatures and a 2 m query scans a 4 m-wide band of
x. Even clustered in a melee, that is a handful of rows, from one cache-friendly
array.

## Where a tile grid still earns its place

Not as the primary combat filter — but it is not useless:

- **Tile-scoped iteration** for editing and streaming ("what is on tile T") is a
  genuinely different question from a radius query, and the renderer and nav
  already think in tiles.
- **As a rejection step for large-radius queries** if profiling shows the x-band
  getting long in dense areas — a 20 m AoE in a crowded area is the realistic
  case.
- If one is added, key it `y * area_width + x` to match navigation.

**Recommendation: do not build it in phase 1.** Add it only when a measurement
asks for it, and keep it behind the same query API so consumers never change.

## Decisions to settle before writing code

1. **One source of membership truth.** Drive the index from `set_area()` /
   `set_position()` plus the server branch's membership publish, rather than
   from the `Area` collections. Otherwise the index is a third roster that can
   disagree with the other two.
2. **Update policy: dirty-and-resort per tick, not sorted insert.** Eager
   sorted insertion is an O(n) move per position change, and positions change
   constantly during movement. Mark the area dirty, resort once, query after.
3. **Where the resort runs.** A new step in the action protocol
   (submit -> **reindex** -> prepare -> movement -> complete), owner-driven,
   since there is no per-area simulation tick yet. Combat queries must land
   after the movement step so they see current positions.
4. **Reads can create rows.** `get_position()` calls
   `get_or_create_spatial()`, so merely reading a position can add a spatial
   row mid-tick. Queries must never create rows, and new rows should join the
   index at the next reindex rather than mutating it during a scan.
5. **Keep `z` a reject filter, not an index axis.** NWN areas are effectively
   single-level; a second axis buys nothing and costs maintenance.
6. **Creature-only first, or all nine membership types?** Every combat consumer
   wants creatures. Doors and placeables matter for line-of-sight and AoE later.
   Suggest typing the API by object type from the start but indexing creatures
   first, so widening it is not a signature change.
7. **The index is derived, never serialized.** Rebuild on area load and
   checkpoint restore.

## Workload contract

Declared up front so phase 1 has something to be measured against, rather than
"it felt faster". Modelled on the
[renderer workload contract](render-perf-workload-contract.md): declare the
inputs, report distribution not just a mean, and name the budget.

**Area scales.** 16x16 and 32x32 tiles, matching the renderer's control areas,
giving 160 m and 320 m square world extents at 10 m per tile.

**Populations.** The repo has no populated-module fixture — the two area files
in test data carry no creature rosters — so these are synthetic, built the way
`benchmarks/navigation.cpp` builds its tile floors:

| Name | Creatures | Represents |
| --- | --- | --- |
| `sparse` | 16 | a quiet area |
| `normal` | 64 | ordinary gameplay |
| `crowded` | 256 | a large battle |
| `stress` | 1024 | not a supported population; the point where the structure breaks |

**Distributions** matter more than the count for a sorted-axis structure, so
each population is measured under three:

- `uniform` — spread across the area. The easy case.
- `clustered` — 80% inside one 10 m tile, a melee scrum. The realistic worst
  case for combat, and the one AoO will actually hit.
- `collinear` — **the degenerate case for sorting on x**: every object shares an
  x coordinate, varying only in y. The x-band stops filtering and the scan
  degrades to the full population. A column formation or a batch spawned at a
  single x reaches this honestly, so it is not a contrived input.

**Budget.** A radius query at `normal`/`clustered` must stay under the cost of
one `resolve_attack`, which the combat benchmarks already measure — that is the
natural unit, because AoO issues queries at roughly attack frequency. Report
median, p95 and p99, not a mean alone. Record the measured crossover where
`collinear` or `crowded` exceeds the budget; **that measurement is what
authorizes phase 3**, rather than a guess that a grid would help.

## Benchmarks

New benchmarks go in `benchmarks/spatial.cpp`, registered in
`benchmarks/benchmark_profiles.json` under a new `spatial` group. Follow
`navigation.cpp`'s `->Arg(...)` scale sweeps rather than hand-rolled loops.

| Benchmark | Args | Measures |
| --- | --- | --- |
| `BM_spatial_query_radius` | population 16/64/256/1024 | the headline query, uniform |
| `BM_spatial_query_radius_clustered` | same | the realistic combat case |
| `BM_spatial_query_radius_collinear` | same | the degenerate case |
| `BM_spatial_query_radius_by_radius` | radius 2/5/10/20 m | where a large-radius query stops being cheap |
| `BM_spatial_reindex` | population 16/64/256/1024 | per-tick resort cost, the thing that pays for the query |
| `BM_spatial_reindex_after_churn` | fraction moved 1/10/50/100 % | whether near-sorted really is near-linear |
| `BM_spatial_membership_change` | population | insert/remove cost against the server membership path |
| `BM_spatial_linear_scan_baseline` | population | phase 0, kept permanently as the control |

Keeping the phase 0 linear scan as a registered benchmark is deliberate: it is
the only honest answer to "is the index worth its complexity", and it stays
cheap to run.

**Registration details that matter.** Add a `spatial` entry to each profile's
`thresholds.groups` — 8.0% for `bench-pr`, 10.0% for `bench-nightly`, matching
the `navigation` group rather than the tighter `combat` 6.0%, because these are
new and their variance is unmeasured. Put `BM_spatial_query_radius`,
`_clustered` and `BM_spatial_reindex` in `hard`; the rest start `advisory` and
get promoted once their variance is known. Note that `compare_benchmarks.py`
defaults to `--min-baseline-ns 50` and ignores anything faster, so the sparse
populations must be sized to clear that floor or they will silently never
regress — report the measured baselines when registering, and size the inner
loop accordingly.

## Stress and correctness under load

Benchmarks answer "how fast"; these answer "still correct, and does it ever
fall over". In `tests/spatial_queries.cpp`, following the
`UnmanagedArrayTest.StressManyArrays` pattern already in the suite.

**Differential oracle — the most valuable test here.** The phase 0 linear scan
is kept as the reference implementation. For randomized positions and random
query centres/radii, the index result **set** must equal the linear scan's,
across all four populations and all three distributions. Any index bug — a
stale row, a missed reindex, an off-by-one in the band bounds — shows up here
and nowhere else. Seed the RNG explicitly so a failure reproduces.

**Invariants to assert after every mutation batch:**

- Sorted order holds, and the order is *total* — `(x, handle id)` — so results
  are reproducible for checkpoint replay.
- Index population equals live area membership. No object appears twice, and
  none is missing.
- No destroyed or area-changed object is ever returned.
- Query never mutates the index, and never creates a spatial row — `get_position()`
  calls `get_or_create_spatial()`, so this one is easy to violate by accident.

**Churn stress.** Interleave, over many iterations against a live index:
position changes, spawns, destroys, area changes, and queries. This is where
the reindex contract gets tested — specifically whether a query between a
mutation and the next reindex returns stale-but-consistent results rather than
garbage. Decide and then assert which of those two it is.

**Adversarial geometry.** Beyond `collinear`: all objects at the exact same
point; objects at extreme coordinates; NaN and infinity rejected at the
`set_position()` boundary (it already checks `is_finite`, so assert the index
cannot be fed one); zero and negative radii; a radius larger than the area.

**Scale ceiling.** Push population until the query exceeds the budget and
record the number. A structure whose breaking point is unknown is a structure
nobody can make a decision about.

## Phasing

**Phase 0 — API, tests and the measurement harness, with no index at all.**
Implement `creatures_within(area, center, radius, out)` as a linear scan over
the area's creature collection. Correct, slow, and it unblocks the consumers
immediately: firing-into-melee and Cleave target selection can be written and
tested against it. Phase 0 also builds the workload generators, registers
`BM_spatial_linear_scan_baseline`, and writes the differential-oracle and
invariant tests against the scan. **This is the phase that makes phase 1
decidable**, so it is not optional scaffolding.

**Phase 1 — sorted-x index behind that same API,** with the per-tick reindex.
Every phase 0 test passes unchanged, with the linear scan retained as the
oracle. The benchmarks say whether it was worth the complexity, and the stress
cases say where it breaks. This is the phase where the index's membership source
and dirty-tracking contract get pinned down.

**Phase 2 — the consumers**, cheapest first: the firing-into-melee penalty,
then Cleave / Great Cleave target selection, then attacks of opportunity proper.
AoO is its own design — threat ranges, provoking actions, a reaction slot per
round — and should not be smuggled in here.

**Phase 3 — a tile grid or a second axis, only if phase 1's measurements demand
it**, behind the unchanged API. The authorizing evidence is specifically the
`collinear` and `crowded` crossover numbers from the workload contract, not a
general feeling that a grid is more scalable.

## Open questions

- **Which branch?** `AreaMembership` lives on `server`, and both `Area.hpp` and
  `ObjectComponentSystem.hpp` are already modified there. Implementation wants
  to land on `server`, or wait for it to merge; doing it on `main` buys a
  conflict in exactly the files this touches. This plan is written on `main`
  because it is a document, not code.
- **Does the AoO design want a reach column in `baseitems.2da`?** There is no
  reach data today, and reach weapons are part of the same blocked item.
- **Should perception share this index or get its own?** Its radii are an order
  of magnitude larger and its frequency far lower, so the x-band argument is
  weaker there; it may prefer the tile grid when that arrives.
- **Accept the `collinear` degeneracy, or pre-empt it?** Sorting on one axis has
  a real worst case, and it is reachable without malice — a column of creatures,
  or a batch spawned at one x. The cheap mitigation is to pick the sort axis per
  area from the wider extent, or to re-pick it when the measured band length
  degrades; both add state and a reindex decision. Recommendation: measure it in
  phase 1 first and only then decide, because if `clustered` comfortably meets
  budget the degenerate case may simply not matter at supported populations.
