# Smalls LSP Feature Roadmap

## Problem

`smalls-lsp` advertises seven capabilities: diagnostics, completion, hover,
definition, signature help, parameter inlay hints, and full semantic tokens.
Everything an editor expects beyond that list is absent, and several of the
features that do exist return the minimum payload the protocol permits. The
server is one 1,391-line translation unit with no request queue, no
cancellation, and no workspace index.

Two structural facts constrain every feature below and are the reason the
work is ordered rather than parallel.

**AST traversal is opt-in and silently truncating.** `SemanticTokenVisitor`
derives from `NullVisitor`, whose base implementations are empty bodies. Any
node kind the visitor does not override terminates the walk at that node
instead of descending. Every future AST-walking provider inherits this hazard.

**The module cache is discarded on every edit.** `publish_diagnostics` calls
`Runtime::evict_user_modules`, which evicts every cached module not prefixed
`core.`, and `handle_did_change` then runs that pass once per open document.
The cache is therefore cold for open documents at all times, so each hover,
completion, definition, and semantic-token request performs a full parse and
resolve of the file and its user-module import closure.

## Observed data (2026-07-26)

- Corpus: 2,192 `.smalls` files, 1.27 MB. Largest single script is 117,366
  bytes (`lib/nw/smalls/scripts/nwn1/combat.smalls`).
- `tools/smalls-lsp/server.cpp` is 1,391 lines; `lsp_text.hpp` is 223.
- Document sync is full-text (`textDocumentSync.change == 1`).
- The semantic token legend declares ten token types; the visitor emits seven
  and never emits `keyword`, `comment`, or `operator`. `tokenModifiers` is
  empty.
- `nw::smalls::Diagnostic` carries `type`, `severity`, `script`, `message`,
  and `location`. It has no stable diagnostic code.
- `Script` exposes `complete`, `complete_at`, `complete_dot`,
  `declaration_to_symbol`, `provider_for_decl`, `export_to_symbol`,
  `dependencies`, `diagnostics`, `inlay_hints`, `locate_export`,
  `locate_symbol`, `signature_help`, `ast`, and `text`.
- `AstPrinter` exists but emits parenthesized debug output from a resolved
  AST. It is not a source-faithful printer and cannot back a formatter.
- `Diagnostic.hpp` already provides `edit_distance` and `format_suggestions`,
  which are the inputs a did-you-mean quick fix needs.

## Direction

Tier 1 is protocol and per-document correctness. Every item is computable
from a single document plus its resolved import closure, so none of it is
blocked on an index. Tier 1 also fixes the two structural hazards above,
because every later provider is built on top of them.

Tier 2 is differentiation. These features use information this project has
and general-purpose language tooling does not: the VM, the propset and
`load_config!` pipeline, and the generated 2da configuration data.

Tier 3 is the incremental module graph and the workspace symbol index. It is
tracked by `smalls-lsp-workspace-navigation.md`. References, rename, workspace
symbols, call hierarchy, and reference code lenses are gated on it and must
not be advertised before it exists, because a half-scoped rename silently
corrupts source.

### Tier 1

| Issue | Blocks |
| --- | --- |
| `smalls-lsp-request-lifecycle.md` | everything |
| `smalls-lsp-semantic-token-fidelity.md` | all AST-walking providers |
| `smalls-lsp-document-sync-cost.md` | all latency claims |
| `smalls-lsp-document-symbols.md` | — |
| `smalls-lsp-structural-ranges.md` | — |
| `smalls-lsp-navigation-results.md` | — |
| `smalls-lsp-diagnostic-payload.md` | `smalls-lsp-code-actions.md` |
| `smalls-lsp-completion-results.md` | — |
| `smalls-lsp-code-actions.md` | — |
| `smalls-lsp-formatting.md` | — |
| `smalls-lsp-workspace-configuration.md` | — |

### Tier 2

| Issue | Depends on |
| --- | --- |
| `smalls-lsp-inlay-hint-fidelity.md` | Tier 1 traversal fix |
| `smalls-lsp-editor-affordances.md` | Tier 1; reference lens needs Tier 3 |
| `smalls-lsp-propset-config-awareness.md` | Tier 1 |
| `smalls-lsp-hierarchies.md` | Tier 3 index |
| `smalls-debug-adapter.md` | independent of the index |
| `smalls-lsp-testing-integration.md` | independent of the index |

## Ordering rule

Do not add a provider that walks the AST until the traversal contract in
`smalls-lsp-semantic-token-fidelity.md` is in place. Do not publish latency
numbers for any provider until the sync cost in
`smalls-lsp-document-sync-cost.md` is resolved, because the current numbers
measure repeated full recompiles rather than the provider.

## Done

- Every Tier 1 issue is resolved or explicitly deferred with a stated reason.
- The advertised server capability set and the set of capabilities the server
  actually answers are identical, and a test asserts that.
- No capability that requires a workspace index is advertised until the index
  exists.
