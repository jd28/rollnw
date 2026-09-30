# Client inventory removal, replacement, and ordering

Status: removal and equipment replacement implemented; Store ordering remains open. Extracted
2026-09-26 from the completed
[object-workbench milestone](closed/rollnw-client-object-workbench-and-property-surfaces.md).

## Atomic equipment replacement (Tier 2, 2026-09-30)

### Patterns & Conventions Found

`objects/Inventory.cpp:137,237` already provides bounded grid placement;
`objects/Equips.cpp:39` owns policy-free slot storage. The current profile path
(`smalls/scripts/nwn1/item.smalls:359`) unequips before removing the incoming
item, so it cannot use the freed space and publishes an intermediate state.
`tools/client/object_edits.cpp:2950` applies/rolls back rows individually;
`inventory_workbench_view.cpp:534` treats an occupied-slot click as unequip even
when an inventory item is selected. Existing native struct bindings, SmallS
callbacks, command/history and renderer visual refresh are reusable. Follow
adjacent snake_case, value records, spans, explicit rejection and clang-format.

### Architecture Decision / Component Design

Add a renderer-independent equipment batch protocol and prepare/commit operation
beside Equips. Input rows identify slot and expected/replacement Item handles.
Preparation reads the Creature's complete live equipment and inventory, computes
final membership/placement without mutation and captures exact before/after
snapshots. Commit revalidates service generation, source state and footprints,
then publishes preallocated storage and one equipment revision. Snapshots borrow
identities; they never own/destroy Items. Editor undo reverses the same snapshots.

Use existing inventory placement order, prefer the incoming item's vacated cell
for the displaced item, and otherwise use the first fitting cell. No automatic
inventory growth or world drops. Batched requests may exchange equipped slots;
incoming Items must belong to this Creature's inventory or a changed slot.
Unresolved references, duplicate ownership/slots/targets, invalid geometry,
stale state and insufficient space reject the entire batch. Empty batches are
no-ops. Detached-item initialization remains on the existing low-level path.

SmallS owns gameplay eligibility, effects and callbacks. A batch notification
updates all effects before publishing existing per-item callbacks and a new
committed-equipment callback containing old/new identities and slots. Consumers
also receive an explicit authoring/gameplay flag so editor undo cannot be
mistaken for a gameplay animation request. Consumers observe final membership; failed validation emits no notifications. A callback
fault is reported after commit, never disguised as an uncommitted operation or
rolled back through additional callbacks. Editor notifications use the same
profile path, with compatibility-only validation and existing document history.

### Implementation Map

- Create `lib/nw/objects/equipment_changes.hpp/.cpp`: explicit bounded protocol,
  preparation, state/footprint validation and atomic forward/inverse storage.
- Extend native `core.item` binding/script and `nwn1.item`: shared inventory
  batch entry point, gameplay checks, post-commit effects/callbacks. Extend
  `profiles/nwn1/scriptbridge` to notify editor commits through that same policy.
- Replace the editor-only sequential inventory move implementation with the
  shared snapshots; enable selected-item replacement on occupied-slot clicks.
- Add native/profile, editor undo/persistence and UI/renderer regression tests;
  update client README and record animation follow-up under `issues/`.

### Data Flow / Cost

One owner thread on Linux/Windows desktop or headless simulation; 18 equipment
slots, at most 1,024 inventory entries, 10x10 cells/page, existing client page
bound 255. Real fixtures include `pl_agent_001.utc`, belts, gloves and composite
weapons. Actual swap frequency/distribution is unmeasured.
ASSUMPTION: one selected inventory item is the common gesture; it uses the same
batch path as multi-slot requests. ASSUMPTION: authoring applies immediately;
gameplay action/animation policy chooses when to submit/revalidate its request.

Stable layout/profile resources plus volatile membership -> bounded native
request -> prepared final inventory/slots -> revalidate/commit -> profile effects
and committed rows -> existing visuals/history. Work is on edits, never per
render frame. Linear snapshots and grid searches cost O(inventory rows + grid
cells per displaced Item); retained undo memory is two bounded snapshots plus
footprints. Object-manager lookups borrow existing pointers only while producing
handle/index records. No measured performance improvement is claimed.

### Build Sequence / Critical Details

