# Smalls LSP Structural Ranges

## Problem

`textDocument/foldingRange` and `textDocument/selectionRange` are both absent.

Without a folding provider, VS Code falls back to indentation-based folding.
That fallback cannot fold a multi-line function signature whose body is
indented identically, cannot fold a block comment, and cannot fold an import
group. It also disagrees with the AST whenever a construct's indentation does
not match its structure.

Without a selection-range provider, the expand-selection command falls back to
word and bracket heuristics, which do not know that a `case` arm, a call
argument, or an f-string interpolation is a unit.

## Direction

Folding ranges come from the AST, not from brace counting, with three region
kinds distinguished because editors treat them differently:

- `comment` for block comments and consecutive line-comment runs, so
  "fold all comments" works.
- `imports` for the leading run of import declarations, so the header collapses
  as one region.
- default (no kind) for function bodies, struct and sum bodies, blocks,
  `switch` bodies, `case` arms, and multi-line brace initializers.

A folding range ends on the line *before* the closing token so the closing
brace stays visible when folded. Ranges shorter than one line are omitted.

Selection ranges are the chain of enclosing AST nodes from the innermost node
containing the position out to the file, expressed as a linked `parent` chain.
The chain must be strictly increasing: each range properly contains its
parent's predecessor. Duplicate consecutive ranges — common when a node and
its only child share a span — must be collapsed, or the expand command
appears to do nothing.

Both providers walk the AST and depend on the traversal contract in
`smalls-lsp-semantic-token-fidelity.md`.

`language-configuration.json` should keep its brace-based folding markers as
the pre-server fallback; the two are complementary, not redundant.

## Done

- `foldingRangeProvider` and `selectionRangeProvider` are advertised.
- Block comments, line-comment runs, and the import header fold with the
  correct `kind`, verified by a fixture.
- No folding range is emitted for a construct that occupies a single line.
- A folded function body leaves its closing brace visible.
- Selection range chains are strictly increasing with no duplicate entries,
  asserted by a test over positions inside a call argument, a `case` arm, and
  an f-string interpolation.
- A position in an unparseable region returns an empty result rather than an
  error.
