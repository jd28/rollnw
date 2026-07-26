# Smalls LSP Formatting

## Problem

There is no `documentFormatting`, `documentRangeFormatting`, or
`documentOnTypeFormatting`. Smalls has no formatter at all, in the server or
as a standalone tool, so the 2,192-file corpus has no mechanical style
guarantee and code review carries formatting argument.

`AstPrinter` is not a starting point. It emits parenthesized debug output, it
requires a resolved AST because it depends on `type_id_` being populated, and
it discards comments. A formatter must work on a parse-only AST — an
unresolved or partially broken file still needs formatting — and must preserve
comments exactly.

## Problem detail: comments

Comments are the reason formatters are hard. The Smalls parser produces
`TokenType::COMMENT`, but comments are not AST nodes, so a naive
print-from-AST silently deletes every comment in the file. Any formatter
design that does not state where comments attach is not yet a design.

## Tier

Tier 2. This is not a language-server correctness gap; it is a language-tooling
project whose home is `lib/nw/smalls`. The server is one caller, a CLI and a CI
check are others, and none of it is blocked on the server.

## Direction

Build the formatter in the library (`lib/nw/smalls`), not in the server. It
must be usable from a CLI for corpus-wide reformatting and CI checking, and
the server must be one caller among several. A formatter that exists only
inside the language server cannot be run in CI.

Use a Wadler-style pretty printer over an intermediate document algebra —
concat, nest, line, group — rather than direct string emission. Direct
emission cannot make width-dependent break decisions, which is the entire
value of a formatter: deciding whether a call's arguments fit on one line.

Attach comments to tokens during lexing as leading and trailing trivia, and
carry that trivia through the AST so the printer can re-emit it. Decide
explicitly whether a comment binds to the preceding or following construct;
the usual rule is that a comment on its own line leads the next construct and
a comment after code on the same line trails it.

The formatter must be idempotent — formatting formatted output changes
nothing — and must never change program meaning. Both are testable properties
over the whole corpus, which is the real test suite here: format all 2,192
files, reformat, and diff.

Range formatting must not reindent regions outside the requested range.
On-type formatting should be limited to closing-brace dedent and, if the
language needs it, statement-terminator alignment; broader on-type behavior
fights the user's cursor.

Formatting a file with syntax errors must return no edits rather than
partial output, because partial formatting of a broken file destroys work.

## Required decision

The style itself: indent width, brace placement, maximum line width, trailing
commas, import ordering and grouping, and whether alignment is ever used.
Derive the defaults from what the existing 2,192-file corpus already does
rather than importing another language's conventions; measure the current
distribution first. Whether any of it is configurable is a separate decision,
and the cheaper answer is no.

## Done

- A formatter exists in `lib/nw/smalls` with a CLI entry point.
- Formatting all 2,192 corpus files produces no parse errors, and reformatting
  the output is a no-op, asserted by a corpus test.
- Comments survive formatting in every position: leading, trailing, inside
  parameter lists, inside brace initializers, and between declarations.
- Formatting a file with a syntax error returns no edits.
- Range formatting leaves text outside the range byte-identical.
- The chosen style is recorded here with the corpus measurements that
  justified each default.
- A CI check fails on unformatted committed Smalls source.
