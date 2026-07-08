# Smalls Compiler Correctness And Codegen

## Problem

Three codegen/allocation defects reachable from ordinary source: a scope bug
that silently produces wrong results, an uncaught exception (plus off-by-one)
on register exhaustion, and an unbounded per-call stack growth for value-type
locals in loops. Plus one VM dispatch bug that makes float `match` take the
wrong branch. Found in the 2026-07 smalls audit; verified in source.

## Findings

- Variable shadowing aliases the outer register. `allocate_local`
  (`AstCompiler.cpp:692`) keys `local_vars_` by raw identifier string,
  function-wide, never erased and with no scope stack — while
  `TypeResolver::visit(BlockStatement*)` (`TypeResolver.cpp:3306`) does real
  `begin_scope`/`end_scope`, so shadowing is legal source. A shadowed inner
  `var x` reuses the outer `x`'s register, so the inner write clobbers the
  outer variable — silent wrong result, no error. Extremely common pattern
  (`i`, `tmp`, `value` reused in nested blocks). Fix: push/pop a
  name→register map per block, matching the resolver's scoping; allocate a
  fresh register for a shadowing declaration.
- Register exhaustion throws uncaught, plus off-by-one.
  `RegisterAllocator::allocate`/`allocate_contiguous`/`high_water_mark`
  (`AstCompiler.cpp:65-135`) `throw std::runtime_error` on overflow; the
  compile call sites (`runtime.cpp:2176`, `:5282`) have no try/catch, so a
  legal function needing ~256 locals terminates the engine instead of a
  diagnostic. `high_water_mark` also throws at `max_reg >= 256`, but index
  255 (the 256th register) is valid per the 8-bit encoding — off by one. Fix:
  use the existing `fail()`/bool-return convention; allow 256 indices before
  failing.
- Value-type locals in loops never free stack. `CallFrame::stack_alloc`
  (`VirtualMachine.cpp:599`) only grows; `stack_free_to` (`:613`) is dead
  code (no caller anywhere) and there is no free/pop opcode
  (`Bytecode.hpp:137`). `visit(VarDecl*)` (`AstCompiler.cpp:1097`) and
  `foreach` value-type bindings (`:1427`) emit `STACK_ALLOC` with no paired
  deallocation, so a value-type/fixed-array local in a loop grows the frame
  stack every iteration for the whole call — O(N) memory, and
  `enumerate_stack_roots` gets slower each iteration too. Fix: emit a
  stack-shrink at block/loop-body exit for block-scoped value-type locals
  (new opcode or wire up `stack_free_to`), mirroring register `free()`.
- Float `match` takes the wrong branch. `op_test_and_skip` float fast path
  (`VirtualMachine.cpp:3956`) does `frames_.back().pc++`, but the primary
  computed-goto build dispatches off `ip_` (`frame.pc` is debug-only per the
  invariant at `VirtualMachine.hpp:160`), so the skip is a no-op. The int and
  string paths correctly call `skip_next_instruction()` (`:3928`,`:3986`).
  Compiler `match`/`switch` codegen (`AstCompiler.cpp:1619`) emits
  `ISEQ` + a to-be-skipped `JMP`, so `match x { 1.5 => a, _ => b }` with
  `x == 1.5` silently runs the default branch. Fix: call
  `skip_next_instruction()` at `:3956`.

## Fix Policy

The scope, register-exhaustion, and float-match bugs are correctness (wrong
results / crash on legal input); the stack-growth is an unbounded hot-path
allocation. Per AGENTS.md, errors on legal input must fail loudly through the
established channel, and per-iteration work must not accumulate unbounded.

## Cross-cutting Note

The float-match bug is an instance of the `frame.pc`-vs-`ip_` footgun: any
control-flow code that writes `frame.pc` directly is dead on the primary
build. New control-flow code should route through `skip_next_instruction()`
and never touch `frame.pc`. The computed-goto and switch dispatch bodies are
also duplicated verbatim, so verify fixes land in both.

## Verification

Script tests: nested-block shadowing returns the lexically-correct value;
`match` over float cases hits the right arm; a ~256-local function compiles
or fails cleanly (no crash); a loop with a value-type local shows bounded
frame-stack growth.
