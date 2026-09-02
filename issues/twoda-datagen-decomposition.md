# Native Catalog and 2DA Decomposition Review Gate

- **Status**: first tranche implemented; further table migration requires a new decision
- **Tier**: 2 — imported-data and native/editor boundary
- **Owner**: NWN1 profile, SmallS runtime, `smalls-datagen`, toolset/LSP

## Real Data And Platform

The platform is one native C++ process, one selected NWN1 profile, one SmallS
runtime, and a resolved resource set whose base-game/module/hak precedence is
fixed for that process lifetime.

Observed inputs and outputs for this tranche:

- `appearance.2da` has 15,100 source rows in the generation fixture; 838 rows
  have a usable label and produce local review snapshots.
- Appearance identity is the non-negative source row index. Sparse and invalid
  rows do not cause ID compaction.
- One representative humanoid head selector contains 74 resolved options.
- The old body-part selector constructed and probed candidate option IDs
  `1..254` each time the selector was opened.
- The checked-in generated NWN corpus remains 2,111 files / approximately
  8.3 MB. Its legal and repository-retention decision is tracked separately in
  [`audit-checked-in-nwn-derived-data.md`](audit-checked-in-nwn-derived-data.md).

The common runtime path is many direct indexed reads after one startup import.
The startup transforms are linear over the resolved source rows/resources.

## Implemented Contracts

### Body-Part Catalog

Input:

- one resolved model-resource batch;
- NWN1 appearance, gender, phenotype, and fallback phenotype assembly inputs.

Transform:

- the NWN1 adapter decodes model resource names once and groups them into
  contiguous set, part, and option arrays;
- the generic catalog validates offsets, counts, duplicate keys, mirrors, and
  unknown-part options;
- editor projection copies stable `{part_id, option_id}` selections from
  bounded spans.

Output and ownership:

- C++ owns immutable catalog arrays for the selected profile lifetime;
- SmallS owns persisted body-part fields, sentinel interpretation, legality,
  defaults, mutation, and undo/redo policy;
- unknown assembly/part lookups return empty spans and invalid edits reject the
  complete edit.

Generic code contains no NWN1 slot count, `0`/`255` policy, or model-name
grammar. Popup opening performs no model-name construction or resource lookup.

### Private Structured Data Spec

The package-owned spec is
`lib/nw/smalls/scripts/nwn1/data_specs/appearance.json`. It is installed as an
ordinary package resource and parsed by runtime, datagen, and the LSP. It is
not embedded in C++.

The private format intentionally has:

- no `$schema`, `format_version`, compatibility negotiation, or plugin system;
- three implemented value operations: `row_index`, `column`, and `enum`;
- scalar types `int`, `float`, `bool`, `string`, and `ResRef`;
- native and SmallS field groups; and
- row `reject`, typed `default`, and `omit_row` policies.

The appearance spec requires 16 field transforms: two `row_index`, eleven
direct `column`, and three `enum` transforms. No constant, scan, join, indirect
table, grid, dynamic array, or dependency graph was required.

Batch protocol:

- input is one valid `StaticTwoDA` plus one parsed `DataSpec`;
- output is one source-sized indexed batch and flat contiguous materialized
  value storage;
- row-local parse/enum/range failures emit a diagnostic, discard that row's
  values, and leave an indexed hole with `[[index]] == -1`;
- intentionally omitted rows use the same hole representation without an error;
- a missing source or required column is structural, returns a cached typed
  empty array, and does not abort the calling script, profile, module, or client;
- publication occurs only after the candidate batch has been fully traversed.

There is no ID remapping and no cross-domain rollback/reload mechanism.

### Appearance And Movement Ownership

`AppearanceInfo` is the canonical native fact row for source identity, label,
string reference, model/base names, model type and attachment flags, scales,
arm availability, and personal space.

`AppearanceRules` retains only SmallS gameplay inputs: size category and base
movement category. The nine numeric base movement rates are a small SmallS
lookup in `nwn1.creature_speeds`; they are not loaded through `load_config!` and
are not duplicated in C++.

SmallS resolves the creature's base movement rate from object/profile state and
pushes that resolved float into native spatial state on object initialization
or relevant change. Native preview locomotion reads the pushed spatial value.
Haste, slow, overrides, caps, CR interpretation, and other effective movement
policy remain SmallS responsibilities.

