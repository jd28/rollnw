# Terrain painter: apply SET primary rules

Status: planned, 2026-10-25. Tier 1 extension of the existing tile-authoring
pipeline. Baseline: `main` at `ef3f11d58eaaf745b6a6b3acb4311043dbf18205`.
This document plans the implementation; rule application is not implemented.

The painter currently edits corner terrain/heights and then searches for tiles
matching that topology. It never loads or applies `[PRIMARY RULES]`. Authored
rules can change neighboring terrain or height so that suitable transition
tiles exist. Omitting this stage can reject edits which the tileset author
intended the toolset to resolve automatically.

## Framing and observed data

**Goal:** apply the tileset's primary rules during topology authoring, then use
the current fitter and atomic edit pipeline. Successful edits must preview,
commit, undo, redo, and save the complete resulting tile batch consistently.
Existing areas must remain loadable and renderable without rewriting them.

**Limit:** this is not a replacement tile solver, a renderer change, or a general
rule engine. Do not implement `[SECONDARY RULES]`, add per-tileset exceptions,
or invent transitions when no authored rule/tile supports them. If the observed
semantics cannot yet be reproduced, retain atomic rejection for those cases
and leave the compatibility question open below; do not claim Aurora parity.

**Platform:** the Linux desktop client runs the current edit/preflight/write
sequence on its UI thread. Areas are row-major tile arrays with a shared corner
lattice; tile orientation is one of four quarter turns. SET data is shared,
read-mostly authoring data, while previews may rebuild candidate edits repeatedly
during a gesture. Responsiveness is a latency concern. No new thread, lock, or
background job is justified here. CPU model/cache sizes and a latency target
were not measured for this planning task.

### Corpus audit

Read the 33 SET resources selected by the installed `data/nwn_base.key`, resolving
resource type 2013 through the KEY/BIF tables. This was not a scan counting
duplicate historical copies in different BIFs. Parse primary rows, resolve
terrain names case-insensitively, and compare `(Adjacent, AdjacentHeight)` with
`(Changed, ChangedHeight)` to identify rows which change a neighbor.

KEY SHA-256:
`09cdafb6dbfb9cdb544993154c514c65ef9fd5be4e0c1b8fd971ed05aff90935`.
The audit excludes module/HAK/override resource precedence and custom content.

| Observation | Count |
| --- | ---: |
| SET resources | 33 |
| SETs with primary rows | 23 |
| SETs with at least one changing row | 12 |
| Total primary rows | 699 |
| Rows preserving adjacent terrain and height | 438 |
| Rows changing adjacent terrain and/or height | 261 |
| Rows changing adjacent height | 96 |
| Largest table (`tno01`) | 192 |
| Duplicate input keys / count mismatches | 0 / 0 |

All 2,097 height fields across these rules were `0` (1,491) or `1` (606).
These are observations of SET encodings, not proof of their coordinate system
or a restriction on the area's absolute elevation. Ten sets have no primary
rows; eleven have only rows preserving adjacent values. Keep explicit identity
rows until their role in matching/no-match behavior is established.

| SET | All primary rows | Changing rows |
| --- | ---: | ---: |
| `tno01` | 192 | 116 |
| `tcn01` | 76 | 52 |
| `tic01` | 36 | 20 |
| `tin01`, `tni01` | 25 each | 12 each |
| `tss13` | 16 | 12 |
| `ttr01`, `tts01` | 28 each | 10 each |
| `tts02` | 33 | 10 |
| `tcm02` | 51 | 5 |
| `trm02` | 50 | 1 |
| `trs02` | 41 | 1 |

For example, `tin01` has `Livingroom@0 + Kitchen@0 -> Wall@0`, where the output
describes the changed neighbor. None of its ordinary, ungrouped tile rows
contains both Livingroom and Kitchen corners. The same pair-coexistence check
fails for all 12 changing rows in each of `tin01`/`tni01`, and all 20 in `tic01`.
This establishes a gap in direct tile matching; it is not an executed comparison
of brush results with Aurora. `ttr01` and `tno01` also supply height-changing
examples, so fixing only room walls is insufficient.

