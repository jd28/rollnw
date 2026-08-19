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

rollnw client loads and compiles `core.rmlui` before initializing the binding.
Provider modules are loaded and compiled when the host import scope and its
listener targets are resolved. Initialization registers:

- a document instancer for RmlUi `body` documents; and
- an event-listener instancer for direct Smalls calls.

Each document may provide exactly one inline Smalls block, and that block may
contain imports only. The host block and event expressions are parsed in a
nested thread-local scratch scope. All names and bound string values that
survive parsing are copied into document- or listener-owned storage; AST nodes,
tokens, and source views never escape scratch lifetime. No document module is
created and no document source enters the runtime compiler arena.

Resolved function pointers are owned by the current Smalls runtime generation.
A listener stores an index into its document's target table, while the target
table stores the provider module, function name, and current compiled pointers.
After kernel service replacement, imports and targets are resolved again before
dispatch. No AST declaration pointer is retained; export ABI metadata remains
valid after provider AST compaction.

External scripts, a second inline block, and declarations in the inline block
are rejected. These are contract errors, not fallback paths.

## Handler Contract

An event attribute contains one direct call expression. Its target must be a
function imported selectively by the host block or reached through an imported
module alias:

```smalls
from toolset.item_editor import { apply_color, details_refresh };
```

```rml
<button onclick="apply_color(42)">...</button>
<div onrefresh="details_refresh()">...</div>
```

Arguments may be `event`, scalar literals, signed numeric literals, or imported
primitive/newtype constants. Calls may have at most 16 arguments. Every
declared parameter must be supplied, including parameters with defaults; no
implicit default application occurs at this boundary. Generic targets are
rejected. Arguments must exactly match the exported function ABI after alias
unwrapping. Numeric literals are not widened at this boundary: an `int`
parameter accepts `1`, while a `float` parameter requires `1.0`.

A valid target returns either:

```smalls
fn handler(...)
fn handler(...): array!(core.rmlui.Command)
```

The binding interns targets by provider module and function. The listener owns
only a target index and its bound argument vector. Thus 176 calls such as
`apply_color(0)` through `apply_color(175)` retain one target and 176 integer
arguments, rather than 176 generated functions.

RmlUi may instantiate attribute listeners lazily. Binding is attempted when the
listener is instantiated and is always retried on first dispatch if the host
scope was not yet available. This fallback is the correctness path; a click is
never dropped to satisfy an eager-binding schedule.

Dispatch currently enters through one RmlUi event at a time. Internally the
binding processes a span of dispatch rows, so event encoding and execution have
one plural path.

## Binding Data Protocol And Bounds

The binding transforms two input batches on the UI thread:

```text
one host import block
    -> owned selective-symbol and module-alias tables

event attribute expressions
    -> document target table
    + listener rows { target index, bound argument rows, diagnostic identity }
```

The RML document owns the import tables and target table until document
destruction. Each RmlUi listener owns its bound argument rows for the same
lifetime. Compiled module and function pointers remain owned by the Smalls
runtime and are valid only for the recorded kernel-service generation. Parsed
ASTs, tokens, and source views remain in nested thread-local scratch storage and
are discarded before either transform returns.

Every input boundary rejects data above its limit:

| Input | Limit | Out-of-range behavior |
| --- | ---: | --- |
| Host import source | 16 KiB | Reject the host scope. |
| Host imports | 64 | Reject the host scope. |
| Imported symbols and aliases | 256 | Reject the host scope. |
| One event expression | 1 KiB | Reject that listener. |
| Expression AST | 64 nodes, depth 16 | Reject that listener. |
| Bound call arguments | 16 | Reject that listener. |
| Interned targets per document | 256 | Reject additional listeners. |
| Returned UI commands | 256 | Reject the complete returned batch. |
| One `set_rml` payload | 1 MiB | Reject the complete returned batch. |

The common dispatch path is one indexed target-table read followed by a linear
materialization of at most 16 arguments. Literal integers, floats, and booleans
require no ScriptHeap allocation. Event and string values are rooted before a
later allocation can collect. The retained compiled pointers avoid a repeated
name lookup on every click; indices cannot replace those pointers at the
runtime execution boundary because `execute_compiled` consumes the
runtime-owned compiled module and function. The generation check makes their
lifetime explicit.

This is a human-input latency path, not a throughput loop. Dispatch latency is
reported by the benchmark without an arbitrary budget. The memory acceptance
gate is exact: after provider modules are warmed, repeated document binding and
dispatch must add zero bytes and zero entries to every measured runtime compiler
counter.

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

Dispatch does not require a valid active object. When there is no active
selection, `active_object` is invalid and the handler decides whether that is
an error. Actions such as closing a selector can therefore run without a live
resource.

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
operation. UI scripts use `core.commands.v1.command_execute` for explicit
editor operations, such as Item property changes. Dynamically generated
Details editors carry an editor kind, propset, field, current integer value,
and inclusive minimum/maximum values in the SmallS row protocol. Boolean rows
use the integer range `0..1`; ranged integer rows state their domain explicitly.
The client treats the row index as an opaque token, rebuilds the bounded row
batch on commit, and rejects the command unless the live row still names the
same editor with the expected value and the requested value is in range. Input
errors and out-of-range values are rejected rather than clamped. Integer rows
render a text input for direct entry and bounded single-step controls; both
commit through the same validated command.

```smalls
from core.commands.v1 import { command_execute };

fn remove_item_property(index: int) {
    command_execute("object.item.remove_property", { f"{index}" });
}
```

Command handlers validate and mutate the live object, mark the document dirty,
and contribute undo data. Direct propset writes remain unavailable to UI
scripts.

## Diagnostics And Failure

The binding reports errors to all three current sinks:

- RmlUi error logging;
- rollnw logging with the `[rml-smalls]` category; and
- `RmlSmallsLanguageBinding::diagnostics()`.

The binding retains at most 256 diagnostic records. Further failures still
reach both logging sinks, but are not retained; the cumulative, saturating
`RmlSmallsBindingStats::suppressed_diagnostic_count` records their count.
`clear_diagnostics()` removes the retained messages without resetting the
lifetime statistics.

Failure stages are explicit:

| Stage | Result |
| --- | --- |
| Host import parsing/resolution | Scope is invalid; listeners do not execute until a later runtime generation successfully rebinds it. |
| Expression binding | Listener is invalid for that runtime generation; diagnostics name the document or template source, element ID, and expression. |
| Event encoding | Handler is not executed. |
| Smalls execution | No commands are applied. |
| Command decoding | No commands are applied. |
| Command application | Application stops at the failing row; earlier UI rows remain applied. |
| C++ exception at an RmlUi entry point | The exception is converted to a diagnostic; no exception crosses the RmlUi callback boundary. |

Imported templates do not contribute or union import scopes. A template handler
resolves only against the including host document's imports. Template roots use
`data-smalls-source` so a missing host import names the template source rather
than incorrectly blaming the host document.

## Deliberately Not Exposed

- Raw `Rml::Event*`, `Rml::Element*`, or `Rml::ElementDocument*` values.
- General DOM traversal or arbitrary element creation/deletion.
- Direct attribute or style mutation. The only structural command replaces one
  identified subtree with size-bounded RML from a compiled handler.
- Resource mutation or save APIs.
- A network or cross-process UI protocol.
