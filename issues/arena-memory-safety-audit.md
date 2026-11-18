# Arena/memory-safety audit: findings and fix plan

Status (2026-11-18): **finding #4 (Dialog) fixed**; #1/#2/#3/#5 still plan
only. Grew out
of [the `load_config!`/`ResourceManager` investigation](smalls-load-config-early-call-corruption.md)
(fixed, separately) — fixing that surfaced a question worth answering
properly: does this engine's arena/scope memory model actually destruct
everything it should, or does the module-load/unload cycle quietly leak?
Swept the whole codebase for the answer. See
[[project_resource_manager_lifecycle]] for the background on why services get
torn down and rebuilt wholesale on every module load — that design is sound
and stays; this doc is about what's *built on top of* it correctly vs. not.

## The three patterns already in the codebase, and the one anti-pattern

Three ways non-trivial, heap-owning objects (anything with a `String`,
`Vector`, map, or similar member) get their destructors run correctly today:

1. **`MemoryScope::alloc_obj<T>()`** (`lib/nw/util/memory.hpp:143-154`):
   registers a destructor finalizer per object; `reset()`
   (`lib/nw/util/memory.cpp:230-242`) walks the list calling each one, in
   reverse construction order, before reclaiming the arena. Correct for
   *cohort-lifetime* data — many objects that are all born and all die
   together at one well-defined point (a kernel `Service`, an AST built
   during one parse/compile pass). Confirmed correct and in active use for
   every kernel `Service` (`Kernel.hpp:164`) and both language ASTs
   (`script/Ast.hpp:550`, `smalls/Ast.hpp:782`).
2. **`unique_ptr` + a custom deleter that calls the destructor explicitly**:
   `ContainerDeleter` (`lib/nw/resources/Container.hpp:48-62`) does
   `c->~Container(); allocator->deallocate(c);`. Correct for *individually-
   owned* objects with an independent lifetime, regardless of which arena
   backs their storage. Confirmed correct and consistently used for every
   `Container` (`StaticDirectory`/`StaticErf`/`StaticZip`/`StaticKey`).
3. **One owned buffer, read-only views over it** — `StaticTwoDA`
   (`lib/nw/formats/StaticTwoDA.hpp`): owns one `ResourceData data_;`, parses
   `Vector<StringView> rows_;` as *views into that same buffer* — zero
   per-cell allocation, nothing to individually free, correct by
   construction once loaded. Contrast with the separate, deliberately
   different `TwoDA` (`lib/nw/formats/TwoDA.hpp`), which owns real per-cell
   `std::vector<RowType>` storage specifically because *it* supports editing
   (`rows_[row][col] = value;`). This is the existing precedent for "don't
   use one representation for both editing and read-only bulk access" —
   worth following elsewhere, not just citing.

**The anti-pattern**: a non-trivial object placement-new'd directly against a
raw `MemoryArena::allocate()` (no finalizer support at all — `MemoryArena`
has none, only `MemoryScope` does), *and* nothing else ever explicitly calls
its destructor before the arena goes away. Distinct from, but related to, a
second failure shape: a *cohort teardown* path that assumes "everything's
getting nuked, no need to tear down individual objects" without checking that
assumption holds for every object kind involved — it only holds for types
with trivial destructors, and `ObjectBase`-derived types aren't.

## Findings, ranked by impact

Verified by direct code reading (this pass) plus an exhaustive codebase sweep
(delegated, same methodology) for every other raw `allocate()`+placement-new
site. Full sweep output retained in this session; citations below are the
sweep's.

