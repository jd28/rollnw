# rollnw | client Transactions

rollnw client's current transaction boundary is the composition of `CommandBus`, a
validated mutation batch, `CommandUndoAction`, and `WorkspaceState`. There is no
general `ToolsetTransaction` type and no serializable server transaction
protocol.

The implemented path is intentionally narrow: one active workspace tab, scalar
or managed-data patches against a live object, and Creature/Placeable membership
batches against a live area.

## Data Contract

`ObjectEditBatch` owns a contiguous vector of `ObjectEditPatch` rows:

```text
ObjectEditBatch {
    kind: propset_int | propset_int_element | creature_feat
    patches: [
        object: ObjectHandle
        propset_type: TypeID
        key: uint32
        before: int32
        after: int32
        element_index: int32  // -1 for a scalar; otherwise an existing array element
    ]
}
```

All rows in a batch target the same live object and edit kind. Keys are strictly
ordered and unique. Scalar propset batches order by `(propset_type, field key)`;
array-element propset batches order by `(propset_type, field key, element_index)`;
Creature feat batches order by feat key. Array-element edits support fixed and
unmanaged integer arrays but never resize them. The batch owns its values
through forward application, undo, and redo.

The active object is singular because one workspace surface is active. Patch
application is plural even for one field; the single edit is a batch containing
one row.

## Command To Commit

```text
widget / RmlUi / palette / terminal / Smalls
    -> CommandInvocation + CommandContext
    -> CommandBus
    -> object command reads validated active_object
    -> construct ObjectEditBatch
    -> commit_object_edits
        -> validate complete batch
        -> apply forward
        -> publish mutation epoch
        -> mark active tab dirty
        -> return CommandUndoAction
    -> CommandBus pushes undo action to active workspace tab
    -> workbench observes mutation epoch and rebuilds affected snapshots
```

`CommandContext` carries the active tab ID, live area handle, source, workspace
pointer, focused region, selection strings, and whether undo recording is
enabled. The bus fills an empty active tab ID from `WorkspaceState`.

Only a successful workspace-scoped command may push an undo action. A noop,
rejection, failure, global command, renderer command, or replay with
`record_undo == false` does not add another undo entry.

## Validation And Application

Validation completes before the first write:

- batch is non-empty and has a supported kind;
- target handle resolves to a live object;
- every patch targets the same object;
- keys are strictly ordered and unique;
- propset type, field index, field type, element index, and propset instance are
  valid;
- feat IDs and Creature target type are valid;
- `before` and `after` differ and are in the supported value range; and
- the current live value equals the expected value for the requested direction.

Out-of-range or malformed data is rejected. A current-value mismatch returns
`stale_value`; rollnw client does not overwrite an intervening edit.

Application writes rows in order. If a write fails after a prefix was applied,
that prefix is replayed in the opposite direction before failure is returned.
One successful batch increments the global object-mutation epoch once and
publishes the edited handle.

Area duplicate/delete uses a separate contiguous membership-row batch. Each row
contains the live area handle, child handle, stable vector index, and expected
attached state. Validation rejects stale/non-member handles and duplicate
inputs before mutation. Attach writes run in ascending index order; detach
writes run in descending order so earlier indices remain stable.

The area owns attached children. An undo action owns detached handles and
destroys them when its history is discarded; undo/redo therefore retains one
live object handle instead of reconstructing gameplay state. Duplicate uses the
existing component/propset instance protocol once to instantiate a live copy.
The UI does not read or edit that JSON representation.

A duplicate batch keeps its relative layout by applying one common diagonal
offset. rollnw client tests the four `(+/-1.5 m, +/-1.5 m)` directions in fixed order
and uses the first direction that keeps every clone inside the area bounds. The
command is rejected without creating clones when no direction fits.

