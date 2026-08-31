# Smalls-First RmlUi Managed Lists

Status: body-part migration implemented and headlessly verified; interactive
Release timing trace pending.

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
- one ordered batch of 19 editor rows from Smalls (the robe slot is not part of
  this editor);
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

The common access pattern is a linear projection and native copy of the 19-row
source when the active creature refreshes, followed by a viewport-bounded DOM
window. Sparse option discovery and projection occur only when the selector is
opened or its source is invalidated. Per-frame synchronization reads only the
native list revision and materialized rows; it does not call the Smalls policy
per row.

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
is linear in the current source: one typed Smalls policy batch, one projected
`ListItem` batch during replacement, one native copied source batch, and one
viewport-sized RmlUi window.

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

## 2026-08-18 Creature Workbench Slice

The repository now contains a generic `RmlSmallsDataModel`; the earlier concern
about adding one speculatively no longer describes the available machinery. The
Creature Sheet reuses that existing boundary for a concrete, bounded,
read-only batch:

- input is the one published live object plus its Creature propsets and the
  `core`/`nwn1` policy functions used to derive combat values;
- `toolset.ui.refresh_creature_sheet_model` replaces one SmallS-owned global
  presentation batch rather than exposing runtime pointers or incrementally
  patching rows;
- `toolset.rmlui.refresh_creature_sheet_model` is the one-event adapter; it
  extracts `active_object` and calls the reusable object-to-batch transform;
- each row is a flat `{label, value, section, alternate}` struct, owned by the
  SmallS runtime until the next refresh or runtime replacement;
- output is zero to 128 rows plus an explicit error string; invalid or
  non-Creature handles produce the empty state, and overflow rejects the whole
  batch with a visible error; and
- RML owns the workbench, tab, surface, header, and Sheet row structure through
  a template and `data-for`. C++ only dirties the existing data model after a
  selection or mutation refresh.

The dedicated-server fixture has 28 skills and produces 48 presentation rows:
five section rows and 43 values. The access pattern is one linear rebuild and
one RmlUi expansion on selection or object mutation; it is not a per-frame or
per-row SmallS call. No performance claim is made. The 128-row bound makes a
viewport virtualizer unnecessary for this observed Sheet data; views with
unbounded or user-sized sources still use the managed-list path.

The simplification pass removed the live C++ Sheet snapshot and its bespoke
markup renderer. It reused the existing RmlUi data model and kept a single
complete-batch replacement state. It did not add a widget registry, universal
schema, or a second UI state store.

### Remaining Migration

The Creature workbench still has one transitional C++ hydrator for tab
activation and for the existing Classes, Appearance, Spells, and Inventory
surfaces whose row production has not yet moved to RML/SmallS. Migrate those
surfaces independently only when their actual input batches and transaction
contracts are preserved. The acceptance condition is that changing or
reordering the static workbench layout requires only RML/RCSS changes; C++ may
retain generic hosting, bounded-list synchronization, popup geometry, and
transaction application, but no surface-specific markup or routing.

## 2026-08-30 Body-Part Migration

The body-part editor now follows the decided protocol:

- `toolset.creature_editor` reads the one published live Creature, retains the
  typed 19-row policy batch, and projects complete replacement batches with
  decimal scalar keys into the existing managed-list host;
- the static RML template owns the main list, option popup, close action, and
  anchor/bounds declarations; RCSS owns their presentation;
- generic native synchronization materializes fixed-height list windows and
  positions active managed popups from the selected row or selected cell;
- accepted sparse option keys call the existing
  `object.creature.set_body_part` command, so the existing object-edit batch
  remains the only mutation, dirty-state, preview, undo, and redo boundary;
- missing/stale objects clear both lists, mismatched keys leave the live object
  and undo history unchanged, and the command rejects parts outside the live
  batch and values outside `[0, 255]`; and
- the former C++ body-part row structs, field decoders, markup builder, popup
  state, and pointer/keyboard/wheel event branches have been deleted.

