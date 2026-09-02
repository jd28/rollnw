# rollnw | client Object Workbench and Property Surfaces

Status: object-workbench vertical slices implemented and verified through
2026-08-19; Item appearance ownership updated 2026-09-01.

## Problem

rollnw client needs to present and eventually edit live instantiated objects through
Smalls. A reflected property tree is useful, but it is not a complete object
editor. Treating every propset field as directly editable would expose storage
instead of the authoring operations that preserve game rules and object
invariants.

Established NWN editor workflows provide useful product constraints:

- Creature identity and preview remain visible while task tabs change.
- Scalar object state and scripts are consolidated into expandable property
  groups instead of being spread across many `nwtoolset.exe` tabs.
- Feats, spells, special abilities, classes, inventory, appearance, item
  properties, encounter membership, variables, and localized text have
  dedicated task views.
- Large rules tables use filtering, assignment state, and compact tabular
  controls instead of large option menus.
- Encounter scalar properties and encounter members are visible together.
- Item properties use an available/applied layout with parameter controls
  derived from the selected property definition.

The workflows and screen density are evidence. Widget-specific object models,
direct member mutation, per-widget undo commands, and hand-built property
objects are not an architecture to reproduce.

## Real Platform and Data

The immediate platform is one rollnw client process with one UI thread, one Smalls
runtime, one preview session, and one published `active_object` handle. The same
live object must be accessible from the preview document, RmlUi event handlers,
the command palette, and the terminal.

The property input is:

- a validated active `ObjectHandle`;
- the object's registered non-transient Smalls propsets;
- Smalls type and field metadata;
- the current live propset values; and
- tree expansion and viewport state.

The common display output is a viewport-sized batch of property rows. Persistent
creatures currently have five propsets and 85 direct fields. Fixed and dynamic
arrays expand that count; for example, item visual arrays expose 139 scalar
elements when fully expanded. Collections such as feats and spellbooks also
depend on rules data that is not present in their storage shape.

Invalid or stale object handles produce an explicit empty state. Transient
propsets are omitted from the authoring surface. Unsupported field types remain
visible and read-only; they are never interpreted as raw bytes or JSON.

The reflected snapshot is implemented read-only. One exact scalar field,
`CreatureStats.plot`, now proves the shared transaction path without making
editability an inferred property of reflection. Feat assignment proves the
managed-view path against the same live object.

## Decision

Build an object workbench containing several property surfaces. Do not build a
universal reflected editor.

```text
Object workbench
|-- stable identity and active-document state
|-- live preview
|-- Properties: reflected Smalls propset tree/grid
|-- object-specific task views
|-- Variables
|-- Description
`-- Comments
```

The balance varies by object type. A trigger, waypoint, or door may be dominated
by the property grid. A creature or item requires several task views. An
encounter should combine its scalar grid and member list in one workspace.

The reflected surface provides complete structural inspection and, later, only
those direct scalar edits whose invariants can be enforced at the field
boundary. It is also the fallback when no curated presentation exists.

Curated task views are ordinary RML plus Smalls behavior. They invoke named
Smalls operations against `active_object`; they do not edit backing arrays or
propset fields directly. Writing the actual RML for a real workflow is preferred
to inventing an annotation language that attempts to generate it.

## Surface Classes

### Reflected Inspection

All persistent propsets appear as root groups. Structs and arrays are expandable.
Primitive values use direct formatting; newtypes and native values use registered
Smalls string operations where available. This surface is read-only in the first
slice.

### Direct Property Controls

Later work may add validated controls for genuinely local values such as flags,
bounded numbers, strings, and resource references. Editability is an explicit
toolset decision. It must not be inferred from field names, type names, or
current values.

### Managed Task Views

Rule-dependent or multi-field transforms require dedicated views and named
operations. Initial known cases include:

- feat assignment;
- class addition, removal, and leveling;
- known and memorized spells, spell slots, and metamagic;
- special ability level and use counts;
- creature, placeable, store, and item inventories;
- appearance parts, palettes, and visual model selection;
- item property construction and parameters;
- encounter member composition;
- variable tables; and
- localized text.

A managed view may read and update several propsets. It owns presentation state
such as filters, selection, and summaries, but it does not own a second copy of
the object data.

## Reflected Row Protocol

The display transform is batch-first:

```text
build_property_rows(active object, propset types, expansion state)
    -> PropertyTreeSnapshot

