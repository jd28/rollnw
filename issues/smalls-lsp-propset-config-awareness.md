# Smalls LSP Propset and Config Awareness

## Problem

`load_config!(T)` reads a config file at compile time and produces typed data;
`smalls-datagen` generates those config files from 2da tables; propsets are
the runtime's property lookup path. The language server knows none of this.

A config file is plain data with no imports and no declarations, so the server
currently cannot even tell it apart from a broken source file — this is the
root of the `looks_like_config_file` suppression described in
`smalls-lsp-diagnostic-payload.md`, which discards real diagnostics from real
source files as collateral.

Concretely, none of the following works: completing a field name inside a
config file against the `T` it will be loaded as; jumping from a config entry
to the field declaration in `T`; jumping from a `load_config!(T)` call to the
config file it reads; a diagnostic when a config file has a field `T` does not
declare, a type mismatch, or a missing required field; or hovering a config
entry to see its declared type.

This is a class of error the compiler catches late and reports against the
config file with no editor support, in a corpus where the config data is
machine-generated from 2da tables and is large.

## Direction

This is the differentiating feature in the roadmap. Editor support for a
project's own data-definition pipeline is not something a general-purpose
language server can provide, and the pipeline already exists here — the type
`T`, the config file format, and the generator are all in-repo.

Model the config file as a document whose type is determined by its
`load_config!` call sites. That relationship is the whole feature: a config
file has no intrinsic type, so the server must resolve which `T` it is loaded
as before it can say anything about it. Where a file is loaded as more than
one type, that is either an error to report or a union to reconcile, and the
answer must be stated rather than discovered at runtime.

Once the file-to-type binding exists, completion, hover, definition, and
diagnostics inside config files all follow from `T`'s declaration and need no
new machinery.

Register the config file's own extension as a language contribution so the
extension can attach the server to it deliberately, rather than the server
inferring file identity from parse failure.

`smalls-datagen` should be the source of truth for which 2da column backs
which field, so hovering a generated field can name its origin table and
column. That provenance is the part a user cannot reconstruct by reading the
source.

## Blocks workspace diagnostics (2026-07-27)

`workspace/diagnostic` is implemented and measured: a full pass over the corpus
takes 0.37 s and 108 MiB, and a repeat poll answers 2,192 `unchanged` in
0.18 s. It is not advertised, because that pass reports 4,222 syntax errors
across 2,117 files, essentially all of them 2da-generated config data that is
not Smalls source. The cost is fine; the file identity is missing.

Flipping `workspaceDiagnostics` to true is a one-line change once config files
declare what they are.

## Required decision

Whether a config file's type binding is declared in the file itself — a header
naming `T` — or inferred from `load_config!` call sites across the workspace.
Inference requires the index and breaks when a file has no caller yet; a
declared header is explicit, verifiable in isolation, and greppable. Decide
before building the provider, because every feature above depends on it.

## Done

- Config files are identified by declared identity, never by parse failure,
  and the `looks_like_config_file` suppression is gone.
- The file-to-type binding rule is decided, documented, and tested for the
  no-caller and multiple-caller cases.
- Completion inside a config file offers `T`'s fields with their types.
- Go-to-definition from a config entry lands on the field declaration in `T`,
  and from `load_config!(T)` lands on the config file.
- Unknown fields, type mismatches, and missing required fields are reported in
  the config file with correct ranges.
- Hover on a generated field names its source 2da table and column.
- Diagnostics over the full generated config corpus are measured and recorded
  here.
