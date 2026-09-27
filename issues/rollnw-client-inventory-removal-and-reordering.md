# Client inventory removal, replacement, and ordering

Status: open. Extracted 2026-09-26 from the completed
[object-workbench milestone](closed/rollnw-client-object-workbench-and-property-surfaces.md).

## Existing data and behavior

Creature, Item, and Placeable workbenches use the existing paged inventory
presentation. Inventory entries name owned live Items and their grid positions;
removing an entry is an ownership operation, unlike removing an Encounter or
Sound value-array row. The current workflow inserts blueprint Items, equips
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

- Define the result of removing an existing Item: destruction, transfer to a
  project/workbench holding area, or creation of a blueprint artifact. Specify
  ownership and lifetime through commit, undo, redo, and history disposal.
- Define occupied-slot replacement, including swaps, displaced-item placement,
  full inventory, and failure behavior. Keep explicit unequip/equip available
  until that policy is settled.
- Define Store removal under the same ownership decision, preserving its
  explicit category and grid coordinates.
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

No implementation or performance improvement is claimed by this issue. Measure
the relevant source sizes and operation costs when selecting the implementation;
do not add a generalized collection editor or ownership system in advance.
