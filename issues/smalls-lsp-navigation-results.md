# Smalls LSP Navigation Results

## Problem

Navigation works but returns the least informative payload the protocol
allows.

`handle_definition` returns a bare `Location`. The `LocationLink` form carries
`originSelectionRange`, `targetRange`, and `targetSelectionRange`, which are
what let an editor highlight the source token, show a peek window scoped to
the whole declaration, and place the cursor on the target identifier.  With a
bare `Location` the peek window opens on a single range and the origin token
is not highlighted.

`handle_hover` returns `contents` with no `range`, so the hovered token is not
underlined and the hover box tracks the cursor rather than the symbol.

Only `definitionProvider` exists. `typeDefinition`, `declaration`, and
`implementation` are unimplemented, so "go to type definition" on a variable
has no answer even though the resolver has computed its `TypeID`.

`handle_definition` also resolves the target URI by scanning every open
document and calling `module_name_for_uri` on each — a filesystem-canonicalizing
call — inside the loop.

## Direction

Return `LocationLink[]` when the client advertises
`textDocument.definition.linkSupport`, and `Location[]` when it does not. The
capability is per-client and must be read at `initialize`, not assumed.

`originSelectionRange` is the identifier under the cursor.  `targetRange` is
the full declaration including its body. `targetSelectionRange` is the target
identifier alone. These are the same two ranges `documentSymbol` needs, so
compute them once in a shared helper.

Give hover the range of the identifier it resolved, which
`identifier_at` already locates.

`typeDefinition` resolves the `TypeID` of the expression at the position and
locates the declaration of that type. `declaration` differs from `definition`
in Smalls only for imported symbols, where the declaration is the import and
the definition is the providing module; return the import site.

Reuse the module-name cache from `smalls-lsp-document-sync-cost.md` for target
URI resolution rather than scanning open documents per request.

Navigation into a symbol whose provider is a module the runtime resolved but
that has no on-disk file — a synthesized or native module — must return no
result rather than a fabricated URI.

## Done

- `LocationLink` is returned when `linkSupport` is advertised and `Location`
  otherwise, verified by tests driving both client capability shapes.
- Peek definition highlights the origin token and scopes the preview to the
  whole declaration.
- Hover returns a `range` covering the resolved identifier.
- `typeDefinitionProvider` and `declarationProvider` are advertised and
  answered.
- Go-to-definition performs no filesystem syscall on a warm module-name cache.
- Navigation to a native or synthesized module returns no result and logs at
  debug level rather than returning a nonexistent path.
