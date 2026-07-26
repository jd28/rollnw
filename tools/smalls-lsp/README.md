# Smalls language server

`smalls-lsp` is a single-threaded Language Server Protocol process. It reads
UTF-8 JSON-RPC messages from standard input and writes framed JSON-RPC messages
to standard output. Diagnostics and semantic features reuse the Smalls parser,
resolver, AST, and runtime.

The transport requires one positive, decimal `Content-Length` per frame and an
exactly sized body. A malformed, duplicate, oversized, or truncated frame is
logged and closes the connection because its next message boundary cannot be
recovered safely.

## Message shapes

A request carries an `id` and receives exactly one response. A notification
carries none and receives none: a reply with `id: null` is not valid JSON-RPC,
so a notification-shaped `textDocument/hover` is dropped rather than answered.
The id is extracted once at dispatch and never re-read inside a handler.

Requests before `initialize`, duplicate initialization, unknown methods, stale
document versions, and malformed parameters receive the corresponding
JSON-RPC/LSP error whenever a request id is available.

`$/cancelRequest` records the id; a request cancelled before it is serviced
answers `-32800` and performs no AST work. The server is single-threaded, so a
request already in flight runs to completion.

`$/setTrace` selects the `window/logMessage` level. Standard error carries only
failures that occur before the connection is usable, such as frame decode
errors.

## Document transform contract

The server owns one document per open URI: its UTF-8 text, the client's
monotonically increasing version, and the module names it imported at its last
compile.

`didOpen` creates that value and rejects duplicate opens. Sync is incremental:
a `didChange` batch is applied in order to a copy, and the copy is committed
only if every change applied, so a rejected batch cannot leave a partially
edited buffer behind. A change without a `range` replaces the whole document.
Empty batches, malformed ranges, ranges outside the document, missing text,
unopened documents, and stale versions are rejected without modifying the owned
document. `didClose` removes the value and clears its diagnostics.

## Document URIs

`lsp_uri.hpp` owns the single transform between an RFC 8089 `file` URI and a
native path. Nothing else parses a URI, and no path policy is inferred from a
string prefix.

- The scheme must be `file`, compared case-insensitively. Any other scheme is
  rejected, so `untitled:` and virtual filesystems are treated as "not on disk"
  rather than becoming a bogus path.
- An empty authority or `localhost` is the local machine. Any other authority is
  a UNC host: on Windows it becomes `\\host\share\...`, and on POSIX it is
  rejected because there is no way to express it.
- Percent escapes must be `%` followed by two hex digits; an invalid escape is
  rejected rather than passed through.
- `?` and `#` terminate the path per RFC 3986, so a file name containing `#`
  must arrive as `%23`. Encoding produces that form.
