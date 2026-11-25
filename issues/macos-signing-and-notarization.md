# macOS Developer ID signing and notarization

Status: **Blocked on Apple Developer Program enrollment activation** (enrolled
2026-11-25; waiting on Apple's "Welcome to the Apple Developer Program"
confirmation email).

## Current state

- `.github/workflows/ci.yml` already wires the `macos-tools-sign` job (mirroring
  `windows-tools-sign`) gated on `vars.MACOS_SIGNING_ENABLED == 'true'` or the
  `workflow_dispatch` input `sign_macos: true`.
- `macos-tools-sign` imports a Developer ID Application `.p12` into an ephemeral
  keychain on `macos-15`, signs all bundled `*.dylib` files (`libSDL3.0.dylib`,
  `libslang*.dylib`) and all five executables (`rollnw-client`, `mudl`,
  `smalls`, `smalls-lsp`, `smalls-datagen`) with `--options runtime --timestamp`,
  verifies signatures and timestamps, re-runs
  `scripts/ci/validate_tool_packages.cmake` under Hardened Runtime, and
  notarizes via `xcrun notarytool submit --wait`.
- Until `MACOS_SIGNING_ENABLED=true` is set, CI skips `macos-tools-sign` cleanly
  and packages unsigned (ad-hoc signed) `rollnw-tools-macos-arm64` tarballs.

## Activation checklist (once Apple approves enrollment)

1. Sign out and back in to `https://developer.apple.com/account` and
   `https://appstoreconnect.apple.com`.
2. Create a **Developer ID Application** certificate (Xcode → Settings →
   Accounts → Manage Certificates → `+` → Developer ID Application, or via the
   Developer portal with a Keychain Access CSR), then export it from Keychain
   Access as a password-protected `.p12`.
3. In App Store Connect → **Users and Access → Integrations → App Store Connect
   API**, click **Request Access** (first-time one-click activation for Account
   Holder), create a Team API key, download the `.p8` file, and record its Key
   ID and Issuer ID.
4. Create the `macos-signing` GitHub Actions environment and populate its 5
   secrets:
   - `APPLE_DEVELOPER_ID_CERT_B64`: `base64 -i DeveloperID.p12 | tr -d '\n'`
   - `APPLE_DEVELOPER_ID_CERT_PASSWORD`
   - `APPLE_NOTARY_KEY_B64`: `base64 -i AuthKey_XXXX.p8 | tr -d '\n'`
   - `APPLE_NOTARY_KEY_ID`
   - `APPLE_NOTARY_ISSUER_ID`
5. Run a dry-run on `main` via **Actions → CI → Run workflow → `sign_macos: true`**
   and verify:
   - all bundled dylibs and all 5 executables pass `codesign --verify --strict`
   - `validate_tool_packages.cmake` passes under Hardened Runtime (confirming
     `libSDL3.0.dylib` and `libslang*.dylib` load cleanly without relaxed
     entitlements)
   - `xcrun notarytool submit --wait` returns `Accepted`
6. Download the `macos-tools-package-signed` artifact on macOS (or apply
   `com.apple.quarantine` manually) and verify `rollnw-client` and `mudl` launch
   without Gatekeeper blocking.
7. Set repository variable `MACOS_SIGNING_ENABLED=true`.

## Optional follow-up

- Reuse the same `macos-signing` environment to sign `smalls-lsp-darwin-arm64`
  in `.github/workflows/vscode-extension.yml` before VSIX packaging.
