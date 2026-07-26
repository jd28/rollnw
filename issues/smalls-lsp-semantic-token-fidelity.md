# Smalls LSP Semantic Token Fidelity

## Problem

`SemanticTokenVisitor` derives from `NullVisitor`. Every `visit` the visitor
does not override is an empty body, so an unhandled node does not merely fail
to produce a token — it ends the traversal at that node and every descendant
is skipped.

The visitor overrides 26 node kinds. These are unhandled, and each one
silently truncates the walk:

| Node | Lost subtree |
| --- | --- |
| `JumpStatement` | the operand of every `return <expr>` |
| `ComparisonExpression` | both sides of every `==`, `!=`, `<`, `<=`, `>`, `>=` |
| `LogicalExpression` | both sides of every `&&` and `\|\|` |
| `IndexExpression` | target and index of every `a[i]` |
| `ForEachStatement` | loop variables, collection, and the whole body |
| `SwitchStatement` | target and the whole body |
| `LabelStatement` | `case` patterns, bindings, and guards |
| `FStringExpression` | every interpolated expression |
| `LambdaExpression` | parameters, return type, and body |
| `TupleLiteral` | every element |
| `OpaqueTypeDecl` | the declared type name |

In a representative function body this removes semantic highlighting from
most identifiers: every returned expression, every condition, every indexed
access, every f-string interpolation, and the entire contents of any
`foreach` or `switch`.

Two further gaps are in the payload rather than the walk. The legend declares
ten token types but the visitor emits seven; `keyword`, `comment`, and
`operator` are dead entries. `tokenModifiers` is empty, so a theme cannot
distinguish a declaration from a use, a `const` from a mutable binding, or a
stdlib symbol from a user symbol.

`add_token` also discards any range whose start and end lines differ, so a
multi-line construct contributes nothing.

## Direction

The traversal contract is the primary fix and it must not be re-established
per visitor. Provide one child-walking visitor in the Smalls library that
descends every node kind and does nothing else, and derive tooling visitors
from that instead of from `NullVisitor`. `NullVisitor` stays as the
do-nothing base for consumers that genuinely want to stop.

A tooling visitor derived from the walking base fails safe: an unhandled node
loses its own token and keeps its subtree. A new AST node kind added later
degrades instead of silently blanking a region of every file.

Populate `tokenModifiers` with at least `declaration`, `definition`,
`readonly`, and `defaultLibrary`, and remove legend entries nothing emits.
Add `full.delta` and `range` so a refresh does not recompute the whole file.

## Required decision

Whether multi-line tokens should be split across line boundaries or remain
dropped. Splitting is required for f-strings that span lines; dropping is
acceptable only if the lexer cannot produce one. Decide from the lexer, not
from the current visitor's limitation.

## Done

- A shared child-walking visitor base exists in `lib/nw/smalls`, and
  `SemanticTokenVisitor` derives from it.
- A test asserts that for every node kind in `BaseVisitor`, a document
  containing that node yields tokens for identifiers nested beneath it.
- `return`, comparison, logical, index, foreach, switch, case, f-string,
  lambda, and tuple bodies all produce tokens; a fixture covering all ten is
  checked in.
- The declared legend and the emitted token-type set are identical, asserted
  by a test.
- Token modifiers are emitted for declarations and stdlib symbols.
- `full.delta` and `range` are implemented and advertised.
- Emitted tokens are non-overlapping and sorted, asserted by a test.
