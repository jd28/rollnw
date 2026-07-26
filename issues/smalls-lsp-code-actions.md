# Smalls LSP Code Actions

## Problem

There is no `textDocument/codeAction`. Every diagnostic the server publishes
is a dead end: it tells the user something is wrong and offers no way to fix
it. This is the largest gap between the current server and one that feels
finished, because it is the only feature where the tool acts on the user's
behalf rather than describing state.

The inputs already exist and are unused. `Diagnostic.hpp` provides
`edit_distance` and `format_suggestions`, which is exactly the machinery a
did-you-mean fix needs. The resolver knows which imports are unused. The
runtime knows every exported symbol in every reachable module, which is what
an auto-import fix needs.

## Direction

Quick fixes must be keyed on diagnostic codes, not on message text. This makes
`smalls-lsp-diagnostic-payload.md` a hard prerequisite: without stable codes,
every action matches on prose and breaks when wording changes.

Start with the fixes that pay for themselves, in this order:

- **Add import for an unresolved name.** Search reachable module exports for
  the identifier, offer one action per candidate module, and insert the import
  in the existing import block in sorted position. This is the highest-value
  action in any language with modules.
- **Replace with suggested name.** Drive from `format_suggestions` on an
  unresolved-identifier diagnostic.
- **Remove unused import.** Pairs with the `tags` work in the diagnostic
  payload issue.
- **Add missing struct field** in a brace initializer that omits a required
  field, using the field's declared type to pick an initializer.

Return `CodeAction` objects with `kind` set — `quickfix` for the above — not
bare `Command` objects. Set `diagnostics` on each action so the editor
associates it with the lightbulb on the right line, and `isPreferred` on the
single unambiguous fix when there is one, which is what enables the
auto-fix keybinding.

Support `codeActionLiteralSupport` detection: clients that do not advertise it
need the legacy `Command` form.

Compute actions lazily. The `only` field in the request narrows what the
client wants; honor it rather than computing every kind on every cursor move.

Refactorings (`refactor.extract`, `refactor.inline`) and
`source.organizeImports` are deliberately out of scope here.
Organize-imports needs the formatter from `smalls-lsp-formatting.md`;
extract-function needs the reference index.

## Done

- `codeActionProvider` is advertised with the supported kinds enumerated.
- Add-import, replace-with-suggestion, remove-unused-import, and
  add-missing-field are implemented, each keyed on a diagnostic code.
- No code action matches on diagnostic message text, asserted by a test that
  reworks a message and confirms the action still fires.
- Applying an add-import action to a file with no existing import block, one
  import, and many imports produces correctly placed and sorted results in all
  three cases.
- Actions set `diagnostics` and, where unambiguous, `isPreferred`.
- The `only` filter is honored; a request for `quickfix` computes no other kind.
- Clients without `codeActionLiteralSupport` receive the `Command` form.
