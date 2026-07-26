# Smalls LSP Diagnostic Payload

## Problem

A published diagnostic carries `range`, `message`, `severity`, and
`source: "smalls"`. Everything the protocol uses to make a diagnostic
actionable is missing.

There is no `code`. `nw::smalls::Diagnostic` has no stable identifier at all —
it carries `type`, `severity`, `script`, `message`, and `location`. Without a
code, a user cannot suppress a rule, documentation cannot be linked, and a
code action cannot key off a diagnostic without string-matching the message.
A quick-fix implementation that matches on message text breaks the moment
wording changes.

There is no `relatedInformation`, so "previously declared here", "expected
because of this parameter", and "imported from here" have nowhere to attach
and end up concatenated into the message or dropped.

There is no `tags`, so unused imports and unreachable code cannot be rendered
faded, and deprecated symbols cannot be struck through.

Diagnostics are push-only. There is no `diagnosticProvider`, so errors exist
only for files the user has already opened. A project-wide error count is
unobtainable without opening 2,192 files.

Separately, `publish_diagnostics` discards every diagnostic for a document
when `looks_like_config_file` holds — no imports, no declarations, and at
least one diagnostic. That predicate is also satisfied by an ordinary source
file whose syntax error prevented the parser from recovering a single
declaration. In that case the user sees a clean file that will not compile.

## Direction

Add a stable code to `nw::smalls::Diagnostic`. It must be an enumerated value
owned by the library, not a string formatted at the LSP boundary, so the
compiler, the test suite, and the server all name the same rule. Emit it as
`code` with a `codeDescription` URL once documentation exists.

Classify config data files by identity, not by failure shape. A file is a
config file because of where it is or what it declares, not because the
parser produced nothing. Until that identity exists, the suppression must be
removed: silently hiding real errors is worse than showing spurious ones,
because the user cannot tell the first case from a working file.

Use `relatedInformation` for every diagnostic that references a second
location, and `tags` for unused and deprecated results.

Implement pull diagnostics (`textDocument/diagnostic` and
`workspace/diagnostic`) alongside push. Pull lets the client control when
analysis happens, supports result reuse via `resultId`, and is the only route
to workspace-wide errors. Workspace pull over the full corpus must be
measured before it is advertised.

## Required decision

Whether the diagnostic code space is per-rule (`E0412`) or per-category
(`type-mismatch`). Per-rule supports precise suppression and stable links;
per-category is cheaper to retrofit onto existing call sites. Count the
distinct diagnostic emission sites in `lib/nw/smalls` before choosing.

## Done

- `nw::smalls::Diagnostic` carries a stable code, and every emission site
  supplies one.
- Published diagnostics include `code`, and `relatedInformation` wherever a
  second location is referenced.
- Unused imports and deprecated symbols carry the corresponding `tags` and
  render faded or struck through.
- The `looks_like_config_file` suppression is removed or replaced by a
  path- and declaration-based classification. A source file with a syntax
  error on line 1 reports that error.
- `textDocument/diagnostic` is implemented with `resultId` reuse.
- `workspace/diagnostic` latency and memory over all 2,192 corpus files are
  measured and recorded here before the capability is advertised.
