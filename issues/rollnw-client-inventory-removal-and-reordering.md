# Client inventory removal, replacement, and ordering

Status: removal implemented; replacement and ordering remain open. Extracted
2026-09-26 from the completed
[object-workbench milestone](closed/rollnw-client-object-workbench-and-property-surfaces.md).

## Existing data and behavior

Creature, Item, and Placeable workbenches use the existing paged inventory
presentation. Inventory entries name owned live Items and their grid positions;
removing an entry is an ownership operation, unlike removing an Encounter or
Sound value-array row. The current workflow inserts and removes blueprint Items, equips
existing inventory Items into empty slots, and unequips them back to exact
inventory coordinates. Occupied-slot replacement currently rejects; the user
can explicitly unequip and then equip.

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
- Define occupied-slot replacement, including swaps, displaced-item placement,
  full inventory, and failure behavior. Keep explicit unequip/equip available
  until that policy is settled.
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

Replacement and ordering remain unimplemented. Measure their relevant source
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
