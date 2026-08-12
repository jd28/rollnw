# RmlUi And Smalls Binding

This document describes the implemented `RmlSmallsLanguageBinding`. It is a
typed event-and-command boundary, not a Smalls clone of the RmlUi DOM.

## Application And Presentation Boundary

Smalls is the application language for RmlUi documents. It owns tool workflow,
view state, data projection, and event behavior. It does not own RmlUi elements
and does not manipulate a general DOM API.

Native ABI, native component, profile propset, and toolset projection ownership
is defined in the
[Propset Architecture](../../../lib/nw/smalls/docs/propset-architecture.md).
This document applies that ownership rule to the UI boundary.

The boundary is:

| Layer | Owns |
| --- | --- |
| C++ core and selected profile | Native data, mechanical invariants, resource access, renderer integration, and typed mutation operations. |
| Generic C++ UI host | Smalls execution, RmlUi integration, input delivery, fixed-height virtual ranges, native textures, and transaction transport. |
| Smalls profile modules | Game-specific schema, interpretation, validation, and policy. |
| Smalls toolset modules | Editor state, filtering, sorting, labels, row projection, event handling, and command composition. |
| RML | Document, tab, panel, control, and row structure plus event wiring. |
| RCSS | Layout and presentation. |

The generic C++ UI host must not:

- enumerate editor surfaces such as `Spells`, `Feats`, `Appearance`, or
  `Inventory`;
- generate editor-specific tab, panel, control, or row markup;
- retain editor-specific presentation state or mirrored row types;
- route input by editor-specific surface identifiers; or
- interpret profile schemas, integer IDs, labels, or policy.

RML is the workbench composition mechanism. Adding, removing, reordering, or
replacing a surface is an RML, RCSS, and optionally Smalls change. It is not a
C++ registration operation. Smalls toolset modules may call existing native
capabilities, but they do not require a native function named after the
surface.

A C++ change is required only when the workflow needs a native capability that
does not exist: for example, a new resource decoder, renderer operation,
ownership mutation, or engine invariant. A new presentation over existing
object data and native operations does not justify a C++ editor API.

The acceptance test is deliberately stronger than restyling:

> A selected toolset package can remove the creature `Spells` surface and
> replace it with a differently shaped `Talents` workflow using only Smalls,
> RML, and RCSS, while using the unchanged client executable.

That test passes when the replacement can read the published `active_object`,
project its rows, mutate supported object data through the normal transaction
path, participate in dirty tracking and undo/redo, and refresh the preview
without a C++ tab enum, event branch, row mirror, or markup function.

An editor-specific C++ branch is evidence that either the generic protocol is
missing a concrete capability or presentation policy crossed the boundary.
Record the input, output, frequency, and ownership of the missing capability
before extending the native API.

## View Data And Virtualization

Toolset views publish typed Smalls values through the RmlUi data-model binding.
RML consumes those values through data bindings and `data-for`; RCSS styles the
result. Scripts generate replacement RML only for bounded structures whose
shape cannot be represented by the current data-model binding.

Variable-length fixed-height views use one generic virtual-array protocol:

```text
Smalls source rows
    -> generic native visible range [start, end)
    -> RmlUi data-for over the visible slice
    -> RML-owned row structure
```

Smalls owns the complete filtered and sorted row batch until it is replaced.
The native host owns scroll geometry, clamps the visible range, and exposes
only the viewport plus bounded overscan to RmlUi. Stable scalar keys identify
selection; source indices do not identify objects or resources.

The common path performs no Smalls call per frame, per row, or per scroll
position. Smalls runs on input or source invalidation, and publishes rows as
one batch. Missing or stale active objects produce an empty view and no
mutation. Duplicate keys reject the complete row publication. Invalid source
ranges are clamped by the virtual host; invalid object values are rejected by
the owning profile or native operation rather than clamped by the UI.

Variable-height views are a different data problem and are not covered by this
protocol.

## Initialization And Lifetime

rollnw client loads and compiles the `core.rmlui` Smalls module before
initializing the binding. Initialization registers:

- a document instancer for RmlUi `body` documents; and
- an event-listener instancer for named Smalls handlers.

Each successfully compiled inline or external script block is recorded as a
unique module owned by its document. Handler identities retain pointers to
runtime-owned compiled modules and functions only for that document lifetime.
Destroying the document evicts its recorded script modules after its listeners
can no longer dispatch.

External scripts are limited to 1 MiB. A missing file interface, open/read
failure, oversized source, compile diagnostic, or bytecode compile failure is
reported and that block is unavailable.

## Handler Contract

An event attribute names a Smalls function. The binding resolves the most
recent document block exporting that name. A valid handler has exactly this
shape:

```smalls
fn handler(event: core.rmlui.Event): void
```

or:

```smalls
fn handler(event: core.rmlui.Event): array!(core.rmlui.Command)
```

