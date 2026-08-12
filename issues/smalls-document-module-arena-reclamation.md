# Smalls Document Module Arena Reclamation

## Problem

RmlUi Smalls documents compile generated modules during document load and evict
those modules during document teardown. `Runtime::evict_module` removes the
script, bytecode, source-map, external-function, and generic-template cache
entries, but the script AST storage is allocated from the runtime compiler
arena. That storage remains at its high-water mark until runtime shutdown.

The rollnw client platform is one long-lived process with one kernel Smalls runtime.
Repeated document reloads can therefore increase compiler-arena use even when
the number of open documents remains constant.

## Observed Data

`BM_rml_smalls_dispatch` compiles one minimal document module containing one
empty event handler. On the 2026-07-14 Release benchmark run, each benchmark
setup reported a 1,368-byte increase in `compiler_arena_used_bytes`. Exact
module eviction returned the compiled-module count to its prior value, but it
cannot return arena storage.

This measurement is specific to that source and compiler configuration. The
distribution for real rollnw client documents and repeated edit/reload sessions has
not been measured.

## Required Data

1. Record compiler-arena use over at least 10,000 reloads of representative
   rollnw client RML/Smalls documents.
2. Record document compile latency and live compiled-module count over the same
   run.
3. Identify which retained compiler allocations require runtime lifetime and
   which require only document-module lifetime.

## Contract

Input is a batch of document module sources loaded, compiled, and evicted in a
long-lived runtime. Output is a bounded amount of compiler storage proportional
to currently retained modules plus an explicit fixed cache budget. Invalid or
failed modules must follow the same reclamation path as successful modules.

## Acceptance

- Repeated load/evict cycles have a measured, explicit memory bound.
- Exact module eviction still leaves unrelated core and toolset modules valid.
- Compile and dispatch benchmarks report before/after latency; no allocator
  change is accepted from memory results alone.
- The solution does not add a second Smalls runtime or a document-specific
  scripting model.
