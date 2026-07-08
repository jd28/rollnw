# Smalls Native-Boundary Input Hardening

## Problem

Every native argument is script-controlled and must be treated as hostile.
The binding layer is mostly disciplined (handles funnel through
`get_object_base`/`as_creature`/`as_item` null-checks), but a few sites
accept an index/size without the bounds check their siblings apply. One is
an out-of-bounds write. Found in the 2026-07 smalls audit; verified in
source.

## Findings

- Equipment-slot family: `scriptapi.cpp:218` (`get_equipped_item`),
  `:229` (`unequip_item_in_slot`), plus `equip_item_in_slot`/`can_equip_item`,
  bound in `native/core_creature.cpp:99-106` as `(..., int32_t slot)` with
  `static_cast<nw::EquipIndex>(slot)` and no range check. `EquipIndex` is a
  script-constructible `newtype(int)` (`scripts/core/types.smalls:19`), so a
  script passes any int. `equipment.equips` is `std::array<EquipItem,18>`
  (`Equips.hpp:165`); `equips[size_t(slot)]` with an out-of-range or negative
  (→ huge) slot reads far out of bounds, and `unequip_item_in_slot`
  additionally writes `it = EquipItem{}` there — an OOB write primitive.
  Sibling natives in the same file (`get_class_at`, etc.) bounds-check
  identical-shaped index params, so this is a dropped check, not design.
  Fix: reject `slot < 0 || slot >= 18` at the top of all four functions in
  `scriptapi.cpp`, returning `nullptr`/`false`.
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

Script tests: `get_equipped_item(cre, EquipIndex(-1))` and slot ≥ 18 under
ASan; `array!(BigStruct, N)` with overflowing N under ASan; `pad_left` with
huge width returns bounded/rejected.
