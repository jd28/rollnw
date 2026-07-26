# Smalls LSP Completion Results

## Problem

`handle_completion` returns a bare JSON array of items carrying `label`,
`kind`, `detail`, and `documentation`. Several consequences follow from that
shape.

Returning an array rather than a `CompletionList` means `isIncomplete` is
absent and defaults to false. VS Code treats a complete list as authoritative:
it caches the first response and filters it client-side as the user keeps
typing, never re-querying. Any completion whose candidate set depends on what
has been typed — member access after a partial name, import paths — is
computed once against the wrong prefix and then filtered.

`resolveProvider` is false and `symbol_to_item` materializes `detail` and
`documentation` for every candidate eagerly. Documentation is the expensive
field and is needed only for the item the user has highlighted.

There is no `textEdit`. Without one the client guesses the replacement range
from its own word-boundary heuristic, which does not match the Smalls lexer —
notably for `$`-prefixed generic parameters and for the `!` in `array!`.

There is no `sortText`, so ordering is whatever the runtime enumerated. Locals
do not outrank distant module exports. There is no `filterText`, so an item
whose label differs from what the user types cannot match. There is no
`insertTextFormat`, so completing a function inserts a bare name rather than a
call with placeholder arguments. There is no `preselect`, no `commitCharacters`,
and no `tags` for deprecation.

Dot-completion detection reimplements lexing in `detect_dot_trigger` and
`identifier_before` using `isalnum || '_'`, which disagrees with the Smalls
lexer's identifier rule — the lexer also accepts `$`. The same mismatch
affects `identifier_at`, and therefore hover and definition.

## Direction

Return a `CompletionList` with `isIncomplete` true whenever the candidate set
is prefix-dependent, so the client re-queries as typing continues.

Move `documentation` — and any other field requiring work beyond enumeration —
into `completionItem/resolve`. Keep `label`, `kind`, `sortText`, `filterText`,
and `textEdit` in the initial response, because the client needs all of those
to order and filter.

Supply an explicit `textEdit` range computed from the Smalls lexer's notion of
the token under the cursor, not from a character-class loop. The word-boundary
logic in `lsp_text.hpp` should defer to the lexer so hover, definition, and
completion cannot disagree about where an identifier starts.

Order with an explicit `sortText`: locals and parameters first, then members
of the receiver type, then current-module declarations, then imported symbols,
then stdlib. Ordering by relevance is the difference between a usable and an
unusable completion list in a language with a large stdlib.

Emit function completions as snippets with parameter placeholders, and mark
deprecated symbols with `tags`.

## Done

- Completion returns a `CompletionList`, and `isIncomplete` is true for
  prefix-dependent results.
- `completionItem/resolve` is implemented and the initial response no longer
  materializes documentation. Time to first completion in a file importing the
  stdlib is measured before and after and recorded here.
- Every item carries an explicit `textEdit` whose range comes from the lexer.
  A test covers completion on `$T` and on `array!`.
- `identifier_at`, `identifier_before`, and `detect_dot_trigger` agree with the
  Smalls lexer on identifier boundaries, including `$`, asserted by a test
  driven from the lexer's own token stream.
- `sortText` implements the stated precedence, verified by a fixture where a
  local shadows a stdlib name.
- Function completions insert a call with placeholders.
- Deprecated symbols carry `tags` and render struck through.