`HASARMS` accepts `0` as false and `1` as true. Other integers are diagnosed and
coerced to true. This preserves the observed worm rows authored with value `8`
without treating the source defect as a startup failure.

### Snapshot And Corpus Contract

Runtime authority for `nwn1.data.appearance` is the active resolved
`appearance.2da` plus the package spec. Appearance snapshots are local review
artifacts under an ignored build directory and are not committed.

Snapshot filenames use the declared label column, lower-case ASCII, and spaces
mapped to underscores. The source row ID remains inside the row and is not part
of the filename. Empty or duplicate sanitized names reject snapshot generation.

`smalls-datagen --check` generates into a temporary directory, validates the
result, compares it to the selected local snapshot, reports additions/removals/
changes, and removes the temporary tree. No backup or custom transaction layer
exists.

Production config paths are dotted. Slash paths remain accepted only at the
external runtime compatibility boundary.

## Verification And Measured Cost

- Body-part popup benchmark, five isolated repetitions: median 76.653 us wall /
  76.428 us CPU, 74 options, zero `resource_exists` calls. Pre-change latency
  was not captured, so no latency-improvement claim is made. The eliminated old
  work is the directly observed 254-candidate probe loop.
- Retained appearance VM bytes: 1,301,487 bytes for the test resource set.
- Local generation: 838 emitted, 14,262 filtered, 15,100 source rows;
  immediate `--check` result `snapshot changes=0`.
- Real CEP module: four `HASARMS=8` warnings at rows 1733 and 3507–3509,
  successful module initialization, and successful `c_worm` resource dump.
- Focused row/publication tests: 14 passed.
- Full isolated suite: 1,898 discovered; 1,871 passed; 27 graphics/headless
  skips; zero failures. One earlier full attempt encountered a non-reproducible
  `nwn1.propsets` startup failure after 1,000+ passing tests; the failing test,
  its predecessor sequence, the full `SmallsRuntime.*` suite, and the complete
  isolated rerun all passed.

Measured startup-time and retained-memory costs are paid once per selected
profile. The body catalog retains contiguous set/part/option arrays. Appearance
retains one VM definition batch and one native fact copy. No general startup or
runtime performance improvement is claimed.

## Reuse Assessment And Next Candidate

The following pieces were shared without separate runtime/datagen
implementations:

- package spec parsing and diagnostics;
- `StaticTwoDA` row access;
- the flat batch materializer;
- runtime and datagen sinks over the same materialized batch;
- canonical config-path binding and appearance provenance in the LSP; and
- native indexed publication and lookup conventions.

`baseitems.2da` is the next mixed-table candidate, but it is not migrated in
this tranche. Its current generated definition has 32 fields. Thirty-one are
expressible with the existing row/column/default scalar machinery. The
remaining `requirements` field is an observed five-column feat-requirement
array and would require exactly one new aggregate transform plus typed
contiguous aggregate storage and a VM writer. Before that work, ownership of
property column, equip mask, stack size, and container flag must be audited from
their real readers.

Classes, feats, spells, progression grids, placeable rules, races, and other
tables remain outside this gate.

## Post-Gate Door Ownership Decision

The later ownership review made a separate, explicit decision for
`doortypes.2da` and `genericdoors.2da`: these are native resource/model facts,
not retained SmallS configuration. The NWN1 profile now loads them directly
into sparse native `Rules` arrays. `nwn1.doors` reads the native facts, retains
the two-selector interpretation and mutation policy, and pushes resolved visual
rows into C++.

This did not expand the shared data-spec transformer and did not generate or
commit derived Door row files. A missing optional Door table logs and publishes
an empty domain; a malformed row remains an invalid sparse slot while valid
rows remain available.

## Decision Required Before Expansion

Do not migrate another table until deciding:

1. whether 1.30 MB retained VM storage warrants a one-shot/unrooted config load;
2. whether the base-item ownership audit and one aggregate operation are worth
   the added implementation and maintenance cost; and
3. whether any derived NWN row corpus may legally and practically remain in the
   repository.

Current evidence does not justify committing new appearance snapshots. Runtime,
tests, local reproducibility, and LSP binding work without them. Existing
unmigrated corpus files remain unchanged pending the separate legal audit.
