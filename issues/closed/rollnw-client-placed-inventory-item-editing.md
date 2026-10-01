# Placed inventory item editing

Status: closed on 2026-09-30. Implementation and recorded regression evidence
satisfy the scoped placed-item editing work; the user requested closure.

The shared Item workbench supports placed inventory and equipped Items, nested
navigation and Back, retaining the containing Area's ownership, history and
save target. The recorded Release builds and 166 affected regressions passed;
the later shared-pencil UI change passed 36 inventory/template regressions.

Closure preserves the verification limits: interactive end-user acceptance and
the pencil icon's appearance in the running client were not verified by those
checks. No new runtime or performance verification is claimed. The original
plan and completion evidence below are retained as history. Separate
[Store ordering work](../rollnw-client-inventory-removal-and-reordering.md)
remains active.

Tier 1. Edit contained/equipped instances through the existing Item workbench,
with the containing Area retaining ownership, save, and undo. Blueprint inventory
entries serialize references, so this feature is restricted to placed instances.
The benefit is removing the need to author a separate blueprint for a local item
variation. Stop at existing Item editing; standalone nested documents and runtime
inventory transfers are outside scope. Plan B is to retain inventory selection
without navigation if ownership cannot be validated.

## Patterns & Conventions Found

- `lib/nw/objects/Inventory.cpp:363`: blueprint references versus embedded instance
  payloads. CAF is the required output for these edits.
- `tools/client/object_document.hpp:20`: one document owns its root and descendants.
- `tools/client/object_workbench_view.cpp:2097`: activation and snapshot rebuilding.
- `tools/client/inventory_workbench_view.cpp:417`: capture owned click facts before
  RmlUi release, then reject stale targets before applying.
- `tools/client/toolset_backend.cpp:677`: Store selection uses generation, owner,
  mutation epoch, category and displayed index in its managed-list key.
- `tools/client/client_application_frame.cpp:310`: renderer selection currently
  controls the displayed object; nested editing must preserve the placed selection.
- `tools/client/client_application_workspace.cpp:46`: mutation synchronization is
  the existing cold boundary for snapshot and appearance refresh.
- `tools/client/blueprint_references.hpp:72` exposes ownership traversal only for
  blueprint replacement, with filtering and replacement ownership. Reusing that
  operation would require unnecessary blueprint state. Extend the workbench with
  a small native ownership snapshot instead; reuse Inventory/Equips accessors.

## Architecture Decision

Borrow the live Item in the existing Area workbench. A contiguous stack of parent
view facts provides Back; no extra document or undo service. One displayed
workbench is genuinely singular; item ownership discovery processes a batch of
all placed owners and their descendants. Editing does not change attachments.

Platform: the native Linux client, kernel/UI owner thread, existing pointer-based
Area/Inventory storage and RmlUi event dispatch. No asynchronous object borrows.
Cost: O(placed owners + contained items) reads and temporary rows on navigation or
relevant mutations; O(nesting depth) retained parent view records. Frame routing
uses stored root identity, with no ownership scan. Existing object pointers must
be borrowed while traversing their native arrays; snapshot rows retain only
generation-checked handles and indices. No measured performance claim.

## Component Design

- Ownership snapshot (`object_workbench.hpp/.cpp`): flat rows containing item,
  immediate parent, placed root and visual owner. Accept only live Area roots;
  unresolved blueprint references, stale objects or repeated/cyclic ownership
  reject the snapshot. Equipment's direct item refreshes its creature; invisible
  inventory contents have no visual owner. No pointers escape the call.
- Workbench navigation (`object_workbench_view.hpp/.cpp`): owned click facts,
  parent stack and cold membership validation. Back restores parent inventory
  surface/page/selection. Tab/root changes and invalidated attachments clear or
  unwind navigation. Invalid and stale clicks are consumed without edits.
- Application routing: retain renderer selection while editing descendants;
  route nested visual mutations to the equipped creature. Undo can resolve the
  correct visual owner even after navigating back.

Actual inputs are native Area owner arrays, inventories (Creature/Item/Placeable),
five Store categories and 18 equipment slots. The Store UI displays at most 1024
rows; grid selections and equipment slots must be in snapshot/live bounds.
DockerDemo's `storethief002.utm` has 58 entries (10/16/6/8/18 by category), while
`pl_agent_001.utc` starts with an empty inventory. No user navigation frequency
distribution has been measured. ASSUMPTION: ordinary use opens one contained item
at a time, with shallow nesting — affects stack storage only, not valid depth.
Attachments change on inventory/Area mutations; identities and view state are
stable between mutations. The common frame path reads only current identity.
Writes are existing Item edits, Area dirty/history state, and transient view state.

## Implementation Map

- Modify `object_workbench.hpp/.cpp`: batch ownership snapshot and lookup facts.
- Modify `object_workbench_view.hpp/.cpp`: capture/apply navigation, parent stack,
  target routing, mutation validation and Back hydration.
- Modify `inventory_workbench_view.cpp`, `ui/item_editor.rml`, inventory CSS:
  placed-only Edit controls and contextual Back.
