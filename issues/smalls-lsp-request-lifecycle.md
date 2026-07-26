# Smalls LSP Request Lifecycle

## Problem

The server does not distinguish a request from a notification, cannot be
told to stop working, and has no client-visible log channel.

`nlohmann::json`'s const `operator[]` guards a missing key with `JSON_ASSERT`,
which compiles out under `NDEBUG` and then dereferences the object's end
iterator. Six handlers index `req["id"]` without first proving the key
exists: `handle_hover`, `handle_definition`, `handle_completion`,
`handle_signature_help`, `handle_inlay_hints`, and `handle_semantic_tokens`.
`handle_request` dispatches to all six without the `contains("id")` check
that the `initialize` and `shutdown` branches perform. A notification-shaped
`textDocument/hover` therefore reads out of bounds in a Release build.

The same handlers use `req.value("id", json(nullptr))` on their invalid-input
paths, which emits a JSON-RPC response carrying `id: null`. A response to a
notification is not valid JSON-RPC; the message must be dropped instead.

`$/cancelRequest` has no `id`, so it falls through the final
`else if (req.contains("id"))` arm and is discarded while the cancelled work
still runs to completion. There is no `$/progress`, so any operation longer
than a frame is indistinguishable from a hang.

Diagnostics go to raw `stderr`. Three unconditional `std::cerr` writes sit in
the completion path and run on every keystroke in the shipped binary.

## Direction

Separate the three JSON-RPC message shapes at dispatch instead of inside each
handler. A request has an `id` and must receive exactly one response. A
notification has no `id` and must receive none. A malformed message with an
`id` receives an error; a malformed message without one is logged and
dropped.

Carry the extracted `id` into handlers as a value the handler cannot get
wrong, rather than having each handler re-read `req["id"]`.

Cancellation is observable, not preemptive: the server is single-threaded, so
record cancelled ids in a set, check it at handler entry and at the coarse
loop boundaries inside long walks, and answer a cancelled request with error
`-32800` (`RequestCancelled`). This is honest about what the current
threading model can deliver and does not require the request queue that
`smalls-lsp-document-sync-cost.md` covers.

Replace `stderr` diagnostics with `window/logMessage`, gated on a level set by
`$/setTrace`. Keep `stderr` only for failures that occur before the connection
is usable, such as frame decode errors.

## Done

- No handler indexes `req["id"]`; the id is extracted once at dispatch.
- A notification-shaped `textDocument/hover`, `definition`, `completion`,
  `signatureHelp`, `inlayHint`, and `semanticTokens/full` each produce no
  response and no out-of-bounds read. A test drives all six over the real
  frame reader.
- No JSON-RPC response is ever emitted with `id: null`.
- `$/cancelRequest` marks the id; a request cancelled before it is serviced
  answers `-32800` and performs no AST work.
- `$/setTrace` is honored and `window/logMessage` replaces the completion
  path's `std::cerr` writes.
- `initialize` reports `serverInfo.version`.
- An ASan run over the protocol test suite is clean.
