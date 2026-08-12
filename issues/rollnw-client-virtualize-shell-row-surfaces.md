# rollnw | client Shell Row Virtualization

## Problem

rollnw client must keep RmlUi document size proportional to the visible viewport,
not to project, module, command-registry, or log size. The object Properties and
Feats surfaces and the project tree already virtualize their visible rows, but
several older shell surfaces still rebuild all matching rows.

## Real Platform and Data

The platform is one rollnw client UI thread driving RmlUi. Scroll events, filter
changes, command registration, module loads, and captured output invalidate row
presentation on that same thread.

The observed surfaces are:

| Surface | Source | Current bound | Current presentation |
| --- | --- | --- | --- |
| Project tree | flattened project resource tree | project-dependent | viewport plus 8 overscan rows |
| Object Properties | live Smalls property snapshot | 16,384 rows, depth 32 | viewport plus 8 overscan rows |
| Creature Feats | rules feats merged with live feat IDs | virtual-list integer range | viewport plus 8 overscan rows |
| Recent projects | preferences | hard cap of 12 | all 12; intentional bounded exception |
| Module areas | loaded module areas | module-dependent | all matching rows |
| Command palette | registered command specs | registry-dependent | all matching rows |
| Output | captured shell lines | hard cap of 400, pruned in batches of 100 | all filtered rows |
| Terminal | terminal history lines | hard cap of 400, pruned in batches of 100 | all rows |
| Resource inspector | diagnostic and summary property vectors | resource-dependent | all rows inside the document |

The common transform is a contiguous row source plus filter/selection state and
scroll geometry to a bounded RmlUi row batch. Output and terminal lines currently
wrap, so their effective row height is variable.

ASSUMPTION: normal command and area result distributions have not been measured
yet - this affects overscan choice, not whether their DOM must be bounded.

ASSUMPTION: preserving wrapped log lines is required - this determines whether
logs need measured-height virtualization or an explicitly constrained one-line
presentation.

Invalid row counts, negative geometry, and stale selections are clamped to an
empty or valid range. A source that cannot fit the chosen row-index protocol is
rejected with a visible diagnostic; it is never partially indexed through
overflowed arithmetic.

## Decision

Use one flat row-window protocol for every data-dependent fixed-height shell
list:

```text
filter_rows(source rows, query/state) -> contiguous row indices or snapshot
compute_visible_range(row_count, scroll geometry, overscan) -> [start, end)
render_rows(snapshot, [start, end)) -> bounded RmlUi markup
```

The row snapshot owns strings and stable keys until the next invalidation. DOM
rows carry stable keys, not pointers. Selection is stored as a source index or
stable key and remapped after filtering. The active list is a batch even when it
contains one row.

Fixed-height surfaces use `VirtualListController`. Output and terminal require a
separate decision based on measured line-height data: either constrain each
entry to one fixed-height row with an expandable detail view, or add cumulative
measured heights and binary-search the visible range. Do not force variable
height through fixed-row arithmetic.

Recent projects remain direct because their source is permanently capped at 12.
Workspace tabs and other small navigation chrome remain direct until observed
data invalidates their explicit small-count constraint.

## Cost

For fixed-height lists, range calculation is constant work and rendering is
linear in visible rows plus overscan. Source filtering remains linear in source
rows on filter changes. Memory is one contiguous filtered index/snapshot per
surface plus a bounded markup buffer.

No shell-list timing has been measured. Performance impact is unverified; the
required verification is a Release benchmark or trace comparing filter and
scroll invalidations at representative and maximum observed row counts, plus a
DOM-row counter proving the viewport bound.

Variable-height logs add either a product constraint and detail interaction or
a cumulative-height array, measurement updates, and binary search. That cost is
not justified until real wrapped-line heights are captured.

## Build Sequence

1. Keep the common `VirtualListController` contract covered by a large-source
   test that proves emitted row count is independent of total source count.
2. Replace the project tree's private range calculation with the common
   controller so one fixed-row contract remains.
3. Virtualize command results and module areas with stable keys and preserved
   keyboard/mouse selection.
4. Capture output/terminal line-height distributions and choose constrained-row
   or measured-height virtualization from that data.
5. Virtualize the chosen log presentation while preserving tail-follow only
   when the user was already at the bottom.
6. Measure resource-inspector row counts; virtualize its repeated sections if
   they are not given a small explicit source bound.

## Simplification Pass

- One fixed-row controller removes per-surface range arithmetic.
- Recent projects do no extra work because the existing 12-entry cap is the
  stronger constraint.
- Log virtualization is not guessed before collecting variable-height data.
- No general widget model, background worker, or second copy of application
  data is introduced.

## Done Criteria

- Every data-dependent fixed-height shell list emits no more than visible rows
  plus bounded overscan.
- DOM row count remains constant when a test source grows from 1,000 to 100,000
  rows at the same viewport size.
- Filtering, selection, activation, keyboard navigation, and scroll position
  behave identically before and after migration.
- Output and terminal preserve wrapped content and tail-follow semantics using a
  virtualization strategy supported by measured row-height data.
- Fixed bounded exceptions state their maximum source count next to the owning
  data policy.

Evidence against the common fixed-row protocol would be a required shell row
whose height cannot be constrained without losing necessary information. That
surface uses the measured-height path; it does not weaken the DOM bound.
