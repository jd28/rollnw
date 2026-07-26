# Smalls LSP Testing Integration

## Problem

Smalls tests exist in the corpus and run through the C++ test binary. From an
editor there is no test explorer, no per-test run or debug action, no inline
pass/fail indicator, and no way to jump from a failure to the assertion that
produced it. A script author edits a test and then leaves the editor to find
out whether it passed.

## Direction

This is a VS Code Testing API feature in the extension, not an LSP capability.
The protocol has no test concept, and attempts to carry one over custom LSP
notifications end up reimplementing the Testing API poorly.

The server's role is discovery only: it knows where tests are declared because
it has the AST. Expose discovered tests over a custom notification carrying
identity, name, and range, and let the extension own the test tree,
execution, and result presentation. Keeping execution out of the language
server matters — a test that hangs or crashes must not take down the editor's
language support.

Discovery must be incremental. Re-enumerating the corpus on every edit is the
same mistake as the diagnostics fan-out in
`smalls-lsp-document-sync-cost.md`; a test tree updates for the edited file
only.

Test identity must be stable across edits. If identity is derived from
position, every test above an insertion point is reported as removed and
re-added, and the tree loses its state on every keystroke.

Failures attach to the assertion's source range so they render inline, which
requires the runtime to report a source position with a failure rather than a
message alone.

Debugging a single test is where this compounds with `smalls-debug-adapter.md`
— run and debug should be the same test item with two actions, so this should
land after the adapter exists rather than growing a second execution path.

## Required decision

How a test is marked: an annotation, a naming convention, or a manifest.
Derive it from how the existing corpus tests are already identified by the C++
test binary rather than introducing a second marker that the two runners can
disagree about.

## Done

- The test-marking rule is decided and is the same rule the C++ test binary
  uses.
- The server discovers tests and reports identity, name, and range; discovery
  is incremental per file.
- Test identity is stable across insertions above a test, verified by a test.
- The extension contributes a test tree with run, and results render inline on
  the failing assertion.
- A crashing or hanging test does not disturb the language server.
- Debug-a-single-test is wired to `smalls-dap` once it exists.