The batch boundary owns no runtime pointers. Smalls arrays live until the next
refresh, the native host owns copied strings until replacement/reset, and RmlUi
owns its DOM elements. The per-frame popup placement pass uses non-owning
`Rml::Element` pointers because synchronous DOM traversal is the only RmlUi
access protocol; those pointers do not escape the update.

The simplification pass reused `VirtualListHost`, the existing combobox
placement calculation, the named command, and the shared transaction history.
It removed the redundant native presentation model and removed an unnecessary
19-row rebuild from popup close. It did not add a widget registry, another
virtualizer, a second mutation path, or an extension API for other editors.

Headless evidence covers the real 19-row fixture; sparse head values including
`0`, `1`, and `119`; a different part whose sparse source includes `255`;
forged-key rejection; pointer-path activation; close/reopen; stale-object
clearing; mutation; undo/redo; generic keyboard cycle/activation; popup
placement; selected-value reveal without trapping later user scrolling; static
RML ownership; and rendered text line boxes for both body-part cells. The
declarative activation contract also verifies that selecting a model returns
focus to the stable body-part list and that Up/Down routes to its retained,
hidden option batch. A 4,096-row synthetic source with a 10-row viewport and
overscan 4 emits exactly 18 DOM rows. The full configured test suite is the
completion gate; all 9 configured CTest targets passed on 2026-08-30.

An interactive follow-up exposed two production-path gaps that the direct
policy test did not cover. The body-part refresh element had no stable RML ID,
so the mutation-safe refresh traversal skipped it; the template now names that
target and the structure test requires the ID. Entering Appearance also
performs a visual-only creature rebuild. Object-file previews were incorrectly
replacing their preview-local root transform with an area-creature placement;
the refresh now retains the existing parent placement. Regression tests cover
both a single-model creature and the dynamic human assembly, while area scenes
continue to read their authored spatial state. The same follow-up found that
RmlUi does not form text line boxes for raw text in the flex-styled body cells;
those cells now use the established block text layout. Model activation retains
the selected part and sparse option batch while hiding the popup, and flat RML
attributes declare focus return and keyboard-cycle routing without restoring a
body-part-specific native event branch.

The first declarative focus pass targeted the stable list container before the
activation callback and mouse-up completed. That did not preserve combobox
focus because each commit replaces the selected row. The body-part declaration
now targets the stable list container while CSS projects its focus state onto
the selected Model cell. The managed-list boundary captures that value-only
element request, completes callback refresh and mouse-up, then resolves the
stable container. Keyboard cycling repeats the same post-refresh resolution.
Creature mutation processing performs a second workspace refresh after that
immediate callback, so it captures the same request before the refresh and
resolves the final container afterward.
No DOM pointer or additional focus state survives an update. Viewer camera keys
also require the viewport's existing focus bit; a missing UI node can no longer
silently transfer Up/Down from model cycling to camera pitch.
The same focused managed-list path now consumes unmodified vertical wheel
steps, matching the former combobox behavior without restoring its native
creature branch. Keyboard and wheel cycling require RmlUi focus ownership and
reject input while the viewport owns focus. The Model cell's gold styling now
comes only from hover or focus on the stable list container; selected body-part
state is still retained for option routing and popup anchoring but no longer
impersonates keyboard focus after a viewport click.

Base-appearance mutations originally bypassed that visual-only path and
rebuilt the entire viewer object (or area), which reset the preview facing.
Creature base-appearance changes now replace only that creature's visual rows,
using the same camera- and root-placement-preserving path as body-part and
color edits. Non-creature base appearances retain their existing full rebuild
contract. The regression changes a live dynamic Human to a single-model Bodak
from an off-axis camera and requires the scene identity, camera matrix, and
root transform to remain unchanged while the model rows change. Appearance,
Wings, and Tail fields also use a CSS border triangle instead of a font glyph,
removing the missing-character squares seen with the bundled font.
Accessory headings and catalog fields are explicit full-width block rows, so
the Accessories and Wings labels cannot share the same inline line box.

No latency or throughput result is claimed. A Release trace of open, scroll,
and selection on the actual client remains unverified; collect it before using
performance as evidence for or against migrating another managed view.