A successful structural batch rebuilds renderer rows from the same live `Area`
and transfers root ownership only after the replacement scene succeeds. This
is a cold O(area render-record count) operation per observed structural batch. The
monotonic structural epoch coalesces multiple commands received before a frame
into one rebuild of the final live area state. The session logs record count and
wall time for every rebuild; no benchmark distribution has been established and
no per-frame work is added.

The dominant access pattern is one cold linear validation pass followed by one
linear write pass over the patch vector. The current edit batches are initiated
by user commands, not by a per-frame loop. No performance result is inferred
from that shape.

## Undo And Redo

`commit_object_edits` captures the owned batch in one `CommandUndoAction`:

- undo applies the batch in the inverse direction and moves the action to the
  active tab's redo stack;
- redo applies it forward and moves the action back to undo;
- failed replay leaves the action on its source stack; and
- a new successful edit clears the redo stack.

Replay validates expected live values again. Undo therefore fails instead of
silently overwriting state changed outside the action sequence.

Undo and redo mark the tab dirty and publish a new mutation epoch. Dirty state
is a boolean, not a comparison against a saved revision; undoing back to a
previously saved value still leaves the tab dirty until it is saved again.

## Diagnostics

Mutation results map to the command protocol:

| Mutation result | Command status | Channel |
| --- | --- | --- |
| success | `success` | normally `none` |
| empty batch | `noop` | `none` |
| invalid batch or stale value | `rejected` | `warn` |
| read/write/internal failure | `failed` | `error` |

The terminal, output panel, palette, and RmlUi caller consume the same
`CommandResult`. RmlUi may correct presentation after a rejection, but it does
not create a second resource mutation or diagnostic policy.

## Dirty State And Save

A successful edit, undo, or redo sets the active preview tab dirty. Saving is a
separate command:

```text
workspace.save_tab
    -> document save callback
    -> validate active preview tab and active_object ownership
    -> serialize the live object through component/propset JSON
    -> atomic file replacement
    -> clear dirty only after success
```

The save callback rejects non-preview tabs, inactive preview documents, stale
objects, and paths outside the current project. Failure leaves the dirty flag
set. The atomic replacement writes the complete JSON document; it does not
append mutation patches to the file.

The save/reload tests cover direct scalar and managed feat edits for blueprints,
plus transform and structural membership changes for native CAF areas. They
destroy the source graph, reload through `ObjectManager`/CAF deserialization,
and verify the replacement live objects.

## Save Before Close

`workspace.close_tab` asks `WorkspaceState` for a close decision. A dirty tab
returns a prompt with three explicit actions:

- **Save** executes `workspace.save_and_close_tab`, which saves first and only
  executes close after save succeeds.
- **Discard** executes forced close without saving.
- **Cancel** executes no command.

`workspace.save_and_close_tab` propagates save rejection or failure directly.
There is no path that reports save failure and then closes the document.

## Script Commands

Smalls command handlers may return an undo token and register named undo/redo
handlers. `ScriptCommandHost` wraps that token in a `CommandUndoAction`, so
script-defined workspace commands enter the same per-tab undo admission path as
native commands.

The token is process-local script data. Its meaning and lifetime are owned by
the registering script module; it is not a durable or remote transaction ID.

## Current Limits

- Supported patch payloads are integer propset fields and Creature feat
  assignment. Strings, floats, localized strings, arrays, and compound managed
  operations need an observed editor workflow before extending the protocol.
- Area tabs are read-only for object mutation and cannot save `ARE`/`GIT`/`GIC`.
- `toolset.save_all` remains a stub.
- `ObjectHandle`, Smalls `TypeID`, in-process callbacks, and C++ undo closures
  are not stable cross-process identities or serialization formats.

## Future Remote Boundary

A future server may reuse command names and the validate-then-apply semantics,
but it needs a different explicit protocol: stable document/object identity,
schema identity independent of process-local `TypeID`, typed payload encoding,
base revision, accepted patch batch, and rejection diagnostics. None of that is
implemented or implied by the current C++ structures.
