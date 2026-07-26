# Smalls VSCode Extension

Smalls language support: syntax highlighting plus the `smalls-lsp` language
server. See `tools/smalls-lsp/README.md` for what the server provides.

## Language server binary

The extension looks for a server in this order:

1. `smalls.lsp.path`, when it is set explicitly
2. a platform binary bundled in `bin/`:
   - Linux x64: `bin/smalls-lsp-linux-x64`
   - macOS universal: `bin/smalls-lsp-darwin-universal`
   - Windows x64: `bin/smalls-lsp-win32-x64.exe`
3. `bin/smalls-lsp`, produced by the local `vscode-smalls-vsix` CMake target
4. `smalls-lsp` on `PATH`

A VSIX is a zip and the executable bit does not reliably survive packaging, so
a bundled binary is marked executable at activation.

`smalls.lsp.path` and `smalls.modulePaths` are `machine-overridable` and are
listed as restricted configurations. Both cause code to be loaded from paths a
repository could otherwise choose, so neither is honored from workspace
settings in an untrusted workspace.

## Module search paths

`smalls.modulePaths` entries may be absolute or relative. Relative entries
resolve against each workspace folder, and `${workspaceFolder}` is substituted.
Every workspace folder is used, so multi-root workspaces resolve modules from
all of them.

The stdlib bundled in the VSIX is added last, which is what lets a published
extension resolve `core.` imports outside this repository. When the workspace
is a rollnw checkout, `lib/nw/smalls/scripts` is picked up from the source tree
as well.

Changing these settings re-resolves modules without a window reload. Changing
`smalls.lsp.path` restarts the server. **Smalls: Restart Language Server**
restarts it on demand.

## Run locally

1. Open this repo in VSCode.
2. Open `tools/vscode-smalls/` in the Explorer.
3. Press `F5` to launch an Extension Development Host.
4. In the new window, open any `.smalls` file (see `lib/nw/smalls/scripts/`).

Build a VSIX with the `vscode-smalls-vsix` CMake target, which builds
`smalls-lsp`, copies it and the stdlib into the extension, and packages it.

## Files

- `package.json` - extension manifest
- `src/extension.ts` - server discovery, module paths, client lifecycle
- `language-configuration.json` - comment/bracket rules
- `syntaxes/smalls.tmLanguage.json` - TextMate grammar
- `snippets/smalls.json` - snippets