- On Windows the leading `/` before a drive letter is dropped, the legacy `C|`
  form is accepted, and `/` becomes `\`.

The transform is parameterized by `PathStyle` rather than compiled per platform,
so the Windows contract is covered by tests on a POSIX build.

## Module search path

When a file is reachable from more than one module path, the most specific root
wins. Preferring the shallowest would name `<repo>/lib/nw/smalls/scripts/core/
array.smalls` as `lib.nw.smalls.scripts.core.array`, which matches no import and
no native module registration, so every `[[native]]` declaration in it reports
as unregistered.

A module path must be a *package directory*: `add_module_path` registers it as
a flat directory of sources, so the path has to be the directory that directly
contains the `.smalls` files. A package marks itself with a `package.json`, and
the package name comes from that directory. `<root>/core/array.smalls` is
module `core.array`, so `<root>/core` is what gets registered, not `<root>`.
Passing the parent silently resolves nothing.

Paths are applied in this order:

1. `modulePaths` from `initializationOptions` and from
   `workspace/didChangeConfiguration`
2. workspace folders reported at `initialize` and by
   `workspace/didChangeWorkspaceFolders`
3. `-I` / `--module-path` command-line arguments
4. the directory containing a `package.json` above an open document

A configuration change re-resolves modules and republishes diagnostics for open
documents without a restart.

## Capabilities

- Diagnostics
- Completion, including module and struct-member completion, with
  `completionItem/resolve`, explicit replace ranges, relevance ordering, and
  snippet call insertion
- Hover, carrying the range of the identifier it resolved
- Go to definition, declaration, and type definition, returning `LocationLink`
  when the client advertises `linkSupport`
- Signature help
- Inlay hints: parameter names, inferred `var` and foreach binding types, and
  lambda return types, each switchable on its own and clickable through to the
  declaration they name
- Document symbols, folding ranges, and selection ranges
- Semantic tokens: full, delta, and range
- Quick fixes: remove an unused import, change a name to a ranked near-match,
  import a name from a module that exports it, and fill in the fields a brace
  initializer left out
- Pull diagnostics for a document, with result-id reuse

`workspace/symbol`, references, rename, and call hierarchy are deliberately not
advertised. They require the index tracked by
`issues/smalls-lsp-workspace-navigation.md`, and a rename that silently covers
only the open buffer corrupts source.

## AST traversal

Tooling visitors derive from `nw::smalls::WalkVisitor`, which descends into
every child and does nothing else. `NullVisitor` implements every `visit` as an
empty body, so a node kind a visitor does not override terminates the walk at
that node and every descendant is skipped. Deriving from `WalkVisitor` fails
safe: an unhandled node loses only itself, and a node kind added to
`BaseVisitor` later degrades rather than blanking a region of every file.

Do not build a new AST-walking provider on `NullVisitor`.

## Position encoding

Source positions inside the Smalls AST are one-based lines with UTF-8 byte
columns. At initialization the server selects UTF-8 when the client offers it;
otherwise it uses the LSP default, UTF-16. Incoming positions are converted to
byte columns before AST queries, and outgoing positions are converted back.
Invalid UTF-8, out-of-range positions, and positions that split a code point or
a UTF-16 surrogate pair are rejected at this boundary.

Identifier boundaries come from `is_identifier_char`, which matches the Smalls
lexer and therefore accepts `$`. Hover, definition, and completion must not
disagree with the lexer about where a name starts.

## Cost model

An edit invalidates the edited module and the cached modules that depend on it,
which is the set the edit can affect. Diagnostics are republished for the edited
document and for the open documents that import it.

Analysis is deferred while more input is already waiting, so a burst of edits
costs one pass rather than one per keystroke, and the client sees diagnostics
for the version it ended on. This is a drain check, not a timer: no window is
guessed at and no second thread exists. A request drains the queue before it is
serviced, so it never observes stale analysis, and whatever is still queued at
end of stream is flushed. `main.cpp` supplies the predicate by polling stdin,
because a stream's own buffer says nothing about bytes still in a pipe.

An incremental change moves the buffer out and restores it, rather than copying,
so a rejected batch still leaves the document untouched without paying a
whole-buffer copy per keystroke.

Evicting every user module per keystroke, as an earlier version did, left the
cache cold for every open document and made each subsequent hover, completion,
definition, and semantic-token request recompile the file and its user-module
import closure.

Measured 2026-07-27, Linux, Release, on `nwn1/combat.smalls`, the largest
script in the corpus at 117 KB. Latency is from sending an edit to receiving the
diagnostics carrying its version, which is what a user perceives.

| | Before | After |
| --- | ---: | ---: |
| One edit to its diagnostics | 37.5 ms | 3.7 ms |
| Analysis passes for a 20-edit burst | 20 | 1 |
| Compiles per edit, 20 unrelated documents open | 20 | 1 |

A hint that restates what is already written is suppressed: a `var` with a
written type gets no type hint, and an argument whose name already reads as the
parameter gets no parameter hint. That is the largest source of inlay-hint
noise, and a user who cannot switch a category off switches all of them off.

Add-import consults a narrow export index built once from every module under a
search path and dropped when that set changes. It answers "who exports this
name" and nothing else; references and rename need declaration identity and stay
gated on the full index.

A module path is indexed when it is added, so a file created afterwards is
invisible until the registry is rebuilt.

A full `workspace/diagnostic` pass over the 2,192-file corpus takes 0.37 s and
108 MiB, and a repeat poll answers every file `unchanged` in 0.18 s. It is
implemented but not advertised: the pass reports 4,222 syntax errors across
2,117 files, nearly all 2da-generated config data rather than Smalls source.
The blocker is file identity, not cost.

`Script::dependencies` reports direct imports only, so the republish set is
grown to a fixpoint. Testing one level deep leaves a transitive dependent
showing stale diagnostics after its cached module was evicted.

An edit is still a full reparse of the document, so cost is linear in file size.
That is comfortable at 117 KB and would not be at ten times that.

`completionItem/resolve` is implemented because it is the protocol-correct
split, not because it was measured to matter. At current corpus scale the
candidate set at a bare prefix is a handful of module aliases, so deferring
documentation is not measurable. It will matter as the stdlib grows.

URI-to-module-name resolution is cached and invalidated when the module path
list changes; resolving it canonicalizes the target and every module path, so
it must not run per request. A providing script is mapped back to a document
URI through the name the script reports, because the runtime normalizes
separators to dots and a provider's name is therefore never the URI it was
loaded from.

Repository scripts average 578 bytes; the largest is about 117 KB.
Open-document memory is linear in the sum of open source sizes. No background
threads or workspace-wide index are maintained. One input buffer is retained
and reused; its capacity is the largest framed message seen during the server
lifetime.
