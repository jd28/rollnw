# Smalls GC Tracer Misses Newtype/Alias-Wrapped Heap Types

## Problem

`GarbageCollector::trace_object` dispatches on a heap object's `type_kind`
without first unwrapping `TK_newtype`/`TK_alias`, while every other
type-introspection routine in the codebase does unwrap. A value carrying a
newtype `TypeID` over a heap-bearing type can be boxed onto the heap, after
which its heap fields are never traced — a collect-live-object bug of the
same class as the previously fixed `TK_struct` miss. Found in the 2026-07
smalls audit; verified in source.

## Findings

- `GarbageCollector.cpp:919` (`trace_object`): `switch (type->type_kind)`
  with `TK_newtype`/`TK_alias` falling to `default: break`. Sibling routines
  all unwrap first: `scan_value_heap_refs` (`runtime.hpp:1992`),
  `type_may_hold_heap_refs` (`VirtualMachine.cpp:39`),
  `initialize_zero_defaults` (`runtime.cpp:2989`), `is_heap_type`
  (`types.cpp:723`).
- Reachability: `CAST` (`VirtualMachine.cpp:2013`) sets `val.type_id` to a
  newtype id while leaving the bytes unchanged (newtypes are runtime-
  identical). Boxing that value into a module global (`SETGLOBAL`,
  `:1792`/`:3064`) or returning it across the entry frame (`RET`, `:1939`)
  calls `alloc_struct(v.type_id)` / `heap_.allocate(..., val.type_id)`, so
  `header->type_id` is the newtype. `alloc_struct`/`heap_.allocate`
  (`runtime.cpp:2830`) do no type-kind validation, and nothing in the
  parser/`TypeResolver`/`TypeTable::define` (`types.cpp:256`,
  `Parser.cpp:1670`, `TypeResolver.cpp:2347`) forbids a newtype over a
  heap-containing struct — it is legal today, just unexercised (bundled
  scripts only wrap `int`). Result: the wrapped type's `string`/array/etc.
  field is never traced and can be freed under a still-reachable global.

## Fix

Unwrap at the top of `trace_object`, exactly as `scan_value_heap_refs`
does, then run the existing switch on the unwrapped type:

    const Type* type = runtime_->get_type(header->type_id);
    while (type && (type->type_kind == TK_newtype || type->type_kind == TK_alias)) {
        if (!type->type_params[0].is<TypeID>()) break;
        type = runtime_->get_type(type->type_params[0].as<TypeID>());
    }

The `TK_sum`/`TK_fixed_array` branches already delegate to
`scan_value_heap_refs` (which re-unwraps), so no further change once the
unwrapped type reaches the switch.

## Prevent Recurrence

This is the recurring class: any `type_kind` switch that doesn't unwrap
first is a latent GC-soundness bug. Consider a single required
`unwrap_newtype_alias(type)` helper that all such switches call, so the
convention is enforceable rather than remembered. Also factor the duplicated
SETGLOBAL boxing logic (`:1792` and `:3064`) into one helper so the fix and
any future GC-adjacent change land once, not twice.

## Verification

Script test: a `[[value_type]]`/heap-bearing struct wrapped by a newtype,
stored to a global or returned, then force a major GC and dereference —
under ASan, must not read freed memory.
