# Smalls LSP Inlay Hint Fidelity

## Problem

`inlayHintProvider` is advertised as a bare `true` and `Script::inlay_hints`
returns parameter-name hints only. Three limitations follow.

The provider is not resolvable. Advertising `true` rather than
`{ "resolveProvider": true }` means every hint must be fully materialized when
a range scrolls into view, including any tooltip.

Hints are plain strings. `InlayHintLabelPart` allows a hint to carry a
`location`, which makes the hinted text clickable and turns an inferred type
into a navigation target. A string label cannot do that.

Only parameter names are hinted. The resolver computes a `TypeID` for every
`var` declaration, every foreach binding, and every generic instantiation, and
none of that is surfaced. In a language with `var` inference and generics,
inferred-type hints are the feature that makes inference readable.

`handle_inlay_hints` also emits `kind: 2` (parameter) for every hint
regardless of what the hint is, so a type hint would be miscategorized and
themed wrongly.

## Direction

Add hint kinds and derive `kind` from the hint, not from a constant. Type
hints are `kind: 1`.

Emit these hint categories, each independently toggleable through
configuration, because inlay hints are the single most divisive editor feature
and a user who cannot turn one category off will turn all of them off:

- inferred type of a `var` declaration with no written type
- inferred element, key, and value types of a foreach binding
- inferred generic arguments at an instantiation site
- parameter names at call sites, as today
- inferred return type of a lambda with no written return type

Suppress a hint when it would restate what is already written. A `var` with an
explicit type gets no type hint. A call argument that is already a bare
identifier matching the parameter name gets no parameter hint — this is the
single largest source of inlay-hint noise.

Use `InlayHintLabelPart` with `location` set to the type's declaration so a
hinted type is clickable, and move tooltips into `inlayHint/resolve`.

This provider walks the AST and depends on the traversal contract in
`smalls-lsp-semantic-token-fidelity.md`. Its current parameter hints stop at
the same unhandled node kinds.

## Done

- `inlayHintProvider` advertises `resolveProvider`, and tooltips are computed
  in `inlayHint/resolve`.
- Type hints carry `kind: 1` and parameter hints `kind: 2`.
- Inferred `var`, foreach binding, generic argument, and lambda return hints
  are emitted, each with its own configuration toggle.
- A `var` with an explicit type and a call argument whose name already matches
  the parameter produce no hint, verified by a fixture.
- Hinted types carry a `location` and are clickable.
- Hints are produced inside foreach bodies, switch bodies, and lambdas.
