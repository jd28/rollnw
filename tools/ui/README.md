# Toolset UI Runtime

`tools/ui` contains the data protocols shared by rollnw client, RmlUi, and Smalls.
It does not own resources or a second editor object model. Its inputs are live
object handles, Smalls runtime metadata, list data, and RmlUi events; its
outputs are bounded presentation snapshots, virtualized row ranges, callback
events, and UI-only element commands.

## Boundaries

The subsystem has four distinct jobs:

| Component | Responsibility |
| --- | --- |
| `rml_smalls_expression_binding.*` / `rml_smalls_language_binding.*` | Parse bounded imports and direct-call expressions in scratch storage, encode RmlUi events, execute typed handlers, and apply bounded UI commands. |
| `smalls_rmlui.*` / `rml_smalls_bridge.*` | Publish the validated process-wide `active_object` and load/call toolset Smalls modules. |
| `virtual_list.*` / `virtual_combobox.*` / `ui_v1.*` / `smalls_ui_v1.*` | Compute viewport-bounded list ranges, own reusable combobox state, and expose the first Smalls list state/callback protocol. |
| `smalls_property_tree.*` / `smalls_creature_feats.*` | Build bounded snapshots from live object data for reflected and managed workbench surfaces. |

RmlUi-specific code may change text, values, checked state, classes,
visibility, and focus. It must not directly write resource or gameplay state.
Those mutations go through the command bus, normally through
`core.commands.v1.command_execute`.

## Smalls As UI Behavior

An RmlUi document contains one inline, imports-only Smalls block. Event
attributes are direct call expressions resolved only through that host import
scope. External scripts and document-local function declarations are rejected;
documents therefore do not create runtime Smalls modules.

The handler boundary is:

```text
host imports + direct call + optional RmlUi Event + active_object
    -> bound target index + typed arguments
    -> fn handler(...): void | array!(Command)
    -> validate and apply UI commands to the owner document
```

The event is a value snapshot. Scripts do not receive `Rml::Event*`,
`Rml::Element*`, or `Rml::ElementDocument*`. Returned commands target elements
by ID inside the handler's owner document. See
[RmlUi/Smalls binding](docs/rml_smalls.md) for the exact fields, operations,
limits, lifetime, and errors.

The shared `active_object` is a non-owning `ObjectHandle`. The host validates
the generational handle on every read. A stale, cleared, or absent selection is
encoded as an invalid object; handlers that require an object reject it.

## Mutation Rule

UI handlers may read event data and the active object, then execute a named
command. The current panel uses this flow for Creature plot and feat changes:

```text
checkbox event
    -> core.commands.v1.command_execute
    -> native object command
    -> validated ObjectEditBatch
    -> dirty/undo/mutation epoch
    -> empty UI command list on success
    -> restore checkbox UI state on rejection
```

Element commands are presentation corrections only. They are not transaction
patches and do not participate in resource undo/redo.

## Virtual Lists

`VirtualListController` stores fixed row height, overscan, total row count,
viewport height, scroll offset, selection, and hover. Its output is one
half-open source range plus top and bottom spacer heights:

```text
[start, end) = visible rows + bounded overscan
```

All numeric inputs are clamped. Empty lists or zero-height viewports produce an
empty range. `render_virtual_list` materializes only that range, so RmlUi
document size depends on viewport height and overscan rather than source count.
Its top and bottom spacers explicitly use block layout because RmlUi does not
apply browser default block layout to an unstyled `div`; without it, the scroll
extent contains only the materialized rows.

`core.ui.v1` exposes the first Smalls-facing list state API:

- create/destroy a named list;
- replace its item batch;
- set/get selection by stable key or index; and
- register hover, select, activate, and scroll callback names.

Hover and scroll events are coalesced. Select and activate events preserve
order. The host retains at most 512 queued events and drops the oldest event on
overflow. The host owns copied item and callback strings.

`VirtualComboBox` is the client-driven reusable combobox widget. Opening it
moves one unique-key item batch into the widget, selects by stable integer key,
and requests a DOM update containing only the visible option range plus
overscan. Empty or duplicate-key batches are rejected and leave it closed.
The caller owns the field and popup elements and dispatches the selected key
through the command bus.

The current rollnw client workbench uses `VirtualListController` directly for DOM
row materialization. `VirtualListHost::drain_events` is not yet connected to the
rollnw client main loop, so `core.ui.v1` should be treated as an available state
and callback protocol, not a completed general RmlUi list widget.

## Live Object Snapshots

The property tree and Creature feat view read live objects. They do not parse
serialized JSON and do not retain raw Smalls storage pointers.

- Property rows are rebuilt when the active object, expansion state, schema,
  or mutation epoch changes. Invalid objects and materialization limits produce
  explicit snapshot status and diagnostics.
- Creature feat rows merge the rules feat table with the live sorted feat IDs.
  The result is filtered presentation data; assignment still occurs through
  the shared object command.

Snapshot construction is cold relative to frame rendering. The visible list
slice is the only data-dependent row batch converted to RML each frame.

## Smalls Modules

- `core.rmlui`: event/command value types, UI operation constants, and
  read-only `active_object()`.
- `core.commands`: command specification and result value types.
- `core.commands.v1`: native command registration and execution.
- `core.ui`: list and callback-manifest value types.
- `core.ui.v1`: native list state and callback functions.
- `toolset.ui`: replaceable scripted UI workflow and its callback manifest.

## Failure Behavior

- Invalid or stale active-object handles read as invalid.
- Invalid list IDs, argument shapes, and callback registrations return `false`
  or an empty selection value.
- List geometry and indices are clamped to valid ranges.
- RmlUi/Smalls compile, signature, execution, decode, and target errors are sent
  to RmlUi logging, rollnw logging, and the binding diagnostic list.
- Resource command rejection is returned as `CommandResult`; UI code must not
  bypass it with a direct object write.

## Tests

The focused coverage is in:

- `tests/rollnw_client_rml_smalls_language_binding.cpp`
- `tests/rollnw_client_virtual_list.cpp`
- `tests/rollnw_client_smalls_property_tree.cpp`
- `tests/rollnw_client_smalls_creature_feats.cpp`
- `tests/rollnw_client_object_edits.cpp`
- `tests/rollnw_client_commands.cpp`
