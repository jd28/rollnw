# Smalls LSP workspace navigation

## Observed data

- The runtime resolves imports and retains declaration-provider relationships,
  but the language server has no workspace symbol/reference index.
- The repository contains 2,192 `.smalls` files and about 1.27 MB of source.
- Rename and references must not return only the current document while
  presenting themselves as workspace-complete operations.

## Required decision

Measure startup and edit latency for a flat index keyed by stable module name
and declaration identity. The index input is each module's declarations and
resolved identifier uses; its output supports workspace symbols, references,
document highlights, and validated rename edits.

## Done

- Index ownership, invalidation, and module-version contracts are explicit.
- References and rename cover open buffers and on-disk dependencies.
- Duplicate/shadowed names are distinguished by resolved declaration identity.
- Corpus-scale latency and memory are measured before advertising the features.
