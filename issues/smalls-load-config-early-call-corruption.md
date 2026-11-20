# `load_config!` could permanently cache empty data read during a resman rebuild gap — fixed

Status (2026-11-18): **fixed**, verified by re-running the original failing
scenario against the fix with no other workaround. Discovered while fixing
[the weapon-category master-feat bug](closed/nwn1-combat-rules-completion.md) — the
first attempt at that fix (an eager loop over `baseitems.count()` inside
`nwn1.init.init()`) was logically correct but broke 6 unrelated equipment/
visual tests. The shipped weapon-feat fix (lazy registration on first real
use) avoids triggering this bug rather than fixing it; this doc is the actual
infrastructure fix, landed separately in `lib/nw/resources/ResourceManager.cpp`.

## Root cause (revised — the first write-up of this was wrong about the mechanism)

An earlier pass through this investigation concluded `ResourceManager::unfreeze()`'s
eager `registry_.clear()` was the cause. That was real but **not** the dominant
mechanism — fixing only that did not close the original bug when re-tested.

The actual cause: `nw::kernel::load_module(...)`, when services are already
running (true for every module load after the first — the normal case, not an
edge case), calls `Services::shutdown()` first. That wipes the entire service
array and the arena backing it (`services_ = std::array<ServiceEntry, 32>{};
kernel_scope_.reset();` — a deliberate design: with as much static/global
state as this engine has, a full arena wipe sidesteps auditing every service
for module-awareness, rather than something to undo). `Services::start()`
then constructs a brand-new `ResourceManager` from scratch.

That fresh instance's `initialize()` only had two trigger points:
`kernel_start`/`module_post_load` call `build_registry()`; `kernel_start`/
`module_pre_load` add the base/override containers and call
`update_container_search()`, but **not** `build_registry()`. Module/HAK
containers get mounted in between (`resman().load_module(path)`,
`load_module_haks(...)`, both outside `ResourceManager::initialize()`). So for
a fresh instance going through a normal module load, there's a real window —
between `module_pre_load` (containers exist) and `module_post_load` (registry
finally built) — where the registry is simply empty, correctly reflecting
"not built yet." Anything that queries `kernel::resman()` in that window gets
a true-at-the-time "not found," and if that result gets cached anywhere
without distinguishing "not built yet" from "genuinely absent," the false
negative outlives the window.

In the broken (eager) weapon-feat fix, the native NWN1 bridge's
`ensure_nwn1_smalls_initialized()` (`lib/nw/profiles/nwn1/scriptbridge.cpp:49`)
ends up calling into `nwn1.baseitems` from inside that exact window during
`load_module("DockerDemo.mod")`. Confirmed by instrumenting `ResourceManager`
directly (object identity via pointer, not just flags) across the whole
sequence — the registry genuinely has zero content between the two
`initialize()` calls, not stale/wrong content.

## Fix

`lib/nw/resources/ResourceManager.cpp`:

1. `initialize()`: also call `build_registry()` immediately after containers
   are added at `module_pre_load`, not only at `module_post_load`. The
   registry is now built as soon as *any* containers exist, with whatever's
   there at the time (base/override game data); `module_post_load` rebuilds
   again once module/HAK containers are mounted. No window where containers
   exist but the registry doesn't reflect them, at any point in the sequence.
2. `unfreeze()`: stopped eagerly clearing `registry_`/`base_twoda_registry_`.
   Not the dominant fix, but a real, independent improvement kept regardless:
   a query between `unfreeze()` and the next `build_registry()` (anywhere this
   happens, not just module loading) now sees the last-built, possibly
   slightly stale registry instead of nothing. `build_registry()` now does
   the clearing itself, right before rebuilding from scratch.
3. Both `initialize()` call sites now `unfreeze()` first if already frozen —
   needed because step 1 means the registry can legitimately be frozen again
   by the time `module_post_load` runs, and `build_registry()` requires
   `!frozen_`.

This does **not** change the arena-wipe-on-load_module design — that stays
exactly as it was. The fix is entirely about making sure the always-rebuilt,
freshly-constructed `ResourceManager`'s registry is never left inconsistent
with its own containers at any point, regardless of what queries it or when.

## Verification

Re-applied the original eager weapon-feat loop (`baseitems.count()` inside
`nwn1.init.init()`'s `init_master_feats()`) on top of this fix and reran the
6 originally-failing tests plus the full `Smalls*`/`KernelResources*`
suites — all pass, including with the eager trigger reinstated (i.e. the fix
closes the hazard itself; it doesn't just happen to dodge it like the lazy
weapon-feat workaround does). The shipped weapon-feat fix was **not** reverted
to the eager version — the lazy registration in `master_feats.smalls` stays
as-is; there's no reason to re-churn code that already works, and it's
harmless defense-in-depth regardless.

## Follow-up (flagged, not done)

User flagged: audit all memory usage across the engine after this change,
given how much state is static/global and how central the arena-wipe-on-load
pattern is to managing that. Not scoped or started in this pass.

## Systemic note

This was a general hazard, not specific to `baseitems` or `load_config!`:
**any** code — native or smalls — that queried `nw::kernel::resman()` during
the `module_pre_load` → `module_post_load` window got a correct-at-the-time
"not found," and anything caching that without distinguishing "not built yet"
from "genuinely absent" would stay wrong after the window closed. The fix
closes the window itself rather than patching individual callers, so it
protects every current and future caller, not just this one.
