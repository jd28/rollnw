# Smalls LSP Workspace Configuration

## Problem

Module search paths reach the server exactly once, as `-I` arguments on the
command line, and can never change afterward. Everything downstream follows
from that.

The server ignores `workspaceFolders` and `rootUri` from `initialize`
entirely. It does not implement `workspace/didChangeConfiguration`,
`workspace/configuration`, or `workspace/didChangeWorkspaceFolders`. Editing
`smalls.modulePaths` therefore has no effect until the window is reloaded, and
nothing tells the user that. Adding a second folder to a multi-root workspace
has no effect at all.

The extension compounds this. It reads only `workspaceFolders[0]`, and it
hardcodes this repository's own layout as the stdlib location:

```ts
path.join(workspaceRoot, 'lib', 'nw', 'smalls', 'scripts')
```

Outside rollnw that path does not exist, so a published extension resolves no
stdlib. `CMakeLists.txt` copies `core` into `tools/vscode-smalls/stdlib/core`
for packaging, but `extension.ts` never adds that directory to the search
path, so the bundled copy is shipped and never used.

Relative entries in `smalls.modulePaths` are resolved by trying, in order, the
workspace root, the extension directory, its parent, its grandparent, and
`process.cwd()`. The extension host's working directory is arbitrary, and the
parent-directory probes are meaningless once installed. Resolution is
order-dependent and unpredictable.

`smalls.lsp.path` declares no `scope`, so it defaults to `window` scope and is
settable from a repository's own `.vscode/settings.json`. Opening an untrusted
repository spawns whatever executable that file names. The extension declares
no `capabilities.untrustedWorkspaces`.

The client watches `**/*.smalls` within the workspace only, so stdlib files
outside the workspace are never watched and the server is never told they
changed.

## Direction

Module search paths become dynamic server state with one ordered, documented
precedence: explicit configuration, then workspace folders, then the bundled
stdlib, then command-line `-I`. `-I` stays as the headless and test entry
point, not as the only one.

Consume `workspaceFolders` at `initialize` and track
`didChangeWorkspaceFolders`. Request settings via `workspace/configuration`
and honor `didChangeConfiguration` by recomputing paths and re-diagnosing open
documents. A configuration change must not require a restart; if some change
genuinely cannot be applied live, the server prompts for a restart rather than
silently ignoring it.

Resolve relative configured paths against workspace folders only, and support
`${workspaceFolder}` substitution. Delete the `process.cwd()` and
parent-directory probes.

Ship the stdlib and use it: the extension adds its own bundled `stdlib`
directory to the search path, and the rollnw-specific source path is a
development convenience discovered by probing, not a hardcoded assumption
about every workspace.

Register file watchers from the server via `client/registerCapability` so the
watched set covers every active module path rather than only the workspace.

Mark `smalls.lsp.path` `machine-overridable` and declare
`capabilities.untrustedWorkspaces` with both settings listed as restricted.
This is the same treatment clangd and rust-analyzer give a server-path
setting, and it is the difference between opening a cloned repository being
safe and being arbitrary code execution.

## Done

- Changing `smalls.modulePaths` re-resolves modules and re-publishes
  diagnostics with no window reload, verified end to end.
- A multi-root workspace resolves modules from every folder.
- The precedence order is documented in `tools/smalls-lsp/README.md` and
  asserted by a test with a symbol resolvable from two sources.
- The bundled stdlib is on the search path in a packaged VSIX, verified by
  installing the VSIX outside this repository and resolving a `core.` import.
- No relative path is resolved against `process.cwd()` or the extension's
  parent directories.
- `smalls.lsp.path` is `machine-overridable` and the extension declares
  `untrustedWorkspaces` with `smalls.lsp.path` and `smalls.modulePaths`
  restricted.
- Server-registered watchers cover all active module paths, including stdlib
  outside the workspace.
