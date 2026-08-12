# Smalls-First RmlUi Managed Lists

Status: open.

## Problem

The rollnw client can call Smalls from RmlUi handlers, but managed object views
still cross an unnecessary C++ presentation boundary. The creature body-part
editor is the first concrete example:

- `nwn1.creature` owns the live body-part values, valid sparse model values,
  labels, display text, and mutation policy;
- `tools/client/object_edits.cpp` decodes those typed Smalls rows by struct field
  offset into C++ mirror structs;
- `tools/client/main.cpp` converts the mirrors into RML markup, retains popup
  state, and translates input events back into a toolset command; and
- `VirtualComboBox` owns the bounded visible window and popup geometry.

The first and last responsibilities are valid boundaries. The two middle steps
duplicate a managed-view protocol in C++ and make every new scripted editor pay
for another C++ row type, decoder, markup function, and event branch.

This issue does not move object policy, undo, or RmlUi internals into the wrong
layer. It removes C++ knowledge of individual managed views.

## Real Platform and Data

The platform is one rollnw client UI thread, one RmlUi document, and one Smalls
runtime operating on a published live `active_object`. RmlUi element mutation
and object transactions both occur on that thread.

The observed body-part inputs are:

- one live creature handle;
- one ordered batch of 20 editor rows from Smalls;
- one sparse option batch containing integer keys and display text; and
- fixed-height popup geometry showing at most ten rows plus bounded overscan.

The required outputs are:

- bounded RmlUi rows for the current viewport;
- stable selection by integer key;
- open, close, keyboard, pointer, and scroll events delivered to Smalls; and
- a named Smalls mutation committed through the existing client transaction
  boundary so dirty state, undo/redo, preview refresh, and selection refresh
  remain coherent.

Invalid active handles and stale selections produce an empty or closed list.
Out-of-range row windows are clamped by the generic virtual-list primitive.
Invalid mutation values are rejected by the named Smalls operation; the UI does
not silently clamp object data.

The common access pattern is a linear copy of the visible fixed-height row
window. Filtering or option discovery occurs only when the source is
invalidated or the combobox is opened. No per-frame full-source projection is
required.

## Decision

Add one generic Smalls/RmlUi managed-list binding, then migrate the creature
body-part editor through it before applying it elsewhere.

```text
Smalls source(active_object, view state)
    -> typed row batch with stable scalar keys
    -> generic bounded list binding
    -> RmlUi visible rows

RmlUi list event(stable key, event data)
    -> Smalls handler(active_object, stable key, event data)
    -> existing transaction command/result
    -> generic invalidation and refresh
```

Smalls owns row order, labels, formatted values, option discovery, selected
value, and view-specific event behavior. RML owns the view's static structure
and styling. C++ owns only the generic host operations that scripts cannot
perform directly: RmlUi element access, fixed-height window calculation, popup
placement, event delivery, and transaction result publication.

The binding protocol must use flat batch data with stable scalar keys. It must
not require a C++ mirror struct for each Smalls row type, retain pointers into
the Smalls arena, inspect field names to infer behavior, or generate a universal
property-editor schema.

The body-part editor is the only required migration for this issue. Appearance,
feats, and other managed views are follow-on users only after this path removes
their measured duplication.

## Cost

Expected implementation cost is medium. The dominant work is defining row
ownership/lifetime, exposing bounded list updates through the existing RmlUi
language binding, and preserving the transaction/undo contract. Runtime memory
is one owned source batch plus one viewport-sized RmlUi batch for the open view.

No performance result is claimed. Verification requires counters proving that
DOM row count is bounded by viewport plus overscan and a Release trace or
benchmark of open, scroll, and selection over representative and maximum
observed option counts.

## Build Sequence

1. Define the flat managed-list row and event contracts, including ownership,
   lifetime, stable-key rules, and explicit invalid/stale behavior.
2. Expose the existing fixed-height `VirtualListController` and popup placement
   through the Smalls RmlUi binding without exposing RmlUi pointers to scripts.
3. Express the creature body-part rows, combobox state, and event handlers in
   RML/Smalls against `active_object`.
4. Route accepted mutations through the existing transaction boundary and
   verify dirty state, undo/redo, preview rebuild, and selected-value refresh.
5. Delete the body-part C++ row mirrors, field-offset decoders, bespoke markup,
   retained popup state, and body-part event branches.
6. Measure source and DOM row counts plus open/scroll/select costs before
   considering another managed-view migration.

## Simplification Pass

- Keep `VirtualListController` and popup geometry as the one native primitive;
  do not build a second scripted virtualization implementation.
- Bind a flat row batch and flat events; do not add a general data-model or
  widget registry before the body-part editor proves the contract.
- Reuse the existing command transaction result; do not create widget-local
  undo state.
- Rebuild only on source invalidation and render only the visible window.
- Do not migrate unrelated property-tree reflection or variable-height lists in
  this issue.

## Done Criteria

- Creature body-part IDs, order, labels, option semantics, markup generation,
  and input behavior contain no managed-view-specific C++ code.
- The body-part RML/Smalls view reads and mutates the same live `active_object`
  used by preview, terminal, and command palette.
- A stale or missing object closes the popup and emits no mutation.
- Invalid part/value pairs fail explicitly in Smalls and do not enter undo
  history.
- Opening, pointer selection, keyboard selection, Escape cancellation,
  scrolling, undo, and redo are covered headlessly.
- A large synthetic source proves that emitted DOM rows remain bounded by the
  viewport plus overscan.
- The interim C++ body-part row mirrors and decoders are deleted.

Evidence against this design would be a required RmlUi operation that cannot be
expressed as a bounded flat-row update or flat event without retaining runtime
pointers or duplicating object state. Record that operation and its measured
frequency before extending the native protocol.
