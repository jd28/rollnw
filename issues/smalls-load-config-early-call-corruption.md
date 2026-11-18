# `load_config!` can permanently cache empty data if first called too early

Status (2026-11-17): root cause narrowed to a specific, strong lead; not fully
pinned down, not fixed. Discovered while fixing
[the weapon-category master-feat bug](nwn1-combat-rules-completion.md) — the
first attempt at that fix (an eager loop over `baseitems.count()` inside
`nwn1.init.init()`) was logically correct but broke 6 unrelated equipment/
visual tests. The shipped fix (lazy registration on first real use) avoids
triggering this bug rather than fixing it. This doc exists so the underlying
bug doesn't get lost, and so the next thing that eagerly touches a
`load_config!`-backed module from `Init.init()` has a known trap to check for.

## Reproduction

In `lib/nw/smalls/scripts/nwn1/init.smalls`'s `init_master_feats()` (called
from `init()`, called from `Init.init()`), add any single call into
`nwn1.baseitems` that triggers `_ensure_loaded()` — even a bare
`baseitems.count();` with no other logic is sufficient. Then run
`SmallsEngineIntegration.Nwn1EquipmentArmorClassProducerUsesEquippedItems`
(or any of the other 5 listed below). It fails:
`NItem.calculate_item_ac(shield) != 1` (returns 0, expected 1), because
`BaseItems.base_ac(BaseItemType(14))` returns 0 instead of 1 — `baseitems`'
own data is empty when it should have 113 entries.

Failing tests, all in `tests/smalls_engine_integration.cpp`:
`Nwn1EquipCallbacksUpdateVisualRowsForChangedSlot`,
`Nwn1EquipCallbacksRebuildHumanoidBodyRowsForHeadSlot`,
`Nwn1HumanoidMirrorsBodyVariationsButKeepsArmorPartsIndependent`,
`Nwn1ItemResolveVisualReturnsAttachmentValues`,
`Nwn1StandaloneItemVisualWritesVisualRows`,
`Nwn1EquipmentArmorClassProducerUsesEquippedItems`.

## Evidence trail

Instrumented `publish_baseitem_info` (`lib/nw/smalls/native/core_item.cpp:53`)
with an `fprintf` logging the incoming array size and early-return paths, and
instrumented the `Nwn1EquipmentArmorClassProducerUsesEquippedItems` test with
extra reads of `nw::kernel::rules().baseitems.entries.size()` at several
points. Observed call sequence, with the eager `baseitems.count()` call added
to `init_master_feats()`:

1. `publish_baseitem_info` called, `array_size=113` (looks correct).
2. `publish_baseitem_info` called again almost immediately, `array_size=0` —
   logs "zero-size array, publishing empty, returning true". This call wins:
   `rules().baseitems.entries.size() == 0` from this point through the rest
   of the test (confirmed at four separate checkpoints, including right after
   `shield->instantiate()`, which runs in C++ before any smalls script in the
   test even executes).
3. `BaseItems.base_ac(BaseItemType(14))` returns 0 (wrong; should be 1) —
   confirms the empty data is what's actually read, not just what gets
   published transiently.
4. At test teardown, `publish_baseitem_info` is called a *third* time with
   `array_size=113` again — too late to matter for the test's assertions, and
   its relationship to calls 1–2 is not understood (see Open question).

Without the eager call (lazy registration, as shipped), `publish_baseitem_info`
is observed to succeed once, early, with 113 entries, and `rules().baseitems`
stays populated throughout — no second call, no corruption.

## Leading hypothesis

`nwn1.baseitems._ensure_loaded()` (`lib/nw/smalls/scripts/nwn1/baseitems.smalls:17-47`)
does, per entry: `if (definition.id == index) { push real info/rules,
valid=true } else { push an EMPTY placeholder (empty label), valid=false }`.
`publish_baseitem_info` (`core_item.cpp:118-120`) separately rejects the whole
publish if `valid_count == 0` — i.e., if *every* entry turned out to be an
empty placeholder, even though the array's *size* (113) looked normal.

That fits call 1 exactly: `array_size=113` but internally all 113 are
placeholders (every `definition.id != index`), so `publish_baseitem_info`
returns `false` without having logged a size-based early return — consistent
with the observed log (no "early return false" or "zero-size" line attached
to that first 113-sized call, because the `valid_count==0` return path wasn't
instrumented in this pass). Critically, this failure path does **not** set
`baseitems`' local `_loaded` flag, so a second call is free to retry.

Why would every `definition.id` fail to match its array index at that early
point, when the same config resolves correctly moments later? Unconfirmed.
`Runtime::load_config_array_value` (`lib/nw/smalls/runtime.cpp:7057-7062`)
opens with `if (resman_needs_build_) { resman_.build_registry();
resman_needs_build_ = false; }` — a lazy, dirty-flag-gated resource registry
rebuild. The native module-load sequence
(`lib/nw/kernel/Kernel.cpp:334-431`: `resman_load_module` → `make_module` →
`load_haks` → `load_custom_tlk` → `module_post_load` services →
`instantiate`) mounts HAKs *after* the base module resources. If the
native-triggered `Init.init()` call (`lib/nw/profiles/nwn1/scriptbridge.cpp:49`,
via `ensure_nwn1_smalls_initialized()`) fires from inside this sequence before
HAK/override content is fully layered into the registry, `load_config!` could
read back a 2da view where row ids aren't yet assigned the way they are once
everything is mounted — this is plausible but **not verified**; no
instrumentation was added inside `load_config_array_value` or the 2da
indexing path itself to confirm it.

What the second (empty, 0-length `definitions`) call means is separately
unexplained — a *different* resman state than call 1's "113 but all
placeholder," not simply "the same bad state again."

## Open question / next steps for whoever picks this up

1. Instrument `load_config_array_value`'s 2da/definition-indexing path
   directly (not just `publish_baseitem_info`) to see the raw `id` values
   call 1 actually produces, and whether `resman_needs_build_` is true at
   that point.
2. Explain the second call's `array_size=0` (completely empty `definitions`,
   not just id-mismatched) — a different failure mode than call 1's, and the
   one that actually locks in the corruption via the `array->size()==0`
   fast-success path in `publish_baseitem_info`.
3. Explain the third, late, correct (113) call — if something *does*
   eventually call `_ensure_loaded()` successfully after `_loaded` was
   already (wrongly) latched true from call 2, that implies either multiple
   independent pieces of state keyed to "baseitems" (not a true singleton),
   or a different, ungated caller publishing directly.
4. Once understood, the real fix is almost certainly in `publish_baseitem_info`
   and/or `_ensure_loaded()`: treat *any* unsuccessful/degenerate publish
   (zero-length array, all-placeholder array, or validation failure) the same
   way — never cache `_loaded = true` unless at least one real entry
   published. A partial version of this (rejecting zero-length without
   caching) was tried in isolation during this investigation and was **not**
   sufficient by itself, consistent with there being more than one failure
   mode in play (see point 2).
5. Treat this as a candidate systemic issue, not a `baseitems`-only one: any
   other `_ensure_loaded()`-style lazy config module touched from inside the
   native-triggered early `Init.init()` call (rather than from its normal,
   later, first-real-use point) should be checked for the same failure shape.
