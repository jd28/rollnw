# Smalls LSP file URI handling

## Observed data

- VS Code sends RFC 8089-style `file` URIs, while the runtime module APIs take
  native filesystem paths.
- The current boundary only removes or adds the literal `file://` prefix.
- Percent-encoded bytes, authorities, and Windows drive/UNC forms are not
  decoded or encoded.

## Required decision

Select or implement one URI-to-native-path transform with explicit POSIX and
Windows contracts. Do not infer path policy from a URI string prefix.

## Done

- Spaces, `#`, `%`, non-ASCII path bytes, POSIX paths, Windows drive paths, and
  UNC authorities have round-trip tests.
- Invalid percent escapes and unsupported URI schemes are rejected.
- Module-name lookup and definition locations use the same transform.