- Modify `client_application_frame.cpp`, `client_application_workspace.cpp`:
  separate displayed contained item from placed selection and visual refresh.
- Modify `client_application_pointer.cpp`, `client_application_keys.cpp`: world
  selection/transform/delete actions return from nested editing to the placed owner.
- Modify client workbench/object-edit regression tests and `tools/client/README.md`.

## Data Flow

Area ownership arrays -> flat validated ownership rows -> captured selected item
and owner -> revalidate after SDK release -> push parent view -> activate Item.
Existing Item command -> Area history/dirty -> ownership-aware visual/snapshot
refresh -> CAF embedded item payload. Back pops parent view. Invalid ownership
rejects navigation; removed/replaced descendants unwind to an attached owner.

## Build Sequence

1. Implement ownership discovery and navigation with instance-only controls.
2. Connect frame/mutation routing and equipped visual refresh.
3. Add focused regression coverage; build client/tests and run affected suites.

## Critical Details

Simplification: reuse all Item fields, commands, serialization and undo (do not
build these again); collect ownership only at cold boundaries; constrain to Area
documents; keep handles rather than copies/standalone nested documents. No new
cache invalidation service, script ABI or general navigation framework.

Done: inventory and equipment instances open the existing Item editor, Back
returns to the owner, changes remain in the Area history and survive CAF reload,
equipped visual refresh targets the creature, and blueprint/stale/detached targets
cannot enter this workflow. Evidence against the design would be a nested Item
selected as a world object, lost data after reload, undo in another document,
stale attachment edits, or full ownership traversal per frame.

Verify native capture after DOM replacement, recursive containers, all four owner
types, Store selection identity, stale generation/tab/epoch/attachments, Back,
undo/redo and CAF round trips. Follow neighboring types/style and clang-format.
Interactive GPU/UI behavior requires a running client; report that separately
from automated coverage. Final self-check and actual results are recorded below.

## Completion and verification

Implemented the placed-only Edit controls, nested Item workbench and contextual
Back button. Creature, container Item and Placeable share inventory markup; Store
uses its current managed selection. Parent selection is retained by Item identity,
so removal/reindexing cannot select an unrelated row on return. Stale tab,
generation, mutation, surface, selection and ownership facts reject navigation.
Mutations unwind detached descendants; normal tab/placed-selection changes leave
nested editing. The Area document remains the sole owner and history target.

Equipped Item visual mutations resolve to the placed Creature even after Back or
undo. Invisible inventory contents do not request a world visual rebuild. World
transform/delete controls explicitly restore the placed target. No Item command,
serialization format, document ownership or SmallS ABI was added.

The final simplification pass removed a second inventory rebuild on Back and
repeated path lookups: parents precede children in the ownership rows, so path
validation uses one forward pass. The common frame route compares stored identity;
only navigation and relevant mutations collect ownership. Costs remain O(placed
owners + contained items) per cold operation and O(depth) retained view records.
The native Area's pointer arrays are traversed once to produce handle/index rows;
there is no new pointer-heavy hot path or speculative cache/service.

Verification on 2026-09-29:

- Release builds of `rollnw-client` and `rollnw_test` passed, without compiler warnings.
- 166 affected regression tests passed in 82.283 seconds, zero skips. XML:
  `/tmp/placed-items-regressions.xml`; log: `/tmp/placed-items-regressions.log`.
- Four added native UI tests cover all four owners, recursive containers,
  Back/selection restoration, Area document/history retention, CAF save/reload,
  equipped slot preservation, hit testing the equipment Edit button, stale captures
  across DOM replacement, wrong tab/generation/epoch/selection, detached items,
  duplicate ownership rejection, and removal undo/redo while navigating.
- The existing `AreaEditsPreserveUnrelatedPlayback` renderer regression now edits
  an equipped Item's model and replays undo/redo through its resolved Creature.
  Rendered frames, Item identity, world selection, camera, unrelated model handles,
  particle state and animation time are checked. The test uses the existing bodak
  model fixture because the test assets do not include the human base rig.
- Existing Item commands/editors, inventory operations, workbench fields, RmlUi
  templates, and standalone Item visual refresh regressions passed.
- `clang-format --dry-run --Werror` and `git diff --check` passed.

Final self-check: framing/data/cost and the unmeasured navigation-distribution
assumption are documented; the ordinary frame path remains independent of tree
size; errors reject explicitly; batch ownership rows and the singular displayed
workbench are justified; no speculative generality or unmeasured performance
claim was added. Done criteria are covered by automated tests. Interactive end-user
acceptance in the running client has not been performed in this change.

UI follow-up: replaced the inventory, Store and equipment Edit labels with a
shared CSS pencil, following the existing CSS-drawn icon convention. Tooltips
retain the action name. Inventory/Store use the existing square action buttons;
equipment uses a 24dp square. Back continues to use the shared left arrow.
No icon dependency or navigation behavior was added. Client/test builds and
36 inventory/template regressions passed (zero failures), including equipment
button hit testing; formatting and diff checks passed. The icon's appearance
in the running client remains to be reviewed.
