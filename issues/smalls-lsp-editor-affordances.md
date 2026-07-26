# Smalls LSP Editor Affordances

## Problem

Two low-cost providers are absent, and their absence is felt on every file.

`textDocument/documentLink` is unimplemented, so the module path in an
`import "core.math"` declaration is inert text. Go-to-definition works on
symbols but not on the import that brought them in, which is where a reader
looking to understand a file's dependencies actually clicks.

`textDocument/codeLens` is unimplemented. Two lenses are worth having: a
reference count above each declaration, and a run action above script entry
points. The reference count is the standard way a reader judges whether a
declaration is safe to change.

## Direction

Document links resolve an import's module path to the providing file's URI
using the same module resolution that go-to-definition uses, so the two cannot
disagree. Set `tooltip` to the resolved absolute path. An import that resolves
to a native or synthesized module with no on-disk file yields no link rather
than a broken one.

Use `documentLink/resolve` for the target so the initial response does not
resolve every import in the file. Link ranges are cheap; targets are not.

Code lenses split by dependency:

- The **run lens** is computable today. A script with a recognized entry point
  gets a lens that invokes it through the existing runtime. This needs a
  decision about what constitutes an entry point.
- The **reference count lens** requires the index from
  `smalls-lsp-workspace-navigation.md`. It must not ship before then: a count
  that silently means "references in this file" is worse than no count,
  because it reads as authoritative.

Code lenses resolve lazily through `codeLens/resolve`. Computing a reference
count for every declaration in a file on every viewport change is exactly the
workload lazy resolution exists to avoid.

Both providers walk the AST and depend on the traversal contract in
`smalls-lsp-semantic-token-fidelity.md`.

## Required decision

What marks a Smalls script as runnable — an annotation, a conventional
function name, or a manifest entry. Derive it from what the existing corpus
and the Arclight runtime already treat as an entry point rather than
inventing a new marker.

## Done

- `documentLinkProvider` is advertised with `resolveProvider` true; import
  paths are clickable and land on the providing file.
- An import of a native or synthesized module produces no link.
- `codeLensProvider` is advertised with `resolveProvider` true.
- The run lens invokes an entry point through the runtime, and the entry-point
  rule is documented in `tools/smalls-lsp/README.md`.
- The reference count lens is not advertised until the index exists.
- Lens and link targets are computed in the resolve request, not the initial
  response.
