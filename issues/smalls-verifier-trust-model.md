# Smalls Bytecode Verifier Trust-Model Gaps

## Problem

The trust model is: bytecode is only ever produced by `AstCompiler`, so
`BytecodeVerifier` is a second line of defense against *compiler* bugs
before bytecode reaches the VM's unchecked fetch-decode loop. Two gaps break
that model outright (an unchecked write primitive, and a whole class of
modules never verified); several more are defense-in-depth holes the VM
happens to cover at runtime today. Found in the 2026-07 smalls audit;
verified in source.

## Findings — model-breaking

- Call argument count vs. callee arity is never verified.
  `BytecodeVerifier.cpp:230` (CALL/CALLNATIVE/CALLEXT/CALLINTR) checks only
  that the caller's source registers `dest+1..dest+argc` exist; it never
  compares `argc` (`c`) against the callee's `param_count`/`register_count`.
  `copy_args_to_callee` (`VirtualMachine.cpp:553`) then
  `memcpy(dst, src, argc * sizeof(Value))` into `&registers_[current_base_]`,
  where `push_frame` (`:255`) sized the window by `register_count`, not
  `argc`. If `argc` exceeds the callee window (codegen bug, or a hot-reloaded
  callee with a narrower signature than a stale cached caller), the memcpy
  writes past the callee frame into the shared 8192-entry register array —
  an OOB write. `docs/spec.md:1636` claims this invariant is checked; it is
  not. Fix: resolve the callee in the verifier and reject on arity mismatch;
  as a second line, bound-check `argc` against `register_count` in
  `copy_args_to_callee`/`push_frame` before the memcpy.
- Modules reached only via cross-module calls are never verified.
  `get_or_compile_module` (`runtime.cpp:2138`) compiles and registers a
  module's functions but never calls `verify_bytecode_module`. Verification
  runs lazily inside `execute`/`execute_closure` for the *entry* module only;
  the sole path that would drag a called-only module through it is
  `execute_module_init`, gated on `global_count > 0` (`runtime.cpp:2218`).
  `CALLEXT` (`VirtualMachine.cpp:1198`) jumps into `ext->script_module` with
  no `verification_passed` check. So any function-only "library" module
  imported by others executes fully unverified — the modules most likely to
  be shared and hot-reloaded. Fix: verify every module unconditionally at the
  end of `get_or_compile_module` (cached via the existing
  `verification_attempted`/`verification_passed` flags), or at minimum check
  `verification_passed` in `setup_script_call`/`push_frame`.

## Findings — defense-in-depth (VM covers at runtime today)

- Jump targets are range-checked (`0..n`) but not against valid instruction
  starts, so a target can land inside the packed CLOSURE upvalue-descriptor
  words that `BytecodeVerifier.cpp:376` skips as data — the VM would decode a
  descriptor byte as an opcode. Not reachable under current codegen.
- `target == n` is accepted (`:118`/`:128`) and no check requires a function
  to end in `RET`/`RETVOID`. The VM's implicit `ip_ >= ip_end_` path
  (`:968`) is not equivalent to `RETVOID` (caller dest register left stale;
  entry-frame `last_result_` never reset in `reset()`), so a fall-off-end
  would silently return a stale/dangling `Value` with `failed()==false`. The
  compiler always appends a terminal `RETVOID` (`AstCompiler.cpp:500`), so
  not reachable today.
- `CALLINTR` inline bit-op fast path (`VirtualMachine.cpp:1248`) reads
  `reg(dest+1)`/`reg(dest+2)` based only on intrinsic id, ignoring `argc`;
  the verifier has no per-intrinsic arity table. Not reachable under current
  codegen (TypeResolver enforces arity).
- `GETUPVAL`/`SETUPVAL` index and `CALLNATIVE` function index are unchecked
  by the verifier but re-validated at runtime (`VirtualMachine.cpp:1730`,
  `runtime.cpp:2716`) — documentation gap only.

## Fix

Close the two model-breaking gaps first (arity check + verify-all-modules).
For the rest, build a valid-instruction-start bitset during the verifier's
existing linear walk and check jumps against it; require a terminal
`RET`/`RETVOID`; add a per-intrinsic minimum-arity table. These make the
verifier's guarantee independent of "the compiler happens to get it right."

## Verification

Hand-built `CompiledFunction`/`BytecodeModule` unit tests (the verifier
already has `tests/smalls_bytecode_verifier.cpp`) for: argc > callee
register_count; a function-only module called via CALLEXT with an injected
bad register index; a jump into descriptor words; a function missing its
terminator.