1. Shared native transaction and rejection tests, then profile entry points.
2. Editor integration and one-action undo, then occupied-slot UI routing.
3. Build client/tests; verify capacity with freed space, mismatched footprints,
   failed batches/no events, final-state callbacks, gameplay policy rejection,
   deferred stale requests, exact undo/redo, save/reload and rendered equipment.

Simplification: reuse Inventory's bounded placement and existing callback/visual
plumbing. Remove sequential rollback and duplicate editor ownership logic. No
animation queue, reservations, networking, new ownership service, or persistence
format. Animation timing/cancellation requirements remain in the
[runtime animation follow-up](runtime-equipment-animation.md).
Done requires one-step UI replacement, exact atomic ownership/history, and a
headless gameplay path with old/new commit data. Intermediate callback state,
item loss, callbacks on rejection, or editor dependencies in core disproves it.

### Verification and final self-check

- Release `rollnw-client` and `rollnw_test` builds passed. The final build has no
  compiler warnings. Formatting checks passed for all 15 changed C++ files, and
  `git diff --check` passed.
- The broad run covered 185 tests: 63 object edits, five inventory providers,
  four native equipment transactions, 96 SmallS engine integrations, two rendered
  scene tests and 15 inventory workbench tests. One new CAF test used a filename
  instead of a tab ID; after fixing that test setup, all 12 focused tests passed.
  No selected tests were skipped. Logs/XML are in `/tmp/equipment-swap-*`.
- Seven new tests cover a completely full synthetic 2x2 inventory using the
  incoming Item's freed cell, insufficient space, multi-slot exchange, invalid
  and stale requests, unchanged revisions on rejection, exact coordinates/order/
  flags through undo/redo, final effects and membership in callbacks, and
  gameplay eligibility. Editor commit/undo/redo each publish an authoring event.
- Creature blueprint and instance JSON round trips match; saving the containing
  Area through workspace CAF persistence reloads the same complete Creature data.
  Native UI capture/dispatch replaces occupied gear in one history entry and
  rejects duplicate release. Offscreen weapon replacement, undo and redo refresh
  rendered equipment while preserving unrelated particle playback and camera state;
  existing equipped-armor visual regressions also pass.
- Simplification: existing placement, profile callbacks, command/history and
  visual refresh remain in use. Shared prepare/apply snapshots replace the
  editor's per-row rollback and metadata reconstruction. The publication step
  uses existing capacity and statically nonthrowing inventory construction and
  equipment assignment. Prepared batches must remain unchanged; they borrow Item
  identities and never acquire destruction responsibility.
- Final self-check: bounded plural inputs, one owning Creature per batch,
  explicit rejection, declared snapshot costs, scoped object borrows and no
  per-frame transform. Usage frequency remains an explicit assumption. No new
  scheduler, ownership service, persistence schema or performance claim was added.
  Post-commit script faults are reported while preserving editor history;
  injected callback faults were not tested.
- Manual desktop interaction and Windows execution were not verified. Actual
  draw/stow clips, marker timing and interruption/cancellation remain in the
  [runtime animation follow-up](runtime-equipment-animation.md).

## Existing data and behavior before equipment replacement

Creature, Item, and Placeable workbenches use the existing paged inventory
presentation. Inventory entries name owned live Items and their grid positions;
removing an entry is an ownership operation, unlike removing an Encounter or
Sound value-array row. The current workflow inserts and removes blueprint Items, equips
existing inventory Items into empty slots, and unequips them back to exact
inventory coordinates. Before this change, occupied-slot replacement rejected; the user
had to explicitly unequip and then equip.

Store inventory has five fixed categories. Insertion accepts bounded batches
of at most 1,024 detached live Items, records the exact category and coordinates,
and adds a page only when that category is full. Undo restores placement and
removes a newly added page when it is empty. The Store projection caps displayed
source rows at 1,024 and uses the shared viewport-bounded managed-list host.
Actual removal/replacement frequency and inventory-size distribution have not
been measured.

## Decisions required before implementation

- Removal decision: history owns detached Item trees, undo restores exact entries,
  and disposal destroys trees that remain detached. Implementation evidence below.
- Equipment replacement: commit final slots and inventory atomically, prefer the
  incoming item's freed cell for displaced gear, otherwise first fit; reject the
  whole request if space is insufficient. One history entry restores exact state.
- Store removal uses the same ownership decision, preserving category, row order,
  grid coordinates and infinite-stock flags.
