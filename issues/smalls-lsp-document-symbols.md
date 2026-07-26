# Smalls LSP Document Symbols

## Problem

The server implements no `textDocument/documentSymbol`, so a Smalls file has
no outline view, no breadcrumb bar, and no `Ctrl+Shift+O` symbol jump. This is
the most frequently used navigation affordance in an editor and it is the
cheapest missing feature to supply: the declaration walk it needs already
exists in skeletal form inside `SemanticTokenVisitor`.

`workspace/symbol` is also absent, but it is not equivalent work. Document
symbols are a function of one parsed file. Workspace symbols require the index
tracked by `smalls-lsp-workspace-navigation.md`.

## Direction

Return the hierarchical `DocumentSymbol[]` form, not the flattened
`SymbolInformation[]` form. The flat form cannot express a field inside a
struct or a variant inside a sum type, which is most of what a Smalls file
declares.

Each symbol needs two ranges and they are not the same. `range` covers the
whole declaration including its body, and drives breadcrumb region tracking.
`selectionRange` covers the identifier alone, and drives what the editor
reveals and selects on jump. `selectionRange` must be contained in `range`;
VS Code drops symbols where it is not.

Map declarations to LSP `SymbolKind` deliberately rather than by convenience:
`FunctionDefinition` to `Function`, `StructDecl` to `Struct`, `SumDecl` to
`Enum` with `VariantDecl` children as `EnumMember`, `NewtypeDecl` and
`TypeAlias` and `OpaqueTypeDecl` to appropriate type kinds, and module-level
`VarDecl` to `Variable` or `Constant` depending on `const`.

Nested declarations are children, not siblings: struct fields under the
struct, sum variants under the sum type, and — where the language permits
them — nested functions under their parent.

Do not descend into function bodies. Local variables in an outline are noise;
the outline is a map of the file's exported shape.

This provider walks the AST, so it depends on the traversal contract in
`smalls-lsp-semantic-token-fidelity.md`. Building it on `NullVisitor` directly
reproduces that bug in a second place.

## Done

- `documentSymbol` is advertised and returns hierarchical results.
- Struct fields nest under their struct and sum variants under their sum type,
  verified by a fixture covering every declaration kind in `BaseVisitor`.
- Every returned symbol satisfies `selectionRange` contained in `range`,
  asserted by a test.
- Function bodies contribute no symbols.
- A file that fails to parse still returns symbols for the declarations that
  did parse, rather than an empty list.
- `workspace/symbol` is not advertised until the index exists.