| # | What leaks | Where | Trigger frequency | Root cause |
| - | - | - | - | - |
| 1 | Every live `Area`/`Creature`/`Door`/`Encounter`/`Item`/`Store`/`Placeable`/`Player`/`Sound`/`Trigger`/`Waypoint` still alive at unload | `ObjectPool<T>::clear()` (`lib/nw/util/memory.hpp:244-255`), reached via `unload_module()` (`lib/nw/kernel/Kernel.cpp:468`) | **Every module switch**, not once per process | `clear()` frees pool chunks without first walking live slots to call `~T()`. `ObjectPool<T>::free()` (used for normal individual destroy) does this correctly — `clear()` (used only at bulk teardown) doesn't. |
| 2 | Every live `Effect`/`VariantObject`'s `String` payload | `RuntimeObjectPool`/`TypedHandlePool` (`lib/nw/util/HandlePool.cpp:22`, used by `EffectSystem`/`smalls::Runtime`, both kernel `Service`s) | Every module switch; likely **highest real-world volume** — effects vastly outnumber and outchurn whole objects | `TypedHandlePool::~TypedHandlePool()` is explicitly documented as "caller is responsible for destroying object contents before pool destruction" — nobody is, on the bulk-teardown path. |
| 3 | The `Module` object itself | `ObjectManager.cpp:71` vs. `unload_module()`'s explicit comment *"Since everything is getting nuked, it's not necessary to call Module::destroy"* | Every module switch, once | The stated assumption is false for any non-trivially-destructible type, which `Module`/`ObjectBase` are. Also bypasses `alloc_obj` entirely (raw `allocate()`), so even fixing the assumption needs an explicit destructor call, not just routing through the existing finalizer. |
| 4 | `DialogNode`/`DialogPtr` (`String`×several, `LocString`, `Vector` members) | `Dialog::arena_`, a bare `MemoryArena` (`lib/nw/formats/Dialog.cpp:239,248`) | Per `.dlg` load; **also unbounded growth during editing** — `delete_node()`/`delete_ptr()` already call the destructor correctly, but the arena slot is never reclaimed (`MemoryArena::deallocate()` is a no-op), so an editing session's normal add/delete/undo/redo churn only ever grows memory, on top of nodes that are simply never explicitly deleted (most of them) leaking on `Dialog` destruction regardless | Wrong allocator model for the access pattern: `Dialog` is individually-churned, mutable, edited data, not cohort-lifetime batch data. See Direction below — this is the one where the fix isn't "use `alloc_obj`," it's "don't use an arena here at all." |
| 5 | `PaletteTreeNode` below depth 1 (`String name`, `String faction`) | `Palette::~Palette()`/`PaletteTreeNode::clear()` (`lib/nw/formats/Palette.cpp:88-91`) only recurse one level into `children` | Once per toolset session (blueprint palette load) — **lowest priority**: small, infrequent, editor-only, non-gameplay-critical | One-level-only recursion bug, unrelated to the arena question — a plain tree-walk fix. |

## Direction

Two different fix shapes, not one, because two different access patterns are
involved:

- **#1–#3 (game objects, effects, `Module`) are correctly individually-owned
  already** — `ObjectPool<T>::free()`/normal per-handle `destroy()` paths are
  right for a gameplay session's individual churn. The bug is narrowly that
  the *bulk* teardown path (`unload_module()`) skips calling them. Fix there:
  make `unload_module()` (or `ObjectPool<T>::clear()`/`TypedHandlePool`'s
  destructor itself) walk live slots and destruct before the arena goes away
  — i.e., extend the existing, working per-object teardown logic to also run
  during bulk teardown, rather than assuming bulk implies skip-safe. Small,
  mechanical, no new abstraction needed; same shape as `ObjectPool<T>::free()`
  already has, just invoked in a loop instead of per-handle.
- **#4 (Dialog) — FIXED (2026-11-18), dropped the arena.** `Dialog` is edited
  interactively — individual node add/delete/undo/redo is the normal access
  pattern, not a cohort that's born and dies together. An arena (bump
  allocator, no individual reclaim) was the wrong tool regardless of whether
  it was wrapped in a `MemoryScope`/`alloc_obj` or not — that would have fixed
  the "undeleted nodes leak on `Dialog` destruction" half but not the
  "editing churn grows memory forever" half (`delete_node`/`delete_ptr`
  already called the destructor explicitly before this fix; the arena slot
  just never reused regardless). Shipped: `create_node`/`create_ptr` now
  plain `new`; `delete_node`/`delete_ptr` now plain `delete`; added a real
  `~Dialog()` that flat-walks `starts` + every node's `pointers` + `entries`/
  `replies` exactly once (independent of `delete_node`/`delete_ptr`'s
  recursive single-call semantics, so it can't double-delete regardless of
  link structure). Also fixed a related bug found while doing this:
  `delete_ptr()` returned early for link pointers *before* deleting the link
  wrapper itself — correct to skip recursing into a link's shared target, but
  it leaked the link `DialogPtr` object every time. Now deletes the pointer
  unconditionally and only conditions the recursion on `is_link`.

  ASan/UBSan caught three more real leaks the plain-build test pass couldn't
  (leaking never crashes or fails an assertion — exactly why these went
  unnoticed under the old arena, where they were silently absorbed by the
  bulk block free): (1) `Dialog.InternalNodeOps` created two nodes, removed
  them via `remove_node_internal()`, and never deleted them — fixed in the
  test, since `remove_*` is a deliberate *untrack, not destroy* operation
  (callers do remove-then-re-add elsewhere in the same test) so the caller
  owns the delete. (2) `Dialog.PtrAddRemove` and `Dialog.RemoveLinkedNode`
  had the same shape — fixed the same way (capture, remove, then explicit
  `delete_ptr`). (3) A real bug in `Dialog.cpp` itself, not just tests:
  `remove_node_internal()` and `Dialog::remove_ptr()` cascade-remove any
  *link* pointer still referencing a node once that node is gone — but never
  delete the cascaded link, and unlike the node itself, the caller is never
  handed a pointer to it to clean up later. Fixed by deleting the link at the
  point of cascade removal (no mechanism re-links a dangling link later, so
  nothing is lost by not keeping it around). Verified: all 17
  `Dialog.*`/`ClientDialogDocument.*` tests pass under both the normal build
  and ASan/UBSan (`ci-linux-clang-asan-ubsan` preset), zero `Dialog.cpp`
  frames in the leak report; full 1834-test suite run to confirm no
  unrelated regression.