slice_visible_property_rows(snapshot, viewport range)
    -> RmlPropertyRow[]
```

`PropertyTreeSnapshot` owns contiguous node rows, path segments, and a text
buffer. Rows reference parents, subtree bounds, types, paths, flags, and text by
index or offset. The snapshot lives until the active object or property model is
invalidated. The RmlUi adapter owns only the visible row batch plus overscan.

The active object is a documented singleton exception: rollnw client currently has
one active preview object. Propsets, fields, and visible rows remain plural batch
transforms.

Runtime metadata pointers may be read during cold snapshot construction because
the Smalls runtime owns their arena lifetime. They must not cross into the row
protocol; output rows use indices, offsets, and `TypeID` values.

Dynamic arrays are expanded on demand. Snapshot construction defaults to 16,384
rows and 32 levels after measuring representative Creature and Item shapes.
Exceeding either bound produces a visible limit row rather than unbounded DOM or
snapshot growth.

## Virtualization Invariant

Every object-workbench surface whose row count depends on object, rules, or
module data must materialize only a viewport window plus bounded overscan into
RmlUi. This includes reflected propsets and arrays, feats, spells, abilities,
classes, inventory, item properties, encounter members, variables, and future
data-dependent row surfaces.

For fixed-height rows, the DOM row bound is:

```text
ceil(viewport_height / row_height) + 2 * overscan_rows
```

clamped at the start and end of the source. The source snapshot may be larger,
but it has its own explicit count/depth bounds and remains contiguous. Filtering
or expansion rebuilds or reindexes the source snapshot; it does not render the
entire result as an intermediate RmlUi document.

Fixed-column catalogs retain the same flat source protocol. Smalls supplies a
column count in `[1, 16]`; invalid values reject list creation. The host derives
`ceil(source_count / columns)` logical rows and maps a flat selection index to
`index / columns` only for scrolling. The materialized card bound is:

```text
(ceil(viewport_height / row_height) + 2 * overscan_rows) * columns
```

The common one-column path does not group rows. RML/RCSS owns card geometry and
styling; no item category, model rule, or equipment policy enters the list host.

Variable-height rows require measured-height virtualization or a presentation
that deliberately constrains row height. Rendering every row is not an allowed
fallback. Fixed, explicitly bounded chrome is not a virtual list; for example,
the recent-project list is capped at 12 entries and workspace task tabs are
small navigation state. The Creature inventory is also bounded: its native
contract exposes 100 cells per active page, so the surface switches pages
instead of scrolling or materializing all six pages.

The current Properties, Feats, Spells, and Appearance surfaces use
`VirtualListController` with bounded, surface-specific overscan. The Appearance
selector exposes exactly ten 30-pixel rows before scrolling. A controller-level
regression test uses a 100,000-row source to verify that emitted DOM rows remain
bounded by the viewport window rather than source size. Existing shell surfaces
outside the object workbench are audited in
`issues/rollnw-client-virtualize-shell-row-surfaces.md`.

The common reflected access pattern is linear over registered propsets and
their fields, with expansion decisions made once per aggregate during cold
snapshot construction. The managed Feats projection is a linear merge of the
rules table and sorted live feat IDs. Viewport slicing is a contiguous span; no
per-row runtime lookup occurs in the render loop.

## Creature Spell Row Protocol

The spell surface consumes one live Creature handle, its fixed eight class
slots, the Smalls-owned class/metamagic/spell configuration, and its ability
loadout rows. Smalls emits three typed row batches: available spellcasting
classes, metamagic choices allowed by the Creature's feats, and spells valid for
one selected class/metamagic pair. Invalid objects or malformed rows reject the
complete snapshot and produce a visible diagnostic.

`CreatureSpellViewSnapshot` owns contiguous class, metamagic, and spell rows plus
one text buffer until the active object or its mutation epoch changes. Rows use
integer IDs and text offsets; no runtime pointer crosses the projection
boundary. Spell IDs and class IDs must be nonnegative, spell tiers must be in
`[0, 9]`, metamagic codes must be in `[0, 255]`, and use counts must be
nonnegative. Out-of-range rows reject the snapshot rather than being clamped.

The display transform is batch-first:

```text
Smalls get_spell_*_editor_rows(active creature, class, metamagic)
    -> CreatureSpellViewSnapshot

