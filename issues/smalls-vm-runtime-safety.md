# Smalls VM Runtime Memory/Arithmetic Safety

## Problem

Several VM opcode handlers operate on script-controlled `int32_t` values or
hold raw heap references across an allocation without the preconditions the
code silently assumes. Each is reachable from ordinary script source. Found
in the 2026-07 smalls audit; the top items verified in source.

## Findings

- `VirtualMachine.cpp:1682` (CLOSURE; duplicated at `:2954` on the switch
  dispatch path): `alloc_closure` returns `closure_ptr` into a C++ local
  only — it is not written to a register or frame slot until `:1721`. The
  intervening upvalue loop calls `get_or_create_upvalue`
  (`:1707` -> `runtime.cpp:4564` `alloc_upvalue`), a second heap allocation
  that can trigger `try_emergency_collect_minor` (`ScriptHeap.cpp:79`).
  `mark_roots` (`GarbageCollector.cpp:369`) does not see VM C++ locals, so
  the young, unrooted closure can be swept and freed; the loop then writes
  into `closure->upvalues` (use-after-free) and stores a dangling handle.
  Trigger: a session that creates closures-with-captures past the
  ~512K-alloc emergency threshold. Fix: write the closure to `reg(dest_reg)`
  (rooting it) before allocating any upvalue, or push it as a temporary GC
  root for the duration of the loop.
- `VirtualMachine.cpp:3634` (`op_arithmetic` int DIV/MOD): the zero-divisor
  case is checked and `fail()`s, but `INT32_MIN / -1` and `INT32_MIN % -1`
  are not — on x86 `idiv` raises `#DE`/SIGFPE, crashing the process. Trigger:
  `-2147483648 / -1`. Fix: alongside the zero check, reject (or define the
  result of) `rv == -1 && lv == INT32_MIN`.
- `VirtualMachine.cpp:4501` (`StringSubstr`): `start` is clamped to
  `[0, str_len]` but `len` only to `>= 0`; `start + len` overflows `int32_t`,
  so the `> str_len` clamp is bypassed and `alloc_string_view` is called with
  an attacker-chosen huge length — out-of-bounds read of the string backing.
  Trigger: `"abc".substr(0, 2147483647)`. Fix: clamp by subtraction —
  `if (len > int32_t(str_len) - start) len = str_len - start;` (safe since
  `start <= str_len`), never add.
- `VirtualMachine.cpp:1272` (CALLINTR inline bit-op fast path) and `:4137`
  (`call_intrinsic` slow path): `lhs << rhs` / `lhs >> rhs` with `rhs` taken
  directly from a register — shifting by `< 0` or `>= 32` is UB. Trigger:
  `bit_shl(1, 40)`. Fix: pick one explicit policy — mask `rhs & 31`, or
  `fail()` when out of `[0,32)` — per AGENTS.md "out-of-range is always
  explicit."

## Fix Policy

All four are the same AGENTS.md rule: an opcode's preconditions on a
script-supplied value must be enforced explicitly (clamp/reject/root), not
assumed. The CLOSURE rooting fix must be applied to both dispatch bodies.

## Verification

Add script-level regression tests: substr with overflowing start+len,
`INT_MIN/-1`, out-of-range shift, and a closure-churn stress test run under
ASan with the emergency-GC threshold lowered so the rooting window is hit
deterministically.
