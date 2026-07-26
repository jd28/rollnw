# Smalls language server

`smalls-lsp` is a single-threaded Language Server Protocol process. It reads
UTF-8 JSON-RPC messages from standard input and writes framed JSON-RPC messages
to standard output. Diagnostics and semantic features reuse the Smalls parser,
resolver, AST, and runtime.

The transport requires one positive, decimal `Content-Length` per frame and an
exactly sized body. A malformed, duplicate, oversized, or truncated frame is
logged and closes the connection because its next message boundary cannot be
recovered safely. Requests before `initialize`, duplicate initialization,
unknown request methods, stale document versions, and malformed request
parameters receive the corresponding JSON-RPC/LSP error where a request ID is
available.

## Document transform contract

The server owns one `OpenDocument` value per open URI:

```text
{ UTF-8 source text, monotonically increasing client version }
```

`didOpen` creates that value and rejects duplicate opens. The server advertises
full document synchronization, so a valid `didChange` batch contains one or
more full-text changes without a `range`. The changes are validated in order
and only the last complete text is committed. Empty batches, ranged changes,
missing text, unopened documents, and stale versions are rejected without
modifying the owned document. `didClose` removes the value and clears its
published diagnostics.

The Smalls runtime copies committed text into the corresponding cached
`Script`. Before recompilation, the server evicts that exact normalized module,
its cached transitive dependents, and ordinary user modules. Open `core.*`
files therefore use the current buffer instead of the runtime's normally
persistent core cache. The open-document buffer and compiled script have
independent ownership and request-safe lifetimes.

## Current capabilities

- Diagnostics
- Completion, including module and struct-member completion
- Hover and go to definition
- Signature help
- Parameter-name inlay hints
- Full semantic tokens

Source positions inside the Smalls AST are one-based lines with UTF-8 byte
columns. At initialization the server selects UTF-8 when the client offers it;
otherwise it uses the LSP default, UTF-16. Incoming hover, completion,
definition, signature-help, and inlay-range positions are converted to byte
columns before AST queries. Outgoing diagnostics, definitions, semantic
tokens, and inlay hints are converted to the selected encoding. Invalid UTF-8,
out-of-range positions, and positions that split a code point or UTF-16
surrogate pair are rejected at this boundary.

## Cost model

The common full-sync edit copies the new source once, evicts user modules, and
runs the existing parse/resolve pass. Repository scripts average 578 bytes; the
largest observed script is about 117 KB. Open-document memory is linear in the
sum of open source sizes. Position conversion locates the affected line and
scans its relevant prefix once without allocation. No background threads or
workspace-wide index are maintained. One input buffer is retained and reused;
its capacity is the largest framed message seen during the server lifetime.
