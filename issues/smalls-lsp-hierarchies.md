# Smalls LSP Hierarchies

## Problem

`textDocument/prepareCallHierarchy` and `textDocument/prepareTypeHierarchy`
are unimplemented. Without them there is no way to ask what calls a function
or what a function calls, and no way to see the relationship between a sum
type and its variants or a newtype and its wrapped type.

Call hierarchy is how a reader answers "is this safe to change", and it is the
question that matters most in a 2,192-file corpus where a single combat
routine is called from many scripts.

## Direction

These are index consumers, not independent features. Incoming calls are a
filtered reference query, and the reference index tracked by
`smalls-lsp-workspace-navigation.md` is a hard prerequisite. Outgoing calls
can be computed from one resolved AST, but shipping outgoing calls without
incoming calls produces a half-working hierarchy view, which is worse than an
absent one.

Both hierarchies are three-request protocols: `prepare` establishes the item
under the cursor, then `incomingCalls`/`outgoingCalls` or
`supertypes`/`subtypes` expand it on demand. Expansion must be lazy — the
client drives the tree one level at a time, and computing a transitive closure
in the prepare step defeats the design and will not terminate on recursive
call graphs.

Items must be identified by resolved declaration identity, not by name.
Duplicate and shadowed names across modules are the case that distinguishes a
correct hierarchy from a text search, and it is the reason
`smalls-lsp-workspace-navigation.md` requires declaration identity in the
index.

Recursive and mutually recursive calls must terminate. The client handles
cycle presentation, but the server must not loop while answering one level.

Type hierarchy in Smalls covers sum types and their variants, newtypes and
their wrapped types, and type aliases. State which relationships are modeled
before implementing, because Smalls has no inheritance and the LSP's
supertype/subtype vocabulary maps onto it only by convention.

## Done

- The reference index exists and `smalls-lsp-workspace-navigation.md` is
  resolved.
- `callHierarchyProvider` is advertised; incoming and outgoing calls are
  answered for a function called from multiple modules, verified against a
  fixture with a deliberately shadowed name in a second module.
- Expansion is lazy: a prepare request computes one item and no edges.
- A recursive and a mutually recursive function each answer without looping.
- `typeHierarchyProvider` is advertised, and the modeled relationships are
  documented in `tools/smalls-lsp/README.md`.
- Hierarchy queries over the full corpus are measured and recorded here.