- **#5 (Palette) is unrelated to the arena question** — fix the recursion to
  walk the whole tree, not just depth 1.

**Considered, not adopted — `StaticDialog`.** Checked actual usage first:
`Dialog` is used *only* by the toolset editor today (`tools/client/dialog_document.cpp`,
`project.cpp`); no runtime playback path exists anywhere in the codebase (the
"conversation" fields elsewhere are just a `ResRef` naming which `.dlg` to
use, not execution — matches the Drafting Table's Phase 3, "event loop and a
conversation engine," being unbuilt). So there is genuinely nothing for a
`StaticTwoDA`-style read-only `StaticDialog` to serve yet — not just "no
cohort-lifetime workload," no consumer at all.

The sharper reason to revisit this *when* that engine gets built, raised
during this work: it's not really about single-conversation read performance
(parsing a `.dlg` once and walking pointers is cheap regardless of
allocator) — it's about **sharing** in a multiplayer context. If N players
are simultaneously in conversations using the same `.dlg` resource, you want
one shared, read-only load of that tree (cached/ref-counted by resref, same
idea `StaticTwoDA` already uses for 2DA data), with each active conversation
holding only a small per-player cursor (current node, condition results)
into the shared tree — not N independent copies of the whole thing. That's
the real `StaticDialog` case, and it only exists once there's a conversation
engine with concurrent players to share across. Recorded here so the
precedent and the reasoning aren't lost when that work starts.

## General rule going forward

Before choosing a representation for new or changed data:
- **Individually churned, mutated, or deleted one at a time?** Use plain
  ownership (`new`/`delete`, `unique_ptr`, or an existing pool with a correct
  per-handle `free()`) — never a raw arena for this shape, regardless of how
  tempting the locality looks.
- **Born and destroyed together as a batch, read far more than written?**
  An arena is right — but only via `MemoryScope::alloc_obj<T>()` (gets
  destructor correctness for free) or a `StaticTwoDA`-style single-buffer
  zero-copy view (gets correctness by never separately allocating per-element
  in the first place). Never raw `MemoryArena::allocate()` + placement-new
  for anything with a non-trivial destructor.
- **Any "bulk teardown, skip the individual work" path** (module unload,
  scene teardown, etc.) — the assumption is only safe for trivially
  destructible types. If it's applied to anything `ObjectBase`-derived or
  otherwise non-trivial, it needs to be verified, not assumed, the same way
  #1–#3 here were assumed safe and weren't.

## Build sequence

1. ~~`Dialog`: drop `arena_`, switch node/ptr allocation to plain `new`/
   `delete`.~~ **Done.** Confirms point 6 below in practice: the plain test
   run showed nothing (all green) until re-run under ASan, which caught 3
   more real leaks the arena had been silently absorbing. Budget ASan
   verification time for 2–4 as well, not just a green `rollnw_test` run.
2. `ObjectPool<T>::clear()`: walk live slots calling `~T()` before freeing
   pool chunks — fixes #1 at the pool level, reusable by whatever currently
   relies on the "everything's getting nuked" assumption.
3. `TypedHandlePool`: same shape for #2 — give it a real destructor instead
   of the "caller's responsibility" comment, or make `RuntimeObjectPool`
   responsible for walking its own live handles before the pool beneath it
   is torn down.
4. `unload_module()`: once 2–3 exist, remove the now-incorrect "no need to
   call `Module::destroy`" shortcut (or confirm it's now safe because the
   pools underneath handle it) and explicitly destruct `Module` itself (#3).
5. `Palette`: fix the one-level-only recursion in `~Palette()`/
   `PaletteTreeNode::clear()`.
6. Regression: for 2–4 specifically, a real test that creates several live
   objects/effects of different types, unloads the module, and verifies (via
   an instrumented counting allocator, or ASan leak detection if already
   wired into CI) that their destructors actually ran — the existing test
   suite passing is not evidence here, since these leaks don't crash or fail
   assertions, they just leak, which is exactly why they went unnoticed.