- Define what explicit Store reordering changes: stored row order, grid
  placement, or another user-visible order. Establish category boundaries and
  persistence semantics from the intended workflow before designing the command.

This is authoring work. Authoritative gameplay pickup/drop belongs to
[runtime world placement](runtime-world-placement.md).

## Completion criteria

- Record the chosen ownership, replacement, and Store ordering contracts using
  actual inventory inputs, including category, page, coordinates, and capacity.
- Implement the agreed operations through the existing shared command/history
  path and managed inventory presentation, without a second ownership model.
- Reject stale selection/ownership, invalid destinations, capacity overflow,
  and other invalid batches before mutation; failures preserve all affected
  Items and placements.
- Verify exact ownership, category, coordinates, and agreed ordering through
  commit/undo/redo, including occupied slots and full inventories. Exercise
  detached Item lifetime when history is discarded under the chosen policy.
- Save/reload representative inventory and Store documents and verify the
  surviving Items and placements. Record UI interaction coverage and its limits.

Store ordering remains unimplemented. Measure its relevant source
sizes and operation costs when selecting the implementation; do not add a
generalized collection editor or ownership system in advance.

## Undoable inventory removal (2026-09-29, Tier 1)

### Patterns & Conventions Found

- `tools/client/object_edits.cpp:92,119` retain detached Items in placement
  history and destroy them when the last action releases ownership.
- `lib/nw/objects/Inventory.hpp:14` stores ordered entries with live identity,
  uint16 coordinates and an infinite-stock flag; `Inventory.cpp:66` fixes capacity
  at 1,024 entries. Stores have five independently paged categories.
- `tools/client/inventory_workbench_view.cpp:411` captures owning native click
  values and validates identity/epoch before dispatch. Store rows use the existing
  viewport-bounded managed list and SmallS callbacks.
- Existing placement helpers append entries and reconstruct their metadata, so
  reversing placement alone cannot restore imported order/infinite-stock flags.
  Reuse command/history, inventory storage and list presentation; no new framework
  or dependency is needed. Follow nearby spans, result diagnostics, RAII and format.

### Architecture Decision

Remove selected live inventory Items from Creature, container Item, Placeable and
Store owners. History owns detached Item trees until undo reattaches them or
history disposal destroys them. Preserve exact row order, coordinates, flags,
Store categories and page counts. Equipment continues to use explicit unequip
before removal. Rejection leaves all Items and inventories unchanged.

Inputs are a borrowed batch of unique live Item handles and one live owner. Find
their existing membership in that owner's one/five inventories. Capture ordered
before/after entry and occupancy arrays only for affected inventories. Validate
all snapshots before publishing into existing fixed-capacity storage. No owner,
file format, runtime gameplay operation or renderer protocol changes.

### Component Design / Implementation Map

- `object_edits.hpp/.cpp`: plural removal API; private owning history state;
  prepare affected snapshots, validate ranges/identity and atomic replay; ordinary
  mutation publication and document dirty state. Kernel generation scopes lifetime.
- `toolset_backend.cpp`: grid-index removal command and selected Store row removal;
  use current object/epoch keys and ordinary command history.
- `inventory_workbench_view.hpp/.cpp`: selected-item Remove button through existing
  native capture/dispatch; clear selection after success.
- `object_workbench_view.cpp`, `tools/ui/scripts/toolset/data_object_editor.smalls`:
  Store Remove button and current-context row keys identifying actual source rows.
- `tools/client/ui/panel.rml`: import the Store removal callback into the panel.
- Existing object-edit, native-workbench and SmallS/Rml tests; client README and
  this issue for completion evidence. No new source modules.

### Data Flow

Selected row -> captured identity/current context -> unique Item batch -> affected
ordered inventories and occupancy snapshots -> validate -> publish all -> one
undo action/dirty update. Undo/redo require the expected snapshot and live owner;
stale owner/rows, duplicates, unsupported owner, oversized batch, invalid geometry
or missing Item layout reject. Empty batches are no-ops. Preparation allocation
failure reports failure before mutation; publication uses preallocated storage.

### Build Sequence

1. Implement batch removal and focused ownership/order/rollback tests.
2. Wire grid and Store actions, stale selection checks and UI tests.
3. Verify blueprint and containing CAF save/reload, history disposal and nested
   Item lifetime; build client/tests and run related regression suites.

### Critical Details