Handler names must be non-empty identifiers containing ASCII letters, digits,
or `_`, and may not start with a digit. A missing handler or a mismatched
parameter/return type is a binding error.

Dispatch currently enters through one RmlUi event at a time. Internally the
binding processes a span of dispatch rows, so event encoding and execution have
one plural path.

## Event Value

`core.rmlui.Event` is copied before the handler executes:

| Field | Meaning |
| --- | --- |
| `event_type: string` | RmlUi event type, such as `click` or `change`. |
| `element_id: string` | Source element ID. May be empty when the RML element has no ID. |
| `document_id: string` | Owner document ID, or its source URL when the ID is empty. |
| `text: string` | Source element inner RML at dispatch time. |
| `value: string` | Form-control value, or empty for a non-control element. |
| `checked: bool` | Whether an input currently has the `checked` attribute. |
| `active_object: object` | Current validated rollnw client selection. |

Dispatch requires a valid active object. When there is no active selection,
the binding reports `no active object` and does not call the handler.

The module also exposes `active_object(): object` for terminal or ordinary
Smalls calls that need to query the same selection outside an RmlUi event.

## UI Command Value

A handler may return no value or at most 256 `core.rmlui.Command` rows. Each
row contains:

```smalls
type Command {
    operation: int;
    element_id: string;
    value: string;
    state: bool;
};
```

Commands resolve `element_id` only inside the handler's owner document.
An empty ID or missing target is an error.

| Operation | Constant | Fields used | Behavior |
| --- | --- | --- | --- |
| Set text | `command_set_text` | `element_id`, `value` | HTML-encodes `value`, then replaces inner RML. |
| Set value | `command_set_value` | `element_id`, `value` | Sets a form-control value; rejects other elements. |
| Set checked | `command_set_checked` | `element_id`, `state` | Adds or removes the `checked` attribute; rejects non-input elements. |
| Set class | `command_set_class` | `element_id`, `value`, `state` | Adds/removes one non-empty class name. |
| Set visible | `command_set_visible` | `element_id`, `state` | Sets `visibility` to `visible` or `hidden`. |
| Focus | `command_focus` | `element_id`, `state` | Focuses when true and blurs when false. |
| Set RML | `command_set_rml` | `element_id`, `value` | Replaces one bounded widget subtree with trusted generated RML. Payloads over 1 MiB are rejected. |

Rows are decoded completely before application. Application is ordered and is
not atomic: if a later command has an invalid target or target type, earlier UI
commands in the same result remain applied. These commands alter presentation
only and are not resource transaction patches.

`command_set_rml` is intended for bounded, compiled widget scripts such as the
Item appearance editor. It does not encode `value`; the script must RML-encode
all live labels and other data before interpolation. Static structure remains in
RML templates and RCSS; scripts generate only the rows or selector subtree whose
shape depends on live policy data.

The client may request a cold UI rebuild by calling
`RmlSmallsLanguageBinding::refresh_elements`. Each target must have both the
`smalls_refresh` class and a non-empty, document-unique ID. Targets are captured
by ID and re-resolved before each synthetic `refresh` event, so an earlier
handler may replace its own subtree without leaving stale element pointers.

## Resource Mutation

The binding intentionally exposes no direct object-write or resource-save
operation. UI scripts use `core.commands.v1.command_execute`:

```smalls
from core.commands.v1 import { command_execute };
from core.rmlui import { Event, Command, command_set_checked };

fn set_plot(event: Event): array!(Command) {
    var desired = "0";
    if (event.checked) {
        desired = "1";
    }
    var result = command_execute("object.creature.set_plot", { desired });
    if (result.status == 0 || result.status == 4) {
        return {};
    }
    return {{
        operation = command_set_checked,
        element_id = event.element_id,
        value = "",
        state = !event.checked
    }};
}
```

The command handler validates and mutates the live object, marks the document
dirty, and contributes undo data. The returned UI command only restores the
checkbox when the resource command rejects the requested change.

## Diagnostics And Failure

The binding reports errors to all three current sinks:

- RmlUi error logging;
- rollnw logging with the `[rml-smalls]` category; and
- `RmlSmallsLanguageBinding::diagnostics()`.

Failure stages are explicit:

| Stage | Result |
| --- | --- |
| Source load/compile | Block is not registered; diagnostics include source path and adjusted line. |
| Handler resolution | Listener retains an invalid handler and ignores later events after reporting once. |
| Event encoding | Handler is not executed. |
| Smalls execution | No commands are applied. |
| Command decoding | No commands are applied. |
| Command application | Application stops at the failing row; earlier UI rows remain applied. |

## Deliberately Not Exposed

- Raw `Rml::Event*`, `Rml::Element*`, or `Rml::ElementDocument*` values.
- General DOM traversal or arbitrary element creation/deletion.
- Direct attribute or style mutation. The only structural command replaces one
  identified subtree with size-bounded RML from a compiled handler.
- Resource mutation or save APIs.
- A network or cross-process UI protocol.
