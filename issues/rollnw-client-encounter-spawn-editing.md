# Client Encounter spawn field editing and reordering

Status: open. Extracted 2026-09-26 from the completed
[object-workbench milestone](closed/rollnw-client-object-workbench-and-property-surfaces.md).

## Existing data and behavior

The Spawns view projects the ordered `EncounterState.creatures` array. Creature
blueprint drops append records; the minus button removes the selected record.
Both use exact, undoable full-array replacement with a maximum of 1,024 entries.
Selection uses an indexed key tied to kernel generation, active object identity,
and mutation epoch, so stale context rejects before mutation. Duplicate entries
and surviving row order are preserved.

The recorded imported sample contains 466 Encounter blueprints: 387 have one
spawn row and the observed maximum is six. The managed-list host renders only
the viewport plus six overscan rows. Standalone previews consume the same array
but display at most 16 valid Creature blueprints; missing resources and preview
truncation produce diagnostics. These are existing bounds, not a reason to
truncate the stored spawn array during editing.

## Remaining work

- Inspect persisted spawn-record fields, representative values, and current
  profile validation. Define which fields the author edits and the accepted
  ranges; do not infer those requirements from the row labels alone.
- Add field editing through the existing array replacement and transaction path.
- Add explicit spawn reordering with defined insertion and no-op behavior,
  preserving all fields and duplicate rows.
- Retain the shared managed-list selection feedback, stale-context rejection,
  and ordinary preview/document refresh.

The current commands already pay O(n) copy/validation and retain before/after
arrays for undo. Measure any additional cost introduced by the chosen editor;
no timing or speedup is claimed here. This issue does not need a new collection
framework, serializer, preview ownership model, or unbounded row materialization.

## Completion criteria

- Record field contracts and explicit rejection of invalid values, stale
  selections, unsupported objects, and oversized arrays before mutation.
- Verify field changes and first/middle/last row moves against real Encounter
  fixtures, including duplicate entries, empty/single-row arrays, no-op moves,
  and stale selections after mutations or object changes.
- Preserve every unrelated field and exact array order through undo/redo;
  successful changes publish one transaction and dirty-state update.
- Verify UI dispatch, selected-row feedback, and bounded row materialization;
  keep the bounded preview consistent with the edited source array.
- Save/reload standalone blueprints and containing CAF Area documents and
  verify exact persisted fields and order. Record desktop verification limits.
