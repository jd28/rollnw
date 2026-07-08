# Smalls Compiler Frontend Input Hardening

## Problem

Script source is untrusted. The lexer is solid, but the parser's
recursion-depth guard is not wired into the production compile path, one
recursive construct bypasses it structurally, and later passes trust literal
parsing and struct-layout invariants that aren't actually established. Found
in the 2026-07 smalls audit; verified in source.

## Findings

- Depth guard disabled in production. `Context.hpp:27`
  (`max_parse_depth = 0` = unlimited), `Parser.cpp:72` (guard only enforces
  when `limit != 0`), `runtime.cpp:203` (`Runtime`'s `diagnostic_context_` is
  default-constructed, never sets the limit). Only the fuzz harness and one
  unit test set a finite value. So any real script with deep nesting
  (`((((...`, or nested `if`) stack-overflows the compiler process instead of
  a diagnostic. Fix: set a sane default (e.g. 256, matching max call depth)
  in `Runtime`'s diagnostic context.
- Brace-init literals bypass the guard entirely. `parse_brace_init_literal`
  (`Parser.cpp:908`) takes its element from `parse_expr_conditional()`, and
  the recursive re-entry never transits `parse_expr()` (`:310`) — the only
  function that increments `depth_`. `{`×N `1` `}`×N recurses N C++ frames
  with the counter stuck at 1, so it overflows even when a finite
  `max_parse_depth` is configured. Fix: add a `ParseDepthGuard` at the top of
  `parse_brace_init_literal`, and audit other entry points reachable without
  transiting `parse_expr()`.
- Literal overflow silently yields 0. `util/string.cpp:64`
  (`DEFINE_FROM_INT`) / `:75` (`DEFINE_FROM_FLOAT`), used by
  `Parser.cpp:626`, check only `res.ptr != str.data()` and ignore `res.ec`.
  On `result_out_of_range`, `from_chars` leaves the output at its initial
  `0`/`0.0` and advances `ptr`, so `99999999999` parses "successfully" as 0
  with no diagnostic. Fix: also require `res.ec == std::errc()`.
- Forward/mutually-referenced struct field types get `size = 0`.
  `TypeResolver` visits `script->decls` once in file order
  (`TypeResolver.cpp:1528`), baking each struct's layout at visit time
  (`:2098` `define()`); `types.cpp:124` placeholders have `size = 0`. A field
  whose type is declared later, or a mutual `A{b:B} / B{a:A}` cycle, reads
  the placeholder size — later fields alias storage and the struct is
  under-sized. Only direct self-reference is caught
  (`AstResolver.cpp:403`); no later fix-up pass exists. Fix: two-phase layout
  (register all struct sizes before computing any offsets), or reject a field
  whose type isn't fully defined with a clear "recursive value type" /
  "type used before fully declared" diagnostic.
- Semantic diagnostics have no cascade cap and each is expensive.
  `AstResolver.cpp:121` (`report`/`error`/`warn`, no `error_limit_` analog),
  `Context.cpp:62` (`find_line` rescans source from offset 0 per diagnostic,
  O(file_size)), `Diagnostic.cpp:9` (`edit_distance`) run against every
  in-scope name per unresolved identifier (`TypeResolver.cpp:3053`). A script
  with many unresolved identifiers is ~O(errors × file_size + errors ×
  symbols × len²) on the untrusted compile path. The parser caps its own
  errors at 20; the semantic layer does not. Fix: cap total semantic
  diagnostics, and make `find_line` use the lexer's line index instead of
  rescanning.

## Fix Policy

Guards were built but not wired; the fix is to wire them (default depth
limit, guard at every recursive entry, semantic error cap) and to enforce —
not assume — the "referenced type is fully defined" invariant before reading
its size.

## Verification

Extend the smalls fuzz corpus (which already sets a depth limit) with the
brace-init nesting case; unit tests for out-of-range int/float literals,
forward-referenced and mutually-recursive struct layout, and a
many-errors script for the diagnostic cap.
