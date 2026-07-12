# Smalls Native-Boundary Input Hardening

## Problem

Every native argument is script-controlled and must be treated as hostile.
The binding layer is mostly disciplined (handles funnel through
`get_object_base`/`as_creature`/`as_item` null-checks), but a few sites
accept an index/size without the bounds check their siblings apply. One is
an out-of-bounds write. Found in the 2026-07 smalls audit; verified in
source.

## Findings

- Resolved: equipment-slot helpers moved to `Equips.cpp` and now reject
  `slot < 0 || slot >= 18` before indexing the fixed 18-slot row. The
  `core.creature` natives still accept script-controlled `int32_t slot`, but
  the native boundary now returns `false`/invalid object instead of indexing
  out of range.
- Fixed-array size overflow: `AstResolver.cpp:657` validates only
  `array_size > 0`; `Type::size = elem_type->size * array_size`
  (`AstResolver.cpp:667`) and `alloc_array`'s `elements_size = count *
  elem_size` (`runtime.cpp:3532`) are `uint32_t` and wrap. For
  `array!(BigStruct, N)` with `N * sizeof(BigStruct) > UINT32_MAX`, the
  allocation is undersized but `initialize_zero_defaults` (`runtime.cpp:3018`)
  runs the init loop for the full un-wrapped `N` (`runtime.cpp:3545`) —
  heap buffer overflow, triggerable by declaring a default-constructed local
  or struct field of that type. Fix: reject at `AstResolver.cpp:657` when
  `uint64_t(array_size) * elem_type->size > UINT32_MAX` (or a smaller sane
  cap), and re-check for overflow in `alloc_array` (compute in `uint64_t`,
  validate before truncating).
- `String.pad_left`/`pad_right` (`native/core_string.cpp:85`,`:116`):
  script-controlled `width` with no upper clamp; `result.reserve(width)`
  attempts ~2 GiB allocations. The VM's native try/catch keeps this from
  crashing the host, but it is an uncapped memory-exhaustion vector. Fix:
  clamp `width` to a sane maximum before reserving.

## Fix Policy

AGENTS.md: explicit out-of-range behavior at every input boundary. The
equip-slot and array-size cases are memory-safety; the pad-width case is
resource-bounding. All three: reject or clamp deliberately.

## Cross-cutting Note

Native validation failures uniformly return a default `Value{}` (invalid
type) rather than calling `rt->fail()`; the VM stores that and continues, so
rejections degrade to silent empty results. This is consistent but
undocumented — worth a deliberate policy decision (documented silent-default
vs. explicit fail) rather than leaving it emergent.

## Verification

Script tests: `array!(BigStruct, N)` with overflowing N under ASan; `pad_left`
with huge width returns bounded/rejected.