The format description documents the six fields and their neighbor-change
purpose: [NWN tileset authoring documentation](https://nwn.wiki/spaces/NWN1/pages/38175063/Tilesets).
It does not settle ordering, normalization, or propagation precisely enough to
implement those details from prose alone.

ASSUMPTION: most interactive edits affect a small neighborhood compared with the
whole area — affects scratch/worklist sizing and performance expectations;
actual gesture-size and tileset-use distributions remain unmeasured.

ASSUMPTION: rule catalogs remain stable for the lifetime of a loaded tileset —
affects borrowed spans; retain the existing resource-generation cancellation
boundary and verify registry refresh behavior instead of adding a second cache.

## Patterns & Conventions Found

References below describe the baseline revision; implementation will move lines.

- `lib/nw/formats/Tileset.hpp:31` keeps cold terrain/crosser topology parallel to
  tiles; `:53` owns the catalogs. Extend this data rather than the rendered tile.
- `lib/nw/kernel/TilesetRegistry.cpp:16` bounds SET counts at 65,536; `:18`
  resolves names case-insensitively; `:29` and `:55` load catalogs and topology.
  Reuse `Ini`, `named_type_index`, `Vector`, and existing logging conventions.
- `tools/client/area_tile_brush.cpp:24` owns contiguous corner/edge scratch;
  `:164` decodes the area and rejects inconsistent seams. `:471` onward marks
  incident cells. Reuse these indices, layouts, and dirty marking.
- `tools/client/area_tile_brush.cpp:264` matches terrain, height and crossers;
  `:324` selects ungrouped candidates across four rotations. Preserve this
  fitter, seeded variation, and canonical eraser policy.
- `tools/client/area_tile_brush.cpp:629`, `:1065`, and `:1168` implement erasing
  and complete-group replacement. Neighboring groups are protected boundaries.
  See [the eraser contract](closed/client-area-eraser-and-placement-output.md).
- `tools/client/area_tile_brush.cpp:1326` builds final rows; `:1695` and `:1890`
  are the batch brush entry points. `tools/client/area_tile_edits.hpp:15` stores
  complete before/after rows. `area_tile_edits.cpp:230` validates rows and doors
  before `:390` writes; `:426` integrates undo. Preserve these contracts.
- `tools/client/area_tile_editor_runtime.cpp:53` previews through the same
  builders that `:971` uses at commit; commit checks resource generation and
  mutation epoch. Keep rule application inside these shared builders.
- `tests/kernel_tilesets.cpp:11` exercises real SET loading;
  `tests/rollnw_client_area_tile_edits.cpp:983` onward covers fitting, height,
  erasing, groups, crossers, and persistence. Add cases to those suites.
- `.clang-format:1` selects WebKit style. Match nearby snake_case functions,
  flat value structs, RAII vectors, borrowed spans, anonymous-namespace helpers,
  include ordering, `ObjectEditApplyResult`, and allocation-error handling.

Reuse inventory: project code already supplies parsing, name resolution,
topology, fitting, dirty marking, group/door protection, and undo. Standard
vectors/spans suffice for rule rows and bounded pending work. No shared utility
or vendored dependency supplies the missing SET semantics; add localized
transforms in the existing files, with no new framework or dependency.

## Architecture Decision

Add one authored-rule stage between brush intent and final tile fitting. Load
and validate rules once with the tileset; transform a borrowed batch of topology
edit seeds into resolved candidate topology; then fit affected cells once and
publish the existing edit batch. Rule processing never writes live `Area` rows.

Preserve author order and all six fields. Start with a contiguous linear scan of
the applicable table: at most 192 rows in the audited stock data. This keeps
ordering visible and avoids a cache whose correctness depends on unsettled
duplicate/height semantics. A sparse lookup is a later measured optimization,
not a prerequisite. The 65,536-entry loader limit still requires bounded total
rule work for custom data.

Do not infer policy from terrain/model names. Names resolve to catalog indices
at load time; production application operates on indices and integer heights.
Examples such as Livingroom/Kitchen are test data only.

### Semantics gate: unresolved questions in this issue

Before implementing application, create small reproducible Aurora edits with
saved before/after ARE rows and record the exact SET resource/version and
gesture order. Test both stock cases and minimal synthetic SETs where needed.
Record the answers here with their fixtures; these are blocking questions for
claiming matching authoring behavior:

1. Which lattice relationships constitute `Placed` and `Adjacent`, including
   shared corners, edge/diagonal neighbors, rotations, and area borders?
2. Relative to which origin are the three heights interpreted? Repeat equivalent
   edits at several absolute elevations; do not compare world heights directly
   against the observed 0/1 fields without evidence.
3. Does a missing match leave topology unchanged or forbid the combination?
   Does an explicit identity row have different meaning? How do duplicate keys
   and row order behave? Stock data contains no duplicates to answer this.
4. Are changes single-pass or propagated? Can changed neighbors become sources?
   What wins when two sources disagree, and can a later edit affect an earlier
   selected target? Preserve ordered gesture input, not an assumed commutative
   set of paint targets.
5. Which behavior applies to terrain, raise/lower, eraser, and group placement?
   How do crossers interact with changed corner terrain and height?

If Aurora/reference outputs cannot be obtained, loading and fixture preparation
can proceed, but leave application/parity unverified. Do not replace the missing
evidence with an invented fixed-point solver. Any intentional deviation needed
to preserve this editor's group/door safety must be documented and tested.

## Component Design

### Loaded primary-rule data

- **Files:** `lib/nw/formats/Tileset.hpp`,
  `lib/nw/kernel/TilesetRegistry.cpp`.
- **Layout:** a flat `TilesetPrimaryRule` with six `int32_t` values: three
  catalog indices and their three encoded heights. A `Vector` owned by
  `Tileset` retains SET row order, making vector position the diagnostic rule
  index. Track malformed/unavailable rules separately from a valid empty table.
- **Transform:** `load_primary_rules` reads the count and numbered sections,
  resolves names once, validates every field, and publishes a complete table.
  This is a batch of rows; the existing registry remains the owner.
- **Lifetime:** immutable to the painter while that loaded tileset is current;
  no pointers in rule rows and no additional global cache.
- **Errors:** missing section/zero count uses the existing no-rule path.
  Malformed nonempty tables must not silently become empty or partially usable:
  log the resource/row reason, keep rendering available, and reject operations
  needing rule application with an actionable diagnostic. Reject negative or
  excessive counts, unknown names, missing fields, and integer parse overflow.
  Establish the supported encoded-height range in the semantics gate; 0/1 is
  observed, while legal custom ranges remain unverified. Reject unsupported
  encodings explicitly rather than clamping. Define duplicate-key handling from
  evidence; until supported, reject ambiguous tables rather than choosing a row
  accidentally.

### Candidate topology transformation

- **Files:** `tools/client/area_tile_brush.cpp` and its header contracts.
- **Interface shape:** internal plural `apply_primary_rules` accepts immutable
  rules, a span of ordered edit seeds, borrowed candidate `AreaTopology`,
  protected/fixed boundary data, and an affected-cell mask; returns success or
  a diagnostic. Keep the exact seed fields minimal and fix them after the
  semantics gate. They must identify lattice indices and any pre-edit values
  needed for correct matching; do not destroy those values before collecting
  the input batch.
- **Contracts:** area dimensions and seed indices validate before access;
  terrain values index the loaded catalog; candidate world heights remain
  checked `int64_t`, and final tile base heights must fit `int32_t`. Void/unset
  lattice entries are not valid terrain inputs and cannot be propagation
  targets unless explicitly materialized by the existing brush contract.
- **Ownership:** builders own candidate topology and scratch for one preview or
  commit. Rule application borrows them and produces no retained references.
  One clicked target is a batch of one; retain existing public entry points.
- **Work:** process the observed neighborhood and order. If the semantics gate
  proves cascading is required, use a bounded vector-backed FIFO of lattice
  indices with pending flags, not recursion or whole-area rescans. Requeue only
  when values actually change. An identity result produces no new work.
  Pending deduplication bounds storage, not termination: enforce a deterministic
  total work limit as well. Choose and document the limit from fixture/corpus
  measurements before shipping; cyclic or excessive propagation rejects the
  entire candidate with its rule/cell diagnostic, never a partial result.
- **Boundaries:** preserve incoming fixed group rows and unselected placed
  groups. Reuse footprint resolution rather than broadening selected groups
  recursively. A rule requiring an illegal boundary change rejects the edit.
  Do not silently clear neighboring crossers to make a tile fit; only make
  changes supported by the verified brush/rule contract. Check all height
  arithmetic and array-size calculations before updating scratch.
- **Output:** mark every tile incident to an actual corner/edge change. Run
  `fit_area_tile_edits` after rule work finishes. Newly affected neighbors keep
  compatible current tiles; preserve the existing explicit-target force and
  canonical flags instead of randomizing every propagated neighbor. A missing
  fit, invalid rule, boundary conflict, work limit, or allocation failure clears
  the whole public output.

### Builder integration and publication

- Terrain and height brushes seed the shared stage from their respective cell
  and corner batches. Eraser and group placement use the same semantics where
  their operations author terrain/heights, after resolving explicit replacement
  footprints and while preserving their existing fixed boundaries.
- Crosser-only, light-only, variation, and void operations do not acquire rule
  work merely because they share an entry point. Verify crosser interaction in
  the semantics gate before deciding whether it needs terrain-edit seeds.
- Preserve the distinction between explicit erased targets and neighboring
  tiles adjusted for a transition. Occupied hooks on neighboring changed tiles
  still go through normal validation; they do not implicitly authorize deleting
  doors or other objects.
- Keep `AreaTileEditBatch` and its before/after row protocol unchanged. Preview
  and commit must include the complete expanded footprint; undo/redo replay the
  stored rows without rerunning rules or randomness. Existing dirty/publication
  code consumes those rows. No new serialization or network protocol is needed.
- The eraser preview currently displays original rows of explicit erase targets
  (`area_tile_editor_runtime.cpp:114`), unlike the ordinary candidate preview.
  Show adjusted neighbors as well while retaining the explicit erase-target
  indication; do not assume the current preview already displays the full batch.

## Implementation Map

| Action | File | Work |
| --- | --- | --- |
| Modify | `lib/nw/formats/Tileset.hpp` | Flat rule rows, owned table, validity state |
| Modify | `lib/nw/kernel/TilesetRegistry.cpp` | Bounded all-or-nothing rule loader |
| Modify | `tools/client/area_tile_brush.cpp` | Seed collection, rule stage, integration and diagnostics |
| Modify | `tools/client/area_tile_brush.hpp` | Expanded-neighborhood and failure contracts |
| Modify | `tests/kernel_tilesets.cpp` | Real catalog and malformed-table tests |
| Modify | `tests/rollnw_client_area_tile_edits.cpp` | Rule, boundary, atomicity and round-trip tests |
| Modify | `tools/client/area_tile_editor_runtime.cpp` | Include changed neighbors in eraser previews while preserving target indication; reuse current commit builders |
| Modify as needed | `tests/rollnw_client_rml_smalls_language_binding.cpp` | Existing editor interaction harness: preview/commit agreement and stale cancellation |
| Create | `tests/test_data/user/development/rules_primary.set` | Minimal authored synthetic rule/topology fixture |
| Create | `tests/test_data/user/development/bad_rules.set` | Malformed-table fixture, analogous to `bad_doors.set` |
| Create | `issues/terrain-primary-rules-evidence/README.md` | Reproduction steps, resource hashes, verified semantic decisions and timing results |
| Create | `issues/terrain-primary-rules-evidence/cases.json` | Compact before/after tile rows, ordered gestures, SET identity and rule indices from reference runs |

Keep further malformed variants in existing temporary-resource test patterns
when practical. Do not copy shipped models, textures, or complete game archives
into fixtures. Use installed resources for stock integration tests and small
authored data for deterministic unit cases. Add no build target unless existing
test/fixture discovery is insufficient. Reinspect paths if implementation begins
after the baseline changes.

## Data Flow

```text
SET bytes -> validated catalogs and primary-rule rows (tileset lifetime)

ordered gesture + current Area/SET
    -> validate/decode original topology and group boundaries
    -> collect intent seeds and apply explicit brush changes in verified order
    -> apply primary rules to candidate topology; mark all affected cells
    -> fit affected cells using existing tile candidates and rotations
    -> complete owned before/after rows
    -> existing door/stale-state validation
    -> preview OR atomic commit -> undo/redo/publication/save

failure before commit -> empty candidate output + diagnostic; no live writes
```

SET/catalog data changes with resources; area topology changes with edits. Rule
processing reads neither render meshes/textures nor unrelated object fields.
Door validation reads occupancy only when final changed rows require it. Avoid
expanding the work to unrelated groups, full-area repair, or runtime navigation.

## Build Sequence

### Phase 1: establish the rule contract

- [ ] Reproduce the corpus audit against active resource precedence and record
  exact SET hashes, rows and counts. Include the user's relevant custom sets
  when available; do not extrapolate stock counts to HAK content.
- [ ] Capture before/after reference cases for `tin01`/`tic01` room boundaries,
  `ttr01` height changes, and `tno01` cascades/conflicts, plus an empty-rule set
  and `ttf01` identity rules. Resolve every semantics-gate question above.
- [ ] Record supported height encodings, no-match/duplicate policy, seed and
  processing order, operation coverage, and deterministic termination bounds.
  Add minimal failing regressions with exact expected topology.

### Phase 2: load and apply

- [ ] Add flat rows and loader validation; verify unchanged rendering when
  authoring rules are malformed and explicit brush rejection for that state.
- [ ] Implement the batch transformation and connect terrain/height builders.
  Reuse matching, masks, seed handling, and complete edit rows; do not introduce
  a second fitter or per-terrain special cases.
- [ ] Integrate verified eraser/group behavior and any demonstrated crosser
  interaction. Keep protected groups, voids, and occupied hooks explicit.
- [ ] Ensure the preview represents every changed neighbor and commit uses the
  identical deterministic transformation for the same inputs and seed.

### Phase 3: verify and close

- [ ] Test all four rotations, borders, multiple elevations, ordered batches,
  conflicts/cycles, invalid data, groups/doors/voids, and full row preservation.
- [ ] Verify one undo action includes all propagated edits; undo/redo and
  save/reload preserve complete tile rows. Redo must not rerun selection.
- [ ] Run focused suites, existing painter regressions, and measured preview
  workloads. Record results and remaining differences from the reference.
- [ ] Repeat the simplification/self-check below and mark this issue complete
  only when its done criteria are met.

## Critical Details

### Cost and performance verification

Let `N` be area cells, `C=(width+1)*(height+1)` corner entries, `R` rule rows,
`T` tile definitions, `A` affected cells, and `W` processed rule-neighbor events.
Existing decode and scratch initialization already cost O(N); fitting scans
up to four rotations of T definitions per affected cell, sometimes twice for
random selection: O(A*T). The proposed linear rule scan adds O(W*R). Rule
changes can increase A; do not attribute that cost solely to lookup overhead.

Six 32-bit fields require 24 bytes per rule row, excluding vector/state overhead:
4,608 bytes for the largest observed 192-row table. Verify `sizeof` in the
implementation. Rows are loaded once; no per-frame parsing/name matching.
Scratch is O(N), including masks and any bounded pending-corner FIFO; retained
undo grows with the final changed-row count. Measure actual allocation sizes.

Access is linear over rule rows, topology decode, and final rows; neighborhood
updates index nearby lattice positions. Matching branches depend on the data;
their predictability is unmeasured. Skip the stage for valid empty tables once
per builder call. Do not add dense terrain/height cross-products or a new tile
cache without measured lookup cost. No pointer-heavy hot loop is added; the
existing Area handle is resolved once at the public boundary, and borrowed
Area/SET references preserve their established ownership.

Latency and memory effects are **unverified**. Hypothesis: rule resolution adds
work but enables currently rejected edits; avoiding repeated fitting during
propagation limits unnecessary additional work. No speedup is claimed. Measure
before/after on the same Release build configuration and machine: successful
no-rule/identity strokes, room joins, height transitions, and the largest
observed rule table, across single clicks, drags, and near-full-area batches.
Record N/R/T/A/W, build-stage wall time (median and p95), allocation/peak scratch
bytes, and preview rebuild frequency. Compare previously successful workloads
separately from newly enabled ones. Record the machine and agree a regression
budget before accepting performance changes; do not invent one from this audit.

### Correctness and failure tests

- Parse case-insensitive names, valid empty/identity/changing tables, malformed
  counts/rows/heights, unknown terrain, duplicates, and supported range edges.
- Use exact reference topology for asymmetric rules, normalized height shifts,
  competing sources, and ordered multi-target gestures. Repeating the same
  inputs/seed must produce the same rows and diagnostics. Do not assume reversing
  gesture order must produce the same result unless the reference proves it.
- Test missing matches separately from missing tile candidates. Cyclic/excessive
  rule work, arithmetic overflow, and any fitting failure must leave live tiles,
  doors, undo history, and dirty state unchanged.
- Exercise unselected groups, replaced groups, fixed incoming group cells,
  crossing paths, occupied door hooks, invalid existing seams, and void borders.
- Compare complete before/after `AreaTile` rows, including lights and animation
  fields. Preserve the existing resource/mutation cancellation checks.
- Build the configured `rollnw_test` target and run `KernelTilesets.*` and
  `ClientAreaTileEdits.*` from the build's `tests` directory. For a client-enabled
  build, also run the relevant existing `ClientRmlTemplates.*` tile-editor cases.
  Use the existing CMake fixture preparation for temporary test data. Confirm
  the suites are compiled in that configuration before claiming coverage.

### Simplification pass

- **Omit work:** empty tables take the existing path. Pure lighting/variation
  operations do not run terrain rules. Do not repair unrelated map seams.
- **Do once:** parse/validate/resolve rule names at tileset load; decode one
  candidate topology per builder; fit only after rule application finishes.
- **Do fewer times:** enqueue actual changes only, deduplicate pending indices,
  and reuse incident-cell masks. Keep identity rows for semantics but do not
  turn an identity output into additional propagation.
- **Approximate:** no; exact authored topology and undo are the requirements.
- **Small/large lookup:** a contiguous rule vector is enough to start. Reject
  dense cross-products and deferred cache invalidation work without profiling.
- **Buffer:** a bounded FIFO is justified only if measured reference behavior
  requires cascades; it is scratch within one builder, not a producer thread.
- **Constrain:** four rotations, existing catalogs, explicit group boundaries,
  supported height encoding, and existing atomic publication remove the need
  for a generic constraint solver, new persistent state, or name-based policies.

### Done criteria and final self-check

Done means verified rule semantics, correct stock and synthetic outputs,
deterministic bounded rejection, complete preview/undo/save behavior, and
recorded latency/memory evidence. A reference result requiring different
adjacency, height normalization, or ordering disproves that part of this design
and must change the contract before implementation is accepted. A valid
reference edit exceeding the chosen work bound disproves that bound.

Planning self-check: framing, real inputs, measured distributions, platform,
ownership, costs, common cases, ranges/errors, batch interfaces, and assumptions
are stated. Reuse/simplification removed a new solver, cache, protocol, and
renderer changes. Open semantic questions are filed here, not hidden in code
TODOs. Source paths and the stock audit were checked; no painter behavior,
Aurora comparison, performance result, custom-resource coverage, or Windows
behavior has been verified by this planning change. Implementation checkboxes
remain open intentionally.
