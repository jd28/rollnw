# Release process

rollnw's library lives at HEAD. A Git revision identifies library source; a
`YYYY.MM.DD[.N]` tag identifies a distribution snapshot; each executable's numeric
`tools/*/VERSION.txt` identifies its next tool release. The existing CMake project
number is legacy metadata, not another compatibility promise. Snapshots are not
LTS branches: fixes land on `main`, and users update to a newer revision.

Keep `main` buildable and test changes through the actual library consumers.
Document public source/behavior changes and migrations in release notes. Do not
promise cross-revision C++ ABI compatibility. Persisted project formats are a
separate contract: incompatible format changes need explicit version/migration
handling, not merely a tool version bump.

## Preparing a distribution

Use a checkout with full history and all release tags. Set numeric tool versions
first. Components are `0..65535`, without leading zeroes, to fit Windows version
resources. A newly approved tool version must exceed its versions in ancestor
release manifests. This conservative rule includes changes inherited from the
library and bundled scripts. During `0.x`, call out incompatible changes in the
release notes; a number alone is not sufficient migration guidance.
Before post-release development, advance affected tools to their next planned
numeric versions. The build supplies the development suffix; do not put it back
in the VERSION.txt files. Keep the `.txt` extension: extensionless `VERSION`
shadows the C++ `<version>` header on case-insensitive filesystems.

For example, to approve just mudl and the client in a dated snapshot:

```sh
cmake -DROLLNW_RELEASE_TAG=2026.09.08 \
  '-DROLLNW_RELEASE_TOOLS=mudl;rollnw-client' \
  -P scripts/ci/prepare_release.cmake
```

This writes `release.json`; it does not commit, tag, push, or publish. Review and
commit the manifest, VERSION.txt changes, and release notes. Tag that exact commit.
Do not move published tags or replace their assets. A correction uses a new date
tag (or `.2`, `.3`, etc.) and new versions for newly approved tools.

An explicitly empty `-DROLLNW_RELEASE_TOOLS=` prepares a snapshot with no stable
tool approvals. Unselected tools remain visibly developmental, even when bundled
in a dated distribution. To approve all five tools, list all five. Approval is
never inferred from a tool name, a directory change, or an optimized build.

For a local reproduction of an approved tagged build, configure with:

```sh
cmake --preset ci-linux-tools-package -DROLLNW_RELEASE_TAG=2026.09.08
cmake --build --preset ci-linux-tools-package \
  --target mudl rollnw-client smalls smalls-lsp smalls-datagen
```

When reusing that build directory for development, configure with
`-DROLLNW_RELEASE_TAG=` to clear the cached release request.

Stable identity requires all of the following: a clean Git checkout (including
untracked files/submodule changes), full history, the exact tag at HEAD, a
matching schema-1 manifest, matching tool versions, and no reused ancestor
release version. Invalid input fails configuration/build. Fetch all tags before
preparing or reproducing a release; a non-shallow clone can still be missing tags.

The dated GitHub **release-published** event starts the existing package workflow.
Package jobs depend on the test/sanitizer gates, and asset publication also waits
for the renderer/client smoke job. They build the tagged revision,
extract the packages, and run every included executable's `--version` and
`--build-info` against its packaged build record before uploading. The final job
archives those same verified install trees and attaches assets plus SHA256SUMS.
Creating a Git tag alone does not publish these distribution assets. The release
entry may be visible while its builds run; successful asset publication is gated.

## VS Code extension

The extension keeps its own `tools/vscode-smalls/package.json` version. Prepare
its matching tag with exactly the LSP approval:

```sh
cmake -DROLLNW_RELEASE_TAG=vscode-smalls/0.0.2 \
  -DROLLNW_RELEASE_TOOLS=smalls-lsp \
  -P scripts/ci/prepare_release.cmake
```

Commit and tag as above. The existing extension workflow builds and verifies
each native LSP binary, bundles its build record beside it, and checks the VSIX
contains matching records for all platforms and the extension package version.
This workflow produces a VSIX artifact; it does **not** publish to Marketplace.
The other tools do not become releases as a side effect of an extension tag.

## Identity data contract

`cmake/BuildIdentity.cmake` transforms the fixed batch of five numeric VERSION.txt
files, one checkout identity, the extension package version, and an optional
release approval into build-owned headers, Windows resources, and
`tool_versions/build-identity.json`. `release.json` contains exactly `schema`
(number `1`), `tag` (string), and `tools` (object mapping approved tool names to
exact numeric versions). The initial empty manifest approves nothing.

The generated record has schema `1`, full `revision`, `source_state`
(`clean`, `dirty`, or `unknown`), `release_tag`, `snapshot` (date tag or empty),
`extension_version`, and a `tools` object. Each tool entry includes those source
fields plus `tool`, `base_version`, `version`, and `channel` (`development` or
`release`). `--build-info` emits that tool entry. The record is a catalog of all
five identities; the platform package's explicit tool list determines which
executables must be present (macOS currently ships only the three Smalls tools).

Examples of generated versions:

- Known development revision: `0.5.0-dev+g<12-character-revision>`.
- Locally changed source: the same version with `.dirty` appended.
- Source archive without Git: `0.5.0-dev+unknown`.
- Explicitly approved release: `0.5.0`, with revision retained in build info.

Git-less source archives can build but cannot certify a release. A tag without
`ROLLNW_RELEASE_TAG` still builds as development. The build refreshes identities
once before dependent targets compile and preserves timestamps when bytes match.
There are no runtime Git calls, date-clock stamping, library API additions, or
implicit network fetches. Do not edit source while building a release. For bug
reports, retain the packaged build record and checksums; `.dirty` is a warning,
not a reproducible description of local edits.

## Verification

```sh
ctest --test-dir build --output-on-failure \
  -R '^(build-identity|(rollnw-client|mudl|smalls|smalls-lsp|smalls-datagen)-version)$'
cmake -DPACKAGE_ROOT=/path/to/extracted/package \
  '-DTOOLS=mudl;rollnw-client;smalls;smalls-lsp;smalls-datagen' \
  -P scripts/ci/validate_tool_packages.cmake
```

Fixture tests create isolated Git repositories and compile/install small probes
to cover development, dirty, archive, release, incremental refresh, manifest and
version errors, and independent extension releases. They do not create tags in
the working repository or exercise external publication services.
