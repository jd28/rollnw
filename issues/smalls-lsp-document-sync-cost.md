# Smalls LSP Document Sync Cost

## Problem

One keystroke costs a multiple of one compile, and the multiplier is the
number of open documents.

`publish_diagnostics` calls `Runtime::evict_modules` for the edited module and
then `Runtime::evict_user_modules`, which evicts every cached module whose
name does not begin with `core.`, transitively including dependents.
`handle_did_change` then runs `publish_diagnostics` for the edited document
and for every other open document in turn. Each pass begins by evicting what
the previous pass just built, so with K open documents a single keystroke
performs K full parse-and-resolve passes and retains only the last one.

The consequence outlives the edit. Because the cache is empty for every open
document except the one processed last, `get_or_load_module` misses on
essentially every subsequent request, so hover, completion, definition, and
semantic tokens each recompile the file and its user-module import closure.

Three costs compound on the same path:

- Document sync is full-text (`change == 1`). Every keystroke in the 117 KB
  `nwn1/combat.smalls` copies and reparses 117 KB.
- Every `publish_diagnostics` ends with a `workspace/semanticTokens/refresh`
  notification, so K notifications per keystroke each provoke a full-file
  token recomputation from a cold cache.
- `workspace/didChangeWatchedFiles` runs the same K-document fan-out, and the
  client watches `**/*.smalls` across 2,192 files.

`module_name_for_uri` runs `weakly_canonical` on the target file and on every
module path, and is called by all six feature handlers.
`handle_definition` calls it inside a loop over open documents.
`publish_diagnostics` additionally calls `module_root_for_uri`, which walks to
the filesystem root testing for `package.json` at each level, and
`Runtime::add_module_path`, which stats and canonicalizes. All of this is on
the keystroke path.

## Direction

An edit to one document invalidates that module and its actual dependents,
and nothing else. Removing the blanket `evict_user_modules` is the single
change that makes the cache useful; it exists to avoid stale on-disk reads
for open buffers, which is a narrower problem solvable by keying open buffers
ahead of disk in module resolution.

Diagnostics for unedited documents are republished only when the edit
actually changed something they depend on. Dependency direction is already
recoverable from `Script::dependencies`.

Adopt incremental sync (`change == 2`). The server owns the buffer, so it
applies each range edit in order and stores the result; this also removes the
per-keystroke full copy.

Coalesce edits. A burst of `didChange` notifications should produce one
diagnostics pass, not one per notification, and one
`workspace/semanticTokens/refresh`, not K.

Cache the URI-to-module-name mapping. It is a function of the URI and the
module path list, both of which change rarely, and it is currently recomputed
with filesystem syscalls on every request.

## Required decision

Whether the coalescing window is a timer or a queue-drain check. A drain
check — process pending input before starting analysis, and restart if a
newer version arrived — needs no timer and no second thread, and should be
measured before a timer is introduced.

## Done

- `evict_user_modules` is no longer called from the diagnostics path, and
  open buffers still take precedence over on-disk content. A test opens a
  buffer that shadows a disk module and asserts the buffer wins.
- A keystroke in a document with K others open performs one compile, not K.
  Measured and recorded here for K in {1, 5, 20}.
- Incremental sync is implemented; a fixture of range edits including
  multi-byte characters, line splits, and joins reproduces the same buffer as
  the equivalent full-text sync.
- A burst of N `didChange` notifications yields one diagnostics pass and one
  semantic-token refresh.
- URI-to-module-name resolution performs no filesystem syscall on a cache hit.
- Edit-to-diagnostic latency for `nwn1/combat.smalls` is measured before and
  after and recorded here.
