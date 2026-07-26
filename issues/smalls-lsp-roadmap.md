# Smalls LSP Feature Roadmap

## Problem

The server began with seven capabilities: diagnostics, completion, hover,
definition, signature help, parameter inlay hints, and full semantic tokens.
Everything else an editor expects was absent, and several of the features that
did exist returned the minimum payload the protocol permits.

Two structural hazards shaped the ordering of this work. Both are now fixed;
they are recorded because the constraints they impose still apply.

**AST traversal was opt-in and silently truncating.** `SemanticTokenVisitor`
derived from `NullVisitor`, whose base implementations are empty bodies, so a
node kind the visitor did not override terminated the walk at that node rather
than skipping only itself. Eleven kinds were unhandled, which removed semantic
tokens from most of a real function body. Fixed by `nw::smalls::WalkVisitor`;
every future AST-walking provider must derive from it.

**The module cache was discarded on every edit.** `publish_diagnostics` called
`Runtime::evict_user_modules`, which evicts every cached module not prefixed
`core.`, and `handle_did_change` ran that pass once per open document, so each
pass threw away what the previous one built. The cache was cold for open
documents at all times. Fixed by evicting only the edited module and its
dependents.

## Observed data (2026-07-26)

- Corpus: 2,192 `.smalls` files, 1.27 MB. Largest single script is 117,366
  bytes (`lib/nw/smalls/scripts/nwn1/combat.smalls`).
- `nw::smalls::Diagnostic` carries `type`, `severity`, `script`, `message`,
  and `location`. It has no stable diagnostic code, which is what gates
  `smalls-lsp-code-actions.md`.
- There are 81 diagnostic emission sites in `lib/nw/smalls`, 47 of them in
  `Parser.cpp`.
- `AstPrinter` emits parenthesized debug output from a resolved AST. It is not
  a source-faithful printer and cannot back a formatter.
- `Runtime::modules_` is private and holds only loaded modules, so no
  workspace-wide symbol search is possible without the index.
- `Script::dependencies` reports direct imports only.

## Direction

Tier 1 is protocol and per-document correctness. Every item is computable
from a single document plus its resolved import closure, so none of it is
blocked on an index. Tier 1 also fixes the two structural hazards above,
because every later provider is built on top of them.

Tier 2 is new capability. Most of it uses information this project has and
general-purpose language tooling does not: the VM, the propset and
`load_config!` pipeline, and the generated 2da configuration data. It also
holds the language-tooling work that lives in `lib/nw/smalls` rather than in
the server, where the language server is one caller among several.

Tier 3 is the incremental module graph and the workspace symbol index. It is
tracked by `smalls-lsp-workspace-navigation.md`. References, rename, workspace
symbols, call hierarchy, and reference code lenses are gated on it and must
not be advertised before it exists, because a half-scoped rename silently
corrupts source.

### Tier 1 — complete

Closed and deleted: request lifecycle, document symbols, structural ranges,
navigation results, semantic token fidelity, completion results, workspace
configuration, document sync cost, diagnostic payload, and code actions.

### Tier 2

Closed and deleted: inlay hint fidelity.

| Issue | Depends on |
| --- | --- |
| `smalls-lsp-formatting.md` | lexer comment trivia; a corpus-derived style |
| `smalls-lsp-editor-affordances.md` | reference lens needs Tier 3 |
| `smalls-lsp-propset-config-awareness.md` | also unblocks workspace diagnostics |
| `smalls-lsp-hierarchies.md` | Tier 3 index |
| `smalls-debug-adapter.md` | independent of the index |
| `smalls-lsp-testing-integration.md` | independent of the index |

## Ordering rule

Do not add a provider that walks the AST on `NullVisitor`. Derive it from
`nw::smalls::WalkVisitor`, which descends into every child, so an unhandled
node kind loses only itself instead of truncating the walk.

## Retiering (2026-07-27)

`smalls-lsp-formatting.md` moved from tier 1 to tier 2. Tier 1 was defined as
what a single document needs, which formatting satisfies, but the useful
distinction turned out to be different: every other tier 1 issue was the server
behaving correctly, and formatting is a language-tooling project that belongs in
`lib/nw/smalls` with a CLI entry point, where the server is one caller among
several and CI is another. Tier 1 now means "the server is correct", and it is
complete.

## Correction (2026-07-26, revised 2026-07-27)

`smalls-lsp-code-actions.md` was filed as tier 1, then reclassified as blocked
because add-import needs to find a symbol in a module the file does not import
and `Runtime::modules_` holds only loaded modules. That reclassification was too
pessimistic. Add-import needs an export index, not the reference index: a map
from exported name to module, built by enumerating the modules under each search
path. That is far cheaper than declaration identity, and it shipped.

References, rename, workspace symbols, and call hierarchy still need the full
index tracked by `smalls-lsp-workspace-navigation.md`, because they turn on
distinguishing two declarations that share a name.

## Done

- Every Tier 1 issue is resolved. The server's advertised capabilities and the
  set it actually answers are identical.
- No capability that requires a workspace index is advertised until the index
  exists. `workspace/symbol`, references, rename, and call hierarchy remain
  unadvertised for that reason, and `workspaceDiagnostics` remains unadvertised
  until config files declare their identity.
