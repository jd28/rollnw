# Smalls Document Module Arena Reclamation

## Status

The RmlUi path is mitigated as of 2026-08-18. RML documents no longer create
Smalls runtime modules: host imports and listener expressions are parsed in
nested thread-local scratch storage and direct calls bind to warmed provider
modules. General runtime module-arena reclamation remains unresolved for other
callers that dynamically compile and evict modules.

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

### Reload slope, 2026-08-18 Release

`BM_rml_smalls_document_reload` and `BM_rml_smalls_document_reload_soak` load,
show, and close one document per cycle. Imported modules are warmed and eight
reloads are performed before the counters are sampled, so first-load module
compilation is excluded. Four document shapes:

| Shape | Document script | Listeners | Bytes/reload | 10,000 reloads |
| --- | --- | --- | --- | --- |
| `minimal` | one empty handler | 1 | 1,368 | 13.68 MB |
| `import_only` | two imports, no functions | 0 | 528 | 5.28 MB |
| `panel_like` | 12 forwarding functions | 12 | 38,384 | 383.84 MB |
| `palette` | one handler | 176 | 3,624 | 36.24 MB |

`minimal` reproduces the 2026-07-14 figure exactly, confirming the harness
measures the same quantity.

Findings:

1. The slope is constant at 100, 1,000, and 10,000 reloads for every shape. There
   is no plateau; growth is linear in reload count for the life of the process.
2. Eviction works for everything except the arena. `module_count_delta`,
   `compiled_module_delta`, `compiled_function_delta`, `export_count_delta`, and
   `source_map_entry_delta` are all zero across 10,000 reloads. The arena is the
   only leaking counter.
3. Cost is driven by the document script, not by listener count. `panel_like`
   costs 10.6x `palette` while instancing 12 listeners against 176. Listener
   registration contributes no measurable arena growth.
4. The real panel shape is the expensive one. Twelve forwarding functions retain
   38 KB per reload -- 73x the import-only document scope.

`import_only` is the floor reachable by binding event attributes directly to
imported functions instead of generating a document module. It is not zero: an
import-only script is still parsed into the runtime compiler arena, so reaching
zero additionally requires parsing document scope in temporary storage.

### Direct binding result, 2026-08-18 Release

The replacement benchmark parses the host import block and every listener
expression in nested thread-local scratch, dispatches every listener, and closes
the document. Provider modules are compiled before the baseline. At 10,000
reloads all of these runtime deltas are exactly zero for every shape:

- compiler-arena bytes;
- module, compiled-module, compiled-function, export, and source-map entries;
- instantiated generic functions and instantiated generic types.

The retained listener data confirms that literal arguments are separated from
the interned call target:

| Shape | Bound listeners/reload | Bound arguments/reload | Interned targets/reload |
| --- | ---: | ---: | ---: |
| `minimal` | 1 | 1 | 1 |
| `import_only` | 0 | 0 | 0 |
| `panel_like` | 12 | 12 | 12 |
| `palette` | 176 | 176 | 1 |

The 10,000-cycle wall times on the measured Release build were 63.0 ms,
46.2 ms, 508.5 ms, and 4.212 s respectively. Steady-state direct dispatch for
batches of 1, 16, and 256 events measured 2.26 us, 38.6 us, and 547 us;
document load measured 26-70 us and first dispatch 6-14 us in those runs.
These are measurements, not budgets; the exact zero-growth result is the
acceptance gate. The old tables above remain as the generated-module baseline.

Reproduce with:

    rollnw_benchmark --benchmark_filter="BM_rml_smalls_document_reload"

## Required Data

1. ~~Record compiler-arena use over at least 10,000 reloads of representative
   rollnw client RML/Smalls documents.~~ Done; see above.
2. ~~Record document compile latency and live compiled-module count over the same
   run.~~ Done; see above. Generated-module reload wall time was 32-182 us
   depending on shape. Direct-binding wall times are recorded above.
3. Identify which retained compiler allocations require runtime lifetime and
   which require only document-module lifetime.

## Contract

For the unresolved general case, input is a batch of module sources loaded,
compiled, and evicted in a long-lived runtime. Output is compiler storage
bounded by currently retained modules plus an explicit fixed cache budget.
Invalid or failed modules must follow the same reclamation path as successful
modules.

## Acceptance

- Repeated load/evict cycles have a measured, explicit memory bound.
- Exact module eviction still leaves unrelated core and toolset modules valid.
- Compile and dispatch benchmarks report before/after latency; no allocator
  change is accepted from memory results alone.
- The solution does not add a second Smalls runtime or a document-specific
  scripting model.

The RmlUi caller now satisfies the first two conditions without an allocator
change or a second runtime. The remaining acceptance work applies only to other
dynamic-module callers.
