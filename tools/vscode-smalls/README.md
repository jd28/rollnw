# Smalls VSCode Extension (Minimal)

This directory contains a minimal VSCode extension that provides Smalls syntax highlighting.

## Run locally

1. Open this repo in VSCode.
2. Open `tools/vscode-smalls/` in the Explorer.
3. Press `F5` to launch an Extension Development Host.
4. In the new window, open any `.smalls` file (see `lib/nw/smalls/scripts/`).

## Files

- `tools/vscode-smalls/package.json` - extension manifest
- `tools/vscode-smalls/language-configuration.json` - comment/bracket rules
- `tools/vscode-smalls/syntaxes/smalls.tmLanguage.json` - TextMate grammar