filter_creature_spell_rows(snapshot, query, tier)
    -> stable row-index array

slice visible row indices(viewport, 30px row, 8-row overscan)
    -> RML row batch
```

Snapshot construction and filtering are linear in the valid spell-row count;
sorting is paid once per class/metamagic snapshot. The current NWN rules input
contains 834 spell definitions. RmlUi materialization depends only on viewport
height and overscan. This algorithmic cost is verified structurally; no wall
clock performance claim is made for this slice.

Class, spell-level, and metamagic selection share one `VirtualComboBox` with at
most 11 copied choices. Its popup is constrained to the workbench and flips
above the field when the bottom edge cannot contain it; native RmlUi select-box
placement is not part of this surface's interaction contract.

Cold snapshot construction reads Smalls heap rows and runtime definitions by
pointer because the runtime owns both for the duration of the synchronous copy.
The output and render paths use only contiguous indices and offsets. Retaining
those pointers would add lifetime coupling without removing any measured work,
so the protocol does not do it.

## Item Editor Row Protocol

The item task views consume one live Item handle, the Smalls-owned base-item
configuration, the Item's visual arrays, and its assigned item properties.
Smalls emits four typed row batches: visible model parts, color channels,
available property definitions, and assigned properties. Model and property
option batches are built only while the corresponding selector is open.

The observed project corpus contains 1,460 item blueprints. Its base-item table
contains 91 rows: 39 simple, 2 layered, 49 composite, and 1 armor row. An item
has at most 19 model parts and 6 color channels. The current rules configuration
contains 84 item-property definitions, and the largest observed item has 40
assigned properties. Invalid handles or malformed typed VM rows reject the
complete snapshot copy. The native RmlUi projection omits duplicate part IDs
or color-channel keys and color rows outside the drawable palette domain; an
invalid active color row produces a local diagnostic.

The Item appearance surface copies the complete SmallS policy snapshot into a
native RmlUi data model. That model owns bounded part/color presentation rows,
selector state, focus, and the fixed 176-cell palette index for the active
Item. Static structure and appearance event wiring live in RML. The variable
model/property option batches remain viewport-sized managed-list windows.
Native commands route edits through the shared transaction bus and store exact
flat before/after values for undo. No VM pointer or RmlUi element pointer is
retained across the snapshot copy.

The General surface follows the same boundary. `core.object` exposes only the
live ObjectBase `resref`, resolved name, tag, and comment. `nwn1.item` combines
those values with `ItemDescriptor`, `ItemStats`, and the base-item config into
one fixed nine-row batch. Smalls owns group order, labels, base-item fallback,
boolean formatting, and the RML presentation. Invalid or stale Item handles
produce an empty batch. The shared one-MiB `set_rml` command limit rejects an
oversized complete presentation; it does not truncate or modify the live data.

The General surface is read-only until localized text edits have an exact patch
protocol. A resolved display string is not sufficient undo data for a
`LocString` or `TextRef` because it omits other languages, TLK fallback, and the
difference between authored text and runtime override.

SmallS owns part order, labels, legal sparse model values, palette meaning,
property definitions, option labels, defaults, and mutation policy. Native C++
owns selector state and the copied RmlUi projection. Model/color command
preparation is a bounded batch transform over at most 19 model parts or 114
part/channel pairs. Invalid handles, unavailable sparse values, and out-of-range
values reject the complete edit. A presentation-row diagnostic does not block
the Item, module, or client.

Color edits cross the transaction boundary as a flat pair batch: Smalls emits
an opaque integer key followed by the exact stored value for each requested
part/channel. C++ preserves those keys and before/after values without knowing
the NWN part count, channel count, palette range, inheritance sentinel, or key
layout. Replay sends the opaque key batch back to Smalls, which decodes and
validates the complete batch before the first write. C++ retains only the
generic ordered-key, stale-value, rollback, and undo contracts.

Variable property and option rows use `VirtualListController`; their RmlUi row
count is bounded by viewport plus overscan. Appearance RML is regenerated only
when the live object or selector state changes. No performance result is
claimed because the slice has no measured latency requirement; the headless
regression verifies structural bounds and the live-object tests verify
mutation, undo, and redo.

`InventoryViewSnapshot` is native storage, not a second presentation policy.
C++ emits one contiguous live Item-handle batch in equipment-slot order followed
by inventory source order. Smalls consumes that batch and emits one aligned row
batch containing resolved display name, resref fallback, and stack size. A
count, order, type, or handle mismatch rejects the complete snapshot. C++ then
combines the accepted rows with native cell geometry, generated icons, hit
testing, and drag transaction data. Container-tab eligibility comes from the
Smalls base-item `is_container` field; C++ does not maintain a container-ID
list.

NWN Item inventory import supplies one 10-column by 6-row page, while Creature
inventory supplies six 10-column by 10-row pages. The shared renderer consumes
those live dimensions. The Item surface therefore hides page controls rather
than inventing a second inventory layout or rendering five unavailable pages.
The source Item inventory remains a bounded batch of at most 60 cells; invalid
page, row, column, or footprint data rejects the complete snapshot.

Project-tree insertion is a native placement transaction because its input is
a detached live Item handle and its output includes exact page, row, column,
footprint, and ownership state needed for rollback and undo. RmlUi hit testing
selects the target cell or equipment slot. Before a forward placement into an
Item owner, C++ makes one Smalls call for the complete owner decision;
`item_editor_has_inventory` reads the live Item propset and Smalls base-item
configuration. Native code then validates handle lifetime, occupancy, and cell
geometry and attaches the accepted batch. Inverse replay deliberately does not
re-evaluate current container policy, so undo can always detach the exact item
that the accepted forward transaction attached. This path does not encode base
item IDs or container rules in C++.

The rebuild cost is two O(n) linear passes and transient O(n) storage for the
handle and presentation batches. It is paid only when the inventory snapshot is
invalidated, not per frame. No performance result is claimed because this path
has not been benchmarked and has no measured latency requirement.

Project-tree placement adds one Smalls owner-policy call and one linear native
validation pass over the placement batch. It is paid once per committed drop,
not during pointer motion or rendering. No performance result is claimed for
this path because no placement-latency requirement has been measured.

## Mutation Boundary

Editing must follow the transaction contract described in
`tools/client/docs/transactions.md`:

```text
RML event or command-palette command
    -> Smalls toolset operation(active_object, typed payload)
    -> validation
    -> live object mutation batch
    -> transaction patches and diagnostics
    -> dirty/undo/preview/property-view refresh
