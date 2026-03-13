# Smalls VSCode Extension (Minimal)

This directory contains a minimal VSCode extension that provides Smalls syntax highlighting.

## Bundled language server

Published VSIX packages bundle `smalls-lsp` binaries for:

- Linux x64: `bin/smalls-lsp-linux-x64`
- macOS universal: `bin/smalls-lsp-darwin-universal`
- Windows x64: `bin/smalls-lsp-win32-x64.exe`

By default, the extension uses a bundled binary when available. You can override this in settings with `smalls.lsp.path`.

## Run locally

1. Open this repo in VSCode.
2. Open `tools/vscode-smalls/` in the Explorer.
3. Press `F5` to launch an Extension Development Host.
4. In the new window, open any `.smalls` file (see `lib/nw/smalls/scripts/`).

## Files

- `tools/vscode-smalls/package.json` - extension manifest
- `tools/vscode-smalls/language-configuration.json` - comment/bracket rules
- `tools/vscode-smalls/syntaxes/smalls.tmLanguage.json` - TextMate grammar
