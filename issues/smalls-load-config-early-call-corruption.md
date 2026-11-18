# `load_config!` can permanently cache empty data read during a resman unfreeze window

Status (2026-11-17): root cause confirmed by direct instrumentation. Not
fixed — three candidate fix locations identified, no fix chosen yet since
each has real tradeoffs (see Recommendation). Discovered while fixing
[the weapon-category master-feat bug](nwn1-combat-rules-completion.md) — the
first attempt at that fix (an eager loop over `baseitems.count()` inside
`nwn1.init.init()`) was logically correct but broke 6 unrelated equipment/
visual tests. The shipped fix (lazy registration on first real use) avoids
triggering this bug rather than fixing it.

## Root cause

`nw::kernel::load_module(...)` unfreezes and re-freezes the **global**
`nw::kernel::resman()` at least once during its own internal sequence (to
mount the module's container/HAKs), well before it returns.
`ResourceManager::unfreeze()` (`lib/nw/resources/ResourceManager.cpp:360-366`)
eagerly does `registry_.clear()` — so for the duration of that window,
`resman.contains(...)`/`resman.demand(...)` report **every** resource as
missing, correct ones included, until something calls `build_registry()`
again.

If anything triggers a `load_config!`-backed config load during that exact
window, it reads back nothing and that *transient* emptiness gets **cached
permanently** by two independent, uncoordinated caches:

- `Runtime::config_array_cache_` (`lib/nw/smalls/runtime.cpp:7072-7080`,
  populated at `:7340-7341` for the data-spec path specifically used here)
  caches by `(canonical_path, config_type)` with no distinction between "this
  value is genuinely empty" and "resman happened to be unfrozen when I asked."
- `nwn1.baseitems`'s own `_loaded` flag (`lib/nw/smalls/scripts/nwn1/baseitems.smalls:17-47`)
  does the same thing one layer up, for the same reason.

In the broken (eager) version of the weapon-feat fix, `nwn1.init.init()` gets
triggered natively (`lib/nw/profiles/nwn1/scriptbridge.cpp:49`,
`ensure_nwn1_smalls_initialized()`) more than once *during*
`load_module("DockerDemo.mod")`'s own sequence — once while resman is frozen
(succeeds), then again while resman is transiently unfrozen (fails), both
*before* `load_module()` returns to the caller. The lazy fix never triggers
`baseitems` loading until well after `load_module()` has returned and resman
has settled, so it never lands in the unfrozen window — that's *why* it
works, not because the underlying hazard is fixed.

## Evidence trail (direct instrumentation, now reverted — not kept in tree)

Added temporary `fprintf` tracing at: `Runtime::load_config_array_value`
(cache state, `resman_needs_build_`, and — critically — the identity of
`kernel::resman()` vs. the Runtime's own private `resman_`, confirmed to be
parent/child, `lib/nw/smalls/runtime.hpp:1426` +
`lib/nw/smalls/runtime.cpp:261`, *not* related to this bug), `load_data_sources`
(`lib/nw/smalls/data_transform.cpp:622`, prints `resources.is_frozen()` and
`resources.contains(...)` right before `demand()`), and the test itself
(prints `nw::kernel::resman().is_frozen()` at each statement). Running
`Nwn1EquipmentArmorClassProducerUsesEquippedItems` with the eager weapon-feat
loop reinstated:

```
[call 1] frozen=1 contains=1 → demand succeeds, is_valid=1   (113 real entries)
DIAG2 frozen BEFORE load_module = 1
[call 2] frozen=0 contains=0 → demand fails,   is_valid=0    ("baseitems.2da is missing")
DIAG2 frozen AFTER load_module = 1
DIAG2 frozen AFTER load target / armor / make_item_from_gff / shield instantiate = 1 (unchanged for the rest of the test)
[call 3] frozen=1 contains=1 → demand succeeds, is_valid=1   (too late — assertions already failed)
```

Both call 1 and call 2 happen **before** `load_module()` returns (interleaved
between the `BEFORE`/`AFTER load_module` prints) — confirming the unfreeze/
refreeze cycle is entirely internal to `load_module()`, not caused by
anything the test itself does afterward (an earlier pass through this
investigation wrongly suspected the test's own `rt.add_module_path(...)`
call; that call touches a *different*, Runtime-private `resman_`, not
`kernel::resman()`, and turned out to be an unrelated correlation — ruled out
by this trace). `kernel::resman()` stays frozen for the entire remainder of
the test once `load_module()` returns, so once past this window, everything
is consistent and correct — the corruption is a one-time race during module
loading, not an ongoing issue.

## Recommendation (not yet implemented — needs a decision)

Three independent places could close this, each a different kind of fix:

1. **`ResourceManager::unfreeze()`**: don't `registry_.clear()` eagerly;
   either defer the clear until `build_registry()` actually rebuilds, or keep
   serving the old (stale but not-yet-wrong) registry until then. Fixes the
   hazard at its actual source — a resource transiently *reporting* missing
   when it isn't. Risk: `registry_`'s stale-serving window could mask a
   genuine removal (e.g. an unloaded module's resource) for longer; needs
   checking every other `unfreeze()` caller's expectations.
2. **`load_config_array_value` / `load_data_spec_as_config_array` caching**:
   never cache a `make_empty_array()`/failed result — only cache real,
   non-empty successes. Narrower, smalls-runtime-local fix; doesn't help any
   *other* native caller that might hit the same unfrozen-resman window and
   draw a wrong one-time conclusion (this bug's shape isn't specific to
   `load_config!`).
3. **The native trigger point**: move `ensure_nwn1_smalls_initialized()`'s
   first real call (or whatever triggers it this early) to run only after
   `load_module()` has fully settled resman (e.g. gate on
   `ServiceInitTime::module_post_instantiation` rather than whatever earlier
   point currently fires it, if that's adjustable). Sidesteps the window
   rather than closing it — doesn't protect against some *other* native path
   hitting the same race differently in the future.

(1) is the most principled fix (closes the actual hazard), (2) is the
narrowest/safest to land quickly, (3) doesn't generalize. Given the shipped
weapon-feat fix already avoids this specific trigger, there's no urgency
forcing a choice — but (2) alone was tried in isolation during this
investigation (treating a zero-length `definitions` array as non-cacheable)
and did **not** fix the test by itself, which makes sense now: the moment of
failure wasn't "zero-length array," it was `resman.demand()` itself returning
invalid because resman was unfrozen — the cache was never going to see
anything cacheable as empty vs. correct from inside `load_data_sources`
without resman-level information. A real fix needs (1) and/or (2) together,
or (1) alone.

## Systemic note

This is a general hazard, not specific to `baseitems` or to `load_config!`:
**any** code — native or smalls — that queries `nw::kernel::resman()` during
a `load_module()`-internal unfreeze/rebuild window gets a false "not found,"
and anything that caches that result without distinguishing "transiently
unavailable" from "genuinely absent" will stay wrong after the window closes.
Worth a wider audit of `config_array_cache_`-style caches and other
`resman().demand()` call sites that might run during module load, not just
this one.