```

The terminal, command palette, and object workbench must call the same operations.
RmlUi commands manage UI state only and must not become a second object mutation
API.

The edit protocol is a contiguous homogeneous patch batch for one live object.
Patch keys are strictly ordered and unique, values carry exact before/after
integers, and the batch owns its data through undo/redo. Validation rejects
stale objects, stale values, invalid schemas, invalid IDs, duplicate keys, and
out-of-range values before the first write. Successful application publishes
one mutation epoch; failed writes roll back the already-applied prefix.

## Cost

The reflected display path adds one cold traversal when the active object,
schema, or expansion state changes. The viewport operation returns a non-owning
span; only that bounded range plus overscan is materialized as RML.

Release measurements on an AMD Ryzen AI 9 HX 370, Linux x86-64, are:

| Object | Expansion | Snapshot CPU | Rows | Snapshot bytes |
| --- | --- | ---: | ---: | ---: |
| Creature | roots | 10.273 us | 90 | 7,717 |
| Creature | aggregate fields | 12.042 us | 112 | 9,455 |
| Item | roots | 1.329 us | 23 | 1,998 |
| Item | aggregate fields | 7.768 us | 162 | 13,106 |
| Encounter | roots | 3.090 us | 18 | 1,554 |
| Encounter | aggregate fields | 3.153 us | 18 | 1,554 |
| Trigger | roots | 3.999 us | 23 | 1,942 |
| Trigger | aggregate fields | 4.479 us | 23 | 1,942 |

The once-per-module Appearance catalog and repeated filter measurements are:

| Catalog | Source rows | Valid rows | Build median CPU | Owned bytes |
| --- | ---: | ---: | ---: | ---: |
| Creature | 15,100 | 838 | 1.735 ms | 2.648 MB |
| Placeable | 16,500 | 16,500 | 5.935 ms | 4.159 MB |

| Catalog | Query | Matches | Filter median CPU |
| --- | --- | ---: | ---: |
| Creature | empty | 838 | 0.699 us |
| Creature | `human` | 75 | 4.941 us |
| Placeable | empty | 16,500 | 13.764 us |
| Placeable | `human` | 4 | 38.622 us |

Visible-span slicing measured 0.892-0.901 ns CPU. These measurements describe
the benchmark fixtures and this machine; they are not a frame-time guarantee.

Each curated task view has an explicit implementation and maintenance cost. That
cost is paid only for observed authoring workflows. The old Qt project already
identifies the first useful set, so no speculative widget registry or general UI
generator is required.

## Simplification Pass

This design removes:

- JSON parsing from live object presentation;
- an editor-owned object or schema copy;
- per-frame reflection and polling;
- a general annotation language;
- C++-generated item presentation RML;
- direct array editing for managed gameplay collections; and
- the requirement that the reflected grid solve every authoring workflow.

The property snapshot is built only when invalidated. Array children are built
only when expanded. Only viewport rows are exposed to RmlUi.

## Build Sequence

1. Add the read-only Smalls propset tree/grid for the live active object.
2. Place it in the object workbench as the `Properties` surface while preserving
   the live preview.
3. Verify Creature, Item, and a scalar-heavy object such as Trigger or Door.
4. Add snapshot and visible-slice benchmarks using those real objects.
5. Implement the transaction boundary with one direct scalar edit.
6. Implement one managed task view, preferably Feats, to prove that curated RML
   and Smalls operations coexist with reflection without an annotation system.
7. Port further workflows according to measured authoring value, not Qt class
   structure.

## Implementation Evidence

- `tools/ui/smalls_property_tree.*` builds the bounded flat reflected snapshot
  directly from the live Smalls propsets; it does not read JSON or retain runtime
  pointers.
- `tools/client/object_edits.*` validates homogeneous patch batches, applies
  them atomically with rollback, publishes one mutation epoch, and supplies the
  shared undo/redo action.
- `tools/ui/smalls_creature_feats.*` linearly merges the sorted live feat IDs
  with the rules table into a filtered presentation snapshot.
- `core.creature.set_feat` is the named Smalls mutation used by
  `object.creature.set_feat`; RML, terminal, and command palette dispatch that
  same command through the command bus.
- The Spells surface calls Smalls batch functions to obtain the Creature's
  spellcasting classes, feat-gated metamagic choices, and valid spell rows. C++
  resolves display text once, sorts the snapshot once, filters a stable index
  array, and materializes only the viewport range plus eight rows of overscan.
  The three filter fields reuse one bounded virtual combobox, so option geometry
  and the selected integer key are explicit rather than delegated to native
  dropdown placement.
  Non-memorizing classes toggle known spells through
  `object.creature.set_known_spell`; memorizing classes adjust one exact
  `(class, spell, metamagic)` use through
  `object.creature.adjust_memorized_spell`. Both commands validate exact
  before/after values, publish one mutation epoch, mark the document dirty, and
  use the shared undo/redo stack. Memorized edits retain the selected row's exact
  tier, slot, and flags, including persisted rows whose flags are zero. The exact
  removal primitive clears that row; it does not infer policy from names or edit
  backing arrays in RML.
- The Appearance surface projects the Smalls creature or placeable config into
  one ascending catalog per loaded module, filters a stable index buffer, and
  materializes only the visible rows in a compact ten-row selector. The live
  appearance is selected and scrolled into view when the selector opens.
  Display names use a valid resolved `STRING_REF`, fall back to the humanized
  `LABEL` when the string reference is missing or invalid, and use the model
  resref only when both names are unavailable. `nwn1.creature.set_appearance` and
  `nwn1.placeables.set_appearance` validate and rebuild the live object visual;
  RML, terminal, and command palette use the shared `object.set_appearance`
  transaction command. Standalone object rebuilds retain the replacement scene's
  bounds fit so large and small appearances remain framed; area rebuilds preserve
  the editor camera. Visual undo and redo refresh both the selected catalog row
  and the closed Appearance field label from the live value.
- The Inventory surface snapshots one live Creature into a fixed array of 18
  equipment rows and a contiguous, source-ordered inventory row array. Each
  row carries an item handle, Smalls-produced text-buffer slices and stack size,
  source index, and
  validated page/row/column/width/height data from the native item-layout
  component. The RmlUi surface groups the 14 standard and four creature-only
  equipment slots, then projects one fixed 10-by-10 inventory page at a time.
  Standard equipment uses the fixed NWN icon-canvas layout rather than equal
  cells: the right-hand slot exposes 64-by-184 pixels of its 64-by-256 canvas,
  chest and cloak expose 64-by-120 pixels of their 64-by-128 canvases, and the
  remaining slots use their native 32- or 64-pixel dimensions. Assigned and
  empty-slot images share those bounds, so neither path stretches the source.
  Generated item icons cache their nontransparent pixel bounds once and center
  that visible rectangle in the equipment slot; inventory icons retain the full
  source canvas for footprint placement.
  This clipping is indexed by equipment slot and never inferred from a model,
  item, or texture name.
  The 100 cell elements are bounded chrome, not a data-dependent list; only the
  items assigned to the active page are materialized. The snapshot owns all row
  and text storage until invalidation. Invalid owner handles, mismatched Smalls
  row batches, uninstantiated item variants, and invalid footprints or
  coordinates reject the snapshot with a visible diagnostic.
  Cold snapshot construction temporarily resolves handles to object pointers
  owned by `ObjectManager`; no pointer crosses the snapshot boundary or enters
  the visible-row loop, which uses contiguous row indices and text offsets.
- Item instantiation materializes a Smalls-owned icon protocol into native item
  component data. Each item has two gender variants, each containing at most six
  ordered rows of resource, fallback, body part, flags, and six PLT material
  colors. Invalid variants, parts, flags, colors, empty non-fallback rows, and
  excess layers are rejected at the native boundary. `core.item` visual setters
  republish standalone visual rows and both icon variants once when a value
  changes; failed publication restores the propset value and republishes the
  prior state. Inventory projection invokes one Smalls batch, never one script
  call per item.
- The Item Appearance model selector consumes the flat, ordered option batch
  produced by `nwn1.item`, then uses the generic managed-list protocol as a
  five-column virtual icon grid. Selection keys and indices remain flat, so the
  same activation callback and command apply model changes. C++ derives only
  logical viewport rows; Smalls owns option validity, order, labels, icon
  resources, and the column count, while RML/RCSS owns the grid presentation.
- Project-tree Item drag materializes one detached live Item and keeps its handle
  in native drag state until cancellation or commit. The placement transaction
  records exact native cell geometry and ownership for rollback and undo, while
  Smalls decides whether an Item owner exposes inventory from its generated
  base-item configuration. Invalid owners, rejected policy, stale handles,
  overlaps, and out-of-range cells reject before attachment; rejected detached
  inputs are destroyed rather than leaked.
- Icon texture generation consumes a contiguous handle batch and emits aligned
  generated-texture source strings. C++ only decodes the Smalls-selected PLT,
  DDS, or TGA resources and alpha-composites ordered rows. A content-addressed,
  resource-generation-scoped CPU cache retains the active snapshot's unique
  RGBA rows; RmlUi uploads independent GPU textures synchronously. Missing rows
  retain a text label, and missing resources use only the fallback selected by
  Smalls.
- Creature inventory mutations use a homogeneous batch for one live Creature.
  A batch contains at most 18 rows with unique item handles and equipment slots.
  Every row records the live item handle, slot, and exact inventory coordinates
  needed by undo. Validation rejects stale handles, stale positions, occupied
  destination slots, duplicate items or slots, and out-of-range slot indices
  before the first mutation. Equipment mutation calls
  `nwn1.item.equip_item_for_authoring` and
  `nwn1.item.unequip_item_for_authoring` through the Smalls bridge; C++ owns
  only batch validation, rollback, undo data, dirty state, and mutation publication.
  Failure rolls back the applied prefix. RML, terminal, and command palette all
  use `object.creature.equip_inventory_item` and
  `object.creature.unequip_slot` through the shared command bus.
- The active preview tab saves by serializing the live object through the
  existing component/propset blueprint serializer and atomic file replacement.
- Focused tests cover Creature, Item, Encounter, Trigger, bounded expansion,
  live rebuilds, scalar edits, feat edits, stale values, dirty state, undo/redo,
  mutation epochs, and atomic save. The save test mutates both the direct scalar
  and managed feat paths, destroys the source object, reloads the saved JSON
  through `ObjectManager`, and verifies the edited propsets on the new live
  object. The benchmark records snapshot rows, owned bytes, and visible slicing.

## Verification

- The Release `rollnw_test` and `rollnw-client` targets build successfully.
- A headless RmlUi regression verifies that a 4,340-row virtual list exposes the
  full 130,200-pixel scroll extent, reaches row 4,339, and retains only the
  viewport window plus overscan in its RML. The final row also retains the
  virtual-list selection class.
- A managed-list regression verifies that a 100-item, five-column model catalog
  exposes 20 logical rows, materializes 15 cards for a two-row viewport plus
  one row of overscan, and reaches and selects flat item 99 in the final window.
- The live creature fixture's appearance is present in the catalog.
- A viewer regression verifies that standalone live-object rebuilds refit the
  camera while live-area rebuilds preserve the existing editor view.
- `rollnw_test --gtest_filter=Client*` passes all 117 tests across 25 suites.
- The focused workbench, virtualization, object-edit, Creature operation, and
  headless RmlUi/Smalls binding tests are included in that client regression
  set.
- Focused Creature inventory tests build the projection from a real module and
  Creature, reject an invalid active object, equip a real item, and verify dirty
  state, visual mutation publication, and exact inventory-coordinate restoration
  across undo and redo.
- The focused Item editor regression runs 15 tests across Smalls policy,
  sparse resource-backed model options, appearance transactions, opaque
  color-key replay, item-property undo, the five-column virtual model selector,
  RML/Smalls compilation, one-page Item inventory projection, and exact
  container placement undo. All 15 pass. The
  Item inventory fixture verifies the imported 1-page, 6-row, 10-column shape
  instead of assuming Creature inventory dimensions.
- Focused Creature spell tests build a sorted projection from a real Wizard,
  verify class-aware spell levels and filters, and reject invalid object
  handles. Known-spell and exact memorized-use edits both restore their live
  Smalls values through undo and redo; the memorized case also restores an exact
  tier/slot row with its original zero flags. The focused spell, virtual-list,
  and command tests pass all 8 selected tests.
- The atomic-save test reloads edited scalar and managed feat data through the
  production object loader rather than inspecting JSON alone.
- A runtime smoke test reaches `rollnw client running` after loading the panel,
  compiling its inline Smalls handlers, and initializing the renderer.
- The Release property-snapshot and visible-slice measurements are recorded in
  the Cost section above. No unmeasured shell-list performance claim is made.

## Resolved Runtime Defect

The Item inventory row protocol exposed an object-subtype array and struct-field
representation defect in the shared Smalls runtime. Typed Item arrays now store
inline object handles, preserve their declared subtype through field access, and
reject incompatible object subtypes. The compiler and VM use the same
object-like classification. Focused tests cover typed Item arrays, aligned
inventory rows, Smalls-owned container eligibility, opaque color-key round
trips, and visual undo/redo; the Item surface contains no local workaround.

## Inventory Follow-ups

The current slice equips existing inventory rows into empty slots, unequips
occupied slots back to their exact inventory coordinates, and inserts a
project-tree blueprint as a new live inventory Item with exact undo. The
following work is deliberately unresolved because its ownership policy is not
yet fixed:

- Define removal of an existing inventory Item, including whether removal
  destroys it, returns it to a project/workbench holding area, or creates a new
  blueprint artifact.
- Add occupied-slot replacement only after the desired swap and displaced-item
  placement behavior is fixed. The current command rejects occupied slots and
  represents replacement as explicit unequip then equip actions.

## Data-object Workbench Status

The standalone Trigger, Sound, and Store documents are data-only workbenches:
they hide the unused model viewport and give the editor the full workspace body.
Selecting those same objects in an Area keeps the Area viewport and uses the
right-side workbench. Waypoints retain their model preview. Encounters use the
viewport for a bounded spawn-group preview.

- Trigger exposes Details and Variables only. Its sparse layout is deliberate;
  no invented preview or duplicate summary fills otherwise unused space.
- Encounter adds a Spawns projection over `EncounterState.creatures`. Dropping
  a Creature blueprint appends one spawn record through an exact, undoable
  full-array replacement. The standalone preview reads the same ordered spawn
  array and displays up to 16 creature blueprints in a centered grid. Missing
  or empty Creature resources are skipped with load diagnostics; rows beyond
  the bound are dropped from the preview with a limit diagnostic.
- Sound adds a Sounds projection over `SoundState.sounds`. Dropping an imported
  WAV resource appends its resref through an exact, undoable full-array
  replacement.
- Store adds an Inventory projection over its five fixed inventory categories.
  Each category is an explicit drop target; dropping an Item blueprint inserts
  it into that exact category through one undoable placement command.
- Placeable adds Smalls-owned Appearance selection and the existing paged
  inventory grid shared with Creature and Item.
- Item already has Details, Variables, Appearance, Item Properties, and
  Inventory; no parallel workbench was added.

The encounter, sound, and store transforms build contiguous `ListItem` source
batches in Smalls. Inputs are capped at 1,024 displayed source rows; additional
rows are dropped from the presentation batch and reported in the list title.
The shared managed-list host materializes only the viewport plus six rows of
overscan into RmlUi. Invalid or stale active objects publish an empty list with
an explicit status message.

Encounter and Sound insertion are implemented as bounded batches of at most
1,024 entries. Each command snapshots the complete ordered source array,
rejects stale or invalid input before mutation, and restores the exact prior
array on undo. Removal and explicit reordering remain unresolved because those
interactions have not been selected.

Store insertion is a bounded batch of at most 1,024 detached live Items. Every
row names one of the five real inventory categories; category is never inferred
from the Item or its name. The command rejects invalid categories, duplicate or
stale handles, oversized layouts, and fixed item-capacity overflow before
mutation. It records the exact category and grid coordinates for undo and redo,
adds one page only when the selected category is full, and removes that page on
undo when it is empty. Store removal and explicit reordering remain unresolved.
The five arrays are not flattened into a universal collection editor.

The Encounter preview transform is linear in at most 16 spawn rows. The current
imported project sample contains 466 Encounter blueprints: 387 have one row and
the measured maximum is six. The transform first builds one temporary creature
scene per valid row, then measures the batch and places it once in a square grid.
Temporary Creature objects are destroyed after their render rows are moved;
the Encounter remains the sole owned live root. No timing claim is made for
this human-driven rebuild path.

## Spell Follow-ups

The old Qt screen exposed Clear, Load, Save, and Summary actions. Their required
data is not present in the current authoring contract: no preset artifact format,
scope across multiple classes/metamagic variants, or clear/summary semantics has
been selected. They are deliberately omitted until a real workflow fixes those
inputs and outputs. The implemented slice edits live known and memorized spell
state only.

## Done Criteria

- Preview, terminal, command palette, and property surface resolve the same live
  `active_object`.
- The first slice displays all persistent propsets without reading serialized
  JSON or constructing a second object model.
- Large expanded arrays do not create an unbounded RmlUi document.
- Every data-dependent object-workbench list has a bounded source snapshot and a
  viewport-bounded RmlUi row batch; tests prove DOM row count is independent of
  total source count.
- Invalid handles, unsupported types, and materialization limits have visible,
  deterministic behavior.
- The property surface can coexist with a handcrafted managed view without
  either surface owning duplicate object state.
- A feat assignment performed through the managed view and the equivalent
  terminal command use the same Smalls operation and refresh both views.
- Known-spell and memorized-use edits performed through the Spells view and the
  equivalent terminal commands use the same Smalls policy and shared transaction
  protocol.
- Save-before-close, dirty state, undo, and preview refresh are driven by the
  shared transaction result rather than widget-local state.

Evidence against this direction would be an inability to express the required
managed workflows through ordinary RML and Smalls without duplicating object
state or bypassing transactions. In that case, document the concrete workflow
and data that fail before adding a new UI description layer.

## Do Not Build Yet

- A universal property editor.
- An annotation DSL for generating task views.
- A second reflection API implemented inside Smalls solely for RmlUi.
- Direct editing for arbitrary arrays, maps, structs, or native values.
- A broad port of Qt widget classes or Qt-specific model abstractions.
- A custom widget registry before the first managed RML/Smalls view demonstrates
  the missing contract.