Platform: Linux/Windows desktop, one UI/kernel thread, 1,024 entries per inventory,
at most five Store categories; fixed-capacity entries and contiguous occupancy
arrays. Existing fixtures include `pl_agent_001.utc`, `storethief002.utm`, native
Items and nested container test construction. Actual usage/size distributions are
unmeasured. ASSUMPTION: single selected-item deletion is the common gesture —
affects UI scope; it uses the same batch path as multi-item regression tests.
Membership and layout stay stable between commands; selection/epoch changes on
UI edits. Only affected inventories and document/mutation state are written.

Cost: cold UI-thread scans of at most five inventories, batch membership checks,
and copies of affected rows/occupancy; retain before/after metadata plus detached
Item trees for history. No per-frame work or performance requirement is added.
No speed claim; measure command latency/retained bytes if a responsiveness or
history-memory problem is observed. Scoped engine/DOM borrows are required by
existing APIs; processing/history store indices and generation handles.

Observed fixture counts: `pl_agent_001.utc` has no initial inventory entries;
`storethief002.utm` has 58 entries split across Armor 10, Miscellaneous 16,
Potions 6, Rings 8 and Weapons 18. Tests preserve these rows and add three real
Item blueprints to exercise ordered batch removal. These fixture counts do not
establish usage frequency or a representative size distribution.

Simplification: preserve page counts instead of adding compaction; share one batch
path across all four owners; use full affected snapshots instead of per-row partial
rollback and separate metadata reconstruction. No clipboard, replacement/swaps,
reorder, new serializer, or generalized ownership service.

Implementation simplification: removal publishes a properties notification,
because the removed Items are unequipped; no viewport rebuild is required.
Existing minus-button styling serves all four surfaces. Store initialization now
uses the same current-context key request as other managed lists, removing its
duplicate initialization helper. Workspace-scoped commands record one ordinary
undo action. Footprints are captured and checked on replay as well as row metadata;
all publication copies use existing capacity and statically nonthrowing entry copies.

Done: all four surfaces remove and restore exact entries, nested trees survive undo,
only detached trees die on history disposal, stale/invalid batches are atomic,
one transaction dirties the right document, and blueprint/CAF persistence matches.
Wrong item/owner, changed surviving metadata/order, stale UI deletion or a lifetime
failure disproves completion. Desktop/platform verification limits are reported.

### Verification and final self-check

- Release `rollnw-client` and `rollnw_test` builds passed without compiler warnings.
  All 77 selected object-edit, inventory-workbench, inventory-provider and related
  SmallS/Rml collection regressions passed; none skipped. Seven tests were added.
- All four owners round-trip removal, undo and redo through native blueprint files
  and containing CAF documents. Checks compare full serialized Item data, source
  order, exact live handles, positions, occupancy, page counts and Store flags.
- Batch tests cover different Store categories, duplicate/foreign/invalid handles,
  oversized and empty batches, malformed coordinates, stale footprints/metadata,
  destroyed owners, and rejection before any partial restoration.
- Nested Item trees retain their identities through undo/redo; history disposal
  destroys only detached trees. Placement followed by deletion composes correctly
  through both undo and redo stacks. Synthetic overlapping footprints preserve
  surviving occupied cells, and deleting all rows retains the inventory pages.
- Native clicks capture identity, selection, page, tab, generation and epoch before
  SDK dispatch; changed selection, wrong owner, duplicate release and old epoch
  reject. The Store test loads the actual panel, lays out its minus button and
  dispatches its SmallS callback; stale/unsupported object selections reject.
- Initial UI tests caught global command scope omitting undo recording and a
  missing panel callback import. Both were repaired; workspace-scoped commands
  now record exactly one undo action and the panel callback resolves normally.
- Simplification and final self-check: existing command/history, fixed inventory
  storage, bounded lists, button styling and serializers are reused. No new
  subsystem, dependency, persistent schema, per-frame work or performance claim.
  Inputs are bounded batches, errors reject before publication, and detached
  ownership is explicit. `clang-format --dry-run --Werror` and scoped
  `git diff --check` passed. Replacement and reordering remain separate work.
- Interactive desktop use and Windows execution were not verified. Timing and
  memory improvements were not measured or claimed.

### User acceptance

The user confirmed "it works!" and requested the commit. This accepts the removal
workflow; the platform and per-owner manual coverage were not specified.
