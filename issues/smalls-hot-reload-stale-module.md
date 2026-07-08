# Smalls Hot-Reload Stale External-Module Pointer

## Problem

Evicting a user module frees its bytecode while other already-resolved caller
modules retain pointers to it, and those callers are not forced to
re-resolve — a latent use-after-free on the hot-reload path. Found in the
2026-07 smalls audit (adjacent to the compiler/verifier scope); verified in
source.

## Findings

- `Runtime::evict_user_modules` (`runtime.cpp:2032`) frees an evicted
  module's `BytecodeModule`/`CompiledFunction` objects and removes its
  name→index entry from `external_function_names_`, but leaves the
  corresponding `ExternalFunction` entries (with now-dangling
  `script_module`) in place in `external_functions_`.
- `execute` re-resolves external refs only when `external_indices[0] ==
  UINT32_MAX` (`VirtualMachine.cpp:662`). A caller module that was already
  resolved and is not itself re-touched by the reload keeps its resolved
  `external_indices`, so a later `CALLEXT` (`:1198`) jumps into the freed
  `ext->script_module` — use-after-free.

## Fix

On eviction, invalidate every `ExternalFunction`/`external_indices` entry
that referenced the evicted module (mark unresolved, i.e. `UINT32_MAX`, so
callers re-resolve on next use), or refuse to free a module while any
resolved external ref points at it. Decide one contract and enforce it at the
eviction boundary.

## Open Questions

- Is there an existing module-generation/epoch that callers could check
  before trusting a resolved external index? If so, bump it on eviction and
  re-resolve on mismatch. If not, this parallels the render asset-cache
  lifecycle issue — the general "who invalidates captured references on
  reload" protocol is missing here too.

## Verification

Test: module A calls B via CALLEXT (resolve it), evict/reload B without
re-touching A, then call through A again — must re-resolve or fail cleanly,
not dereference freed memory (ASan).
