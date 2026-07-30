# Releasing rollNW

rollNW repository releases are dated snapshots. Independently shipped
packages keep their own versions; a snapshot date is not a package version.

## Snapshot tags

Use the UTC publication date:

```text
YYYY.MM.DD
```

For a second snapshot on the same UTC day, append a sequence number starting
at 2:

```text
YYYY.MM.DD.2
```

The release workflow rejects malformed tags, impossible calendar dates, and a
sequence suffix below 2. Use GitHub's prerelease flag when a snapshot is not
ready to be presented as the current release; do not encode alpha or RC state
in the tag.

## Package versions

Each package version comes from the package's own data:

- The C++ API version is the CMake `PROJECT_VERSION`.
- The VS Code extension version is
  `tools/vscode-smalls/package.json::version`. Its release tag is
  `vscode-smalls/<version>`.
- Smalls packages use the `version` in each package's `package.json`.

The tools bundle belongs to the rollNW snapshot and therefore uses the
snapshot date directly. For example, snapshot `2026.07.29` attaches
`rollnw-tools-linux-x64-2026.07.29.tar.gz`. Tools archives produced by
non-release CI runs use the exact commit SHA instead. Publishing a snapshot
must not rewrite an independently versioned package manifest or substitute
the date for one of those package versions.

## Snapshot procedure

1. Freeze and push the exact snapshot commit.
2. Wait for the CI and VS Code extension workflows to pass for that commit.
3. Exercise the generated tools archive and VSIX rather than local build
   outputs.
4. Create the dated tag and publish the GitHub release.
5. Verify that the tools asset names match the snapshot date and that
   independently versioned package artifacts match their manifests.
