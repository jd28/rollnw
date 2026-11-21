# Arena/memory-safety audit: findings and fix plan

Status (2026-11-18): **all findings fixed** (#1/#3 ObjectPool+Module, #2
TypedHandlePool/RuntimeObjectPool, #4 Dialog, #5 Palette). Grew out
of [the `load_config!`/`ResourceManager` investigation](../smalls-load-config-early-call-corruption.md)
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
- **#1 and #3 (game objects, `Module`) — FIXED (2026-11-18), turned out to be
  one fix, not two.** `ObjectPool<T>::clear()` (`lib/nw/util/memory.hpp`) now
  walks every chunk slot and calls `~T()` on anything not in the free list
  before handing chunks back to the allocator — but a snapshot of the free
  list taken once at the start goes stale, because `T`'s destructor can itself
  `free()` *other* slots in the same pool (an `Item`'s destructor destroying
  `Item`s nested in its own inventory); fixed by re-draining the free list
  immediately before testing each slot (`drain_new_frees()`), so a
  nested-destruction's `free()` call is visible to the rest of the loop
  instead of leaving the now-dead slot to be double-destructed.

  That alone wasn't enough: fixing the leak surfaced three latent
  destructor-ordering bugs, all from the same root cause — C++ destructs
  members in reverse declaration order, and several of this cleanup path's
  cross-references didn't respect that order:
  1. `Services::shutdown()` (`Kernel.cpp`) cleared the service lookup table
     `services_` *before* calling `kernel_scope_.reset()` — but a service's
     destructor (e.g. a `Creature`'s, cascading into releasing an equipped
     `Item`) can itself look up another still-"live" service
     (`nw::kernel::objects()`) mid-teardown; with the table already cleared
     this threw `"kernel: unable to load object service"` (SIGABRT). Fixed by
     resetting the scope first, clearing the table only after.
  2. `ObjectArray`'s pool members (`items_`, `areas_`, `creatures_`, ...) were
     declared in a fairly arbitrary order, so `items_` destructed *before*
     `creatures_`/`players_`/`stores_`/`placeables_` — but those types'
     destructors release their equipped/contained `Item`s through the normal
     handle path, which needs `items_` still alive. Fixed by declaring
     `items_` first (destructs last).
  3. `ObjectManager`'s `object_tag_map_` was declared *after* `objects_array_`
     — but tag lookups during the same nested item/creature teardown need the
     map still alive while objects are destructing. Fixed by declaring
     `object_tag_map_` before `objects_array_`.
  4. Even with ordering fixed, nothing had ever walked `ObjectArray`'s live
     handles and called `destroy()` on them before its pools implicitly
     destructed — `ObjectPool<T>::clear()`'s own slot-by-slot `~T()` calls
     don't go through the handle-validity path, so a container `Item`
     destructing mid-`clear()` and cascading into destroying a *contained*
     Item via the normal `ObjectArray::destroy()` path raced with `clear()`'s
     own iteration over the same pool, causing a double-destruction (crashed
     as a SIGSEGV through a corrupted vtable). Fixed with an explicit
     `ObjectArray::~ObjectArray()` that walks every slot in `object_array_`/
     `player_array_` and calls the existing `destroy(handle)` for each one
     *before* the pools below it destruct — `destroy()`'s own `valid(hndl)`
     guard (it nulls the slot immediately) makes this safe against the exact
     cascading-destruction case that broke `clear()`'s own bookkeeping.

  This closes #3 for free: `Module` is allocated through the same
  `objects_array_.alloc(ObjectType::module)` path as every other object type
  (confirmed by reading `make_module()` → `ObjectManager::make<T>()`), so it
  already gets a handle in `object_array_` and the new `~ObjectArray()` walk
  destructs it like anything else. The stale `unload_module()` comment
  ("since everything is getting nuked, it's not necessary to call
  Module::destroy") has been reworded to say why that's true now (it cascades
  through `~ObjectArray()`), rather than resting on the "bulk teardown, skip
  individual work" assumption this whole audit exists to distrust.

  Verified: `*Object*:*Module*:*Area*:*Creature*` (399 tests) and the full
  1834-test suite both pass clean under a normal build; same 399-test filter
  re-run clean under ASan/UBSan with zero crashes and zero leak-report frames
  inside `ObjectPool<T>::clear()`, the new `ObjectArray::~ObjectArray()`, or
  the reordered `Services::shutdown()` — confirming the fix doesn't leak and
  the three ordering bugs are gone. (ASan did report a large pre-existing
  leak volume dominated by the smalls VM/Runtime and a few tests that
  construct objects directly without going through `ObjectManager` at all —
  unrelated to this fix; the VM/Runtime volume is exactly what #2 below still
  needs to cover.)
- **#2 (`Effect`/`VariantObject`/unmanaged-array payloads) — FIXED
  (2026-11-18).** `TypedHandlePool` (`lib/nw/util/HandlePool.hpp/.cpp`) is a
  type-erased raw-byte pool — it allocates fixed-size slots and hands back
  `void*`, with no idea what's actually stored there, so it genuinely can't
  call a destructor itself (its own comment says as much: "caller is
  responsible for destroying object contents before pool destruction").
  `RuntimeObjectPool::destroy(TypedHandle)` already has the correct per-type
  logic for the individual-handle path (dispatch on `Effect` vs. a generic
  `VariantObject` with N inline `String`s) — but nothing ever ran that logic
  for objects still live when `RuntimeObjectPool` itself gets bulk-destructed
  (every module unload): `~RuntimeObjectPool() = default;` just let `pools_`
  (an array of `unique_ptr<TypedHandlePool>`) free raw chunk bytes, leaking
  every live `Effect`/`Event`/user-variant's inline `String`s.

  Fixed by giving both pool types a way to walk their own live slots without
  needing to know what's stored in them: added `for_each_live()` to
  `HandlePool<Payload>` (template, header-only — snapshots the free list into
  a set, then calls `fn(payload)` for every constructed slot not in it) and
  to `TypedHandlePool` (same shape, but calls `fn(void*)` with the raw object
  pointer `get()` would return, since it has no `Payload` type to hand back).
  `RuntimeObjectPool::~RuntimeObjectPool()` now uses these — for every
  registered `pools_[type_id]`, walks its live slots and runs the matching
  per-type destructor (the same branch `destroy()` already had: `~Effect()`,
  or N `~String()` calls + `~VariantObject()`) before `pools_`'s implicit
  destruction frees the chunks. `Effect`/`VariantObject` destructors only
  touch their own inline `String`s (confirmed by reading `Effect::~Effect()`)
  — no cross-handle references — so unlike `ObjectPool<T>::clear()` there's
  no cascading-destruction hazard here; a single free-list snapshot per pool
  is enough.

  Found and fixed a second instance of the same bug shape while in this
  code: `unmanaged_array_pool_` (a `HandlePool<UnmanagedArrayHandle>`, used
  for propset array storage) stores `UnmanagedArrayHandle{IArray* array;
  Handle generic_handle;}` — a plain struct with an implicit, trivial
  destructor that never deletes `array`. `delete wrapper->array` only ever
  happened inside the explicit `destroy_unmanaged_array()` call; any array
  whose handle outlived that call leaked the same way. Fixed in the same
  `~RuntimeObjectPool()`, using the new `HandlePool::for_each_live()` to
  delete every still-live `array` before `unmanaged_array_pool_` destructs.

  Verified: targeted filter (`*Runtime*:*Effect*:*Event*:*Variant*:
  *HandlePool*`, 122 tests) passes under a normal build; a combined filter
  covering that plus `*Object*:*Module*:*Area*:*Creature*` (499 tests) passes
  clean under ASan/UBSan with zero crashes, and zero leak-report frames in
  `RuntimeObject.cpp`, `HandlePool.cpp`, or `HandlePool.hpp`, and zero leaks
  whose allocation site is `allocate_effect()`/`allocate_event()`/
  `allocate_variant()`/`allocate_unmanaged_array()` — confirmed by reading
  every leak stack that even mentions `Effect`/`VariantObject` by name; each
  one turned out to be an unrelated local vector inside `dispatch_on_effects`
  or the smalls VM/GC (which just happens to take `Effect*` parameters), not
  an actual `Effect`/variant object leaking. Full 1834-test suite passes
  clean under the normal build.
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
- **#5 (Palette) — FIXED (2026-11-18), unrelated to the arena question** —
  a plain tree-walk bug, not a destructor-correctness one. `Palette::
  ~Palette()` and `PaletteTreeNode::clear()` both called `node_pool_.free()`
  on direct children only — `node->children` is a `Vector<PaletteTreeNode*>`
  of raw pointers, so destructing *that* vector (which is all `free()` does
  for the node itself) frees the pointer array, not the pointed-to
  grandchildren. Turned out `PaletteTreeNode::clear()` is dead code (no call
  site anywhere in the codebase outside its own definition) — but the shape
  is real and was one call site away from a live bug, so fixed both with one
  shared recursive helper (`free_subtree()`, post-order: free every child's
  own subtree before freeing the child itself, so nothing dereferences an
  already-freed pointer).

  The two call sites have different exposure, worth being precise about:
  `~Palette()`'s *leak* half was already incidentally masked before this fix
  — `ObjectPool<T>::clear()` (fixed under #1 above) walks every slot in the
  whole pool at `~ObjectPool()` time regardless of tree structure, so any
  grandchild `~Palette()`'s shallow loop missed still got its `String`s
  destructed when `node_pool_` itself destructed afterward, just not in any
  particular (tree) order — correct outcome, by accident, via a different
  layer's fix. `clear()`'s bug is not masked the same way: it's meant to
  reset *one* node for reuse while the `Palette`/pool stays alive, and a
  grandchild it fails to free stays allocated (an orphaned, unreachable pool
  slot) for the `Palette`'s entire remaining lifetime, not just until the
  next process-wide sweep — `ObjectPool<T>::clear()` only runs at whole-pool
  destruction, so it doesn't help here at all. Fixed on its own terms now,
  not relying on #1's sweep for either call site.

  Verified: existing `Palette.*`/`ClientAreaTileEdits.Palette*` tests (which
  exercise `~Palette()` on real 3-level GFF-loaded trees via RAII) pass under
  both a normal build and ASan/UBSan, zero `Palette.cpp` leak-report frames.
  Added `Palette.ClearRecursivelyFreesWholeSubtree` (`tests/
  legacy_palette.cpp`) as new coverage for `clear()`'s recursion specifically
  (previously exercised by nothing, dead code or not) — builds a branch →
  category → two blueprints tree, calls `clear()` on the branch, and checks
  `node_pool_.size()` drops from 4 to 1 (only the reset branch itself
  remains allocated), not 4 to 3 (the old, depth-1-only behavior).

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
2. ~~`ObjectPool<T>::clear()`: walk live slots calling `~T()` before freeing
   pool chunks.~~ **Done** — plus the three destructor-ordering fixes
   (`Services::shutdown()`, `ObjectArray` pool member order, `ObjectManager`
   member order) and the new `ObjectArray::~ObjectArray()` handle-walk it
   turned out to need. See #1/#3 above for the full chain.
3. ~~`TypedHandlePool`: same shape for #2 — give it a real destructor instead
   of the "caller's responsibility" comment, or make `RuntimeObjectPool`
   responsible for walking its own live handles before the pool beneath it
   is torn down.~~ **Done** — `TypedHandlePool` stays type-erased (it still
   can't call a destructor itself), but both it and `HandlePool<Payload>`
   gained `for_each_live()`, and `RuntimeObjectPool::~RuntimeObjectPool()`
   uses it to run the right per-type cleanup (and to fix the
   `unmanaged_array_pool_`/`IArray*` leak found along the way). See #2 above.
4. ~~`unload_module()`: once 2–3 exist, remove the now-incorrect "no need to
   call `Module::destroy`" shortcut ... and explicitly destruct `Module`
   itself (#3).~~ **Done** — turned out to need no explicit call: `Module`
   goes through the same handle-array path as every other object type, so
   step 2's `~ObjectArray()` walk already covers it. Reworded the
   `unload_module()` comment to say why, instead of resting on the
   bulk-teardown assumption this audit distrusts.
5. ~~`Palette`: fix the one-level-only recursion in `~Palette()`/
   `PaletteTreeNode::clear()`.~~ **Done** — shared recursive `free_subtree()`
   helper; also added the first regression test for `clear()`'s recursion
   (previously dead code, untested either way). See #5 above.
6. Regression: for 2–4 specifically, a real test that creates several live
   objects/effects of different types, unloads the module, and verifies (via
   an instrumented counting allocator, or ASan leak detection if already
   wired into CI) that their destructors actually ran — the existing test
   suite passing is not evidence here, since these leaks don't crash or fail
   assertions, they just leak, which is exactly why they went unnoticed.
   Not done as a dedicated test; covered so far by ASan/UBSan leak-detection
   runs against the existing suite at each fix (2, 3, 5) rather than one
   purpose-built regression test — revisit if a future change needs a
   standing guard rather than one-off verification.
