# SmallS Rules Ownership Audit Follow-up

Status: open. Tier 2.

## Goal

Finish the three deferred findings from the 2026-09-02 SmallS rules-ownership
audit without reopening the completed package/bootstrap migration. The target
is one selected package owning gameplay policy while C++ owns validated native
storage, event scheduling, object identity, and copied native catalogs.

The limit is the observed NWN1 runtime. This work does not move persistence,
resource-container storage, renderer data, object-component lifetime, or the
native event queue into SmallS. It does not add a profile registry, plugin
interface, generalized resource backend, hot reload, or schema negotiation.

Plan B is to retain a specific native operation when measurement or an actual
native invariant shows that moving its policy would add material per-attack
cost or violate native lifetime requirements. That exception must document the
input, output, owner, lifetime, and measured cost; a C++ file name alone is not
evidence of native ownership.

## Real Platform and Data

The runtime is one native process, one selected package, one SmallS runtime,
and one service generation at a time. The current package has 44 top-level
NWN1 SmallS modules and 22 sorted data specs. Native SmallS support is provided
by 18 `core_*.cpp` translation units. Module and hak resources become active
between kernel startup and module post-load initialization.

The common case is a game runtime using the shipped `nwn1` package. Combat
reads contiguous native object/effect state and calls package policy; item
catalogs are built once per resource generation; scheduled attacks recur for
each active attacker. Error cases are malformed or missing table rows, invalid
SmallS integer indices, missing policy exports, unloaded objects, and service
generation changes.

ASSUMPTION: one selected package and its resource set remain fixed for one
service generation — affects cache invalidation and catalog publication.

ASSUMPTION: event scheduling remains a native storage/lifetime invariant while
attack cadence and attack resolution remain package policy — affects the
combat-scheduler boundary.

No combat latency or throughput baseline was collected by this audit. Any
performance effect below is unverified until the existing representative combat
test/benchmark is measured before and after on the same build and host.

## Remaining Findings

### 1. Publish item-property catalogs as complete active-resource batches

Severity: high correctness and ownership risk.

`EffectSystem::initialize` directly reads `iprp_costtable`, `iprp_paramtable`,
`itempropdef`, and `itemprops` during both kernel startup and module post-load.
The definition vector appends on the second pass, so indexed lookups continue
to see the earlier rows instead of the active module/hak definitions. Resized
cost/parameter vectors can also retain stale entries, and unchecked signed
table indices are converted to `size_t` before indexing. A malformed negative
or oversized `CostTableResRef`/`Param1ResRef` can therefore access outside the
catalog.

Input is the resolved active 2DA batch. Output is a validated indexed native
catalog used by `core.item` editor/query operations. Build every cost table,
parameter table, definition, and base-item permission table into temporary
contiguous storage; reject invalid references explicitly; publish the complete
batch only on success. Module/hak rows must win, and a rejected publication
must leave that domain empty rather than preserve the previous generation.

Done evidence:

- active module/hak item-property overrides are returned by `core.item`;
- a second initialization does not append or retain base-resource rows;
- negative and oversized table references are rejected without out-of-bounds
  access under ASan/UBSan;
- malformed publication is all-or-nothing for this domain; and
- valid sibling rules domains remain usable.

### 3. Make the combat scheduler package-neutral

Severity: medium architecture and lifecycle risk.

`combat_scheduler.cpp` selects the configured module for `resolve_attack`, but
still requires the concrete return type name
`nwn1.combat_primitives.AttackData`, invokes
`ensure_nwn1_smalls_initialized`, and calls
`nwn1.combat.resolve_attack_cooldown_ticks` directly. This is only partially
configured. The native queue and object handles are correct native storage;
module names, return schema, and cadence are package policy.

Input is a batch of attack scheduling requests containing attacker handle,
target handle, generation, and delay. Output is native event-queue entries and
resolved attack/effect intents. Resolve and validate the required attack and
cooldown functions once from the selected package for each service generation,
then schedule by handles and numeric ticks. Missing mandatory functions reject
combat bootstrap or the request explicitly; unloaded/dead objects drop the
event. Keep singular public helpers only as thin requests into the same queue
path.

Done evidence:

- the scheduler contains no `nwn1` module/type/function names;
- configured package functions are cached and invalidated by service
  generation;
- missing exports, bad return layout, unloaded targets, death, stop/restart,
  and repeated service generations are tested; and
- scheduled and automatic attack behavior remains covered by existing combat
  tests.

### 4. Validate integer bounds before native iterator arithmetic

Severity: medium correctness risk.

`core_combat.cpp` computes `begin + index` and `begin + start_index` before
proving that a SmallS-supplied signed integer is within the effect-array extent.
For a sufficiently large positive value, forming that iterator is undefined
behavior even though the subsequent comparison intends to reject it.

Input is a SmallS `int` index and a contiguous effect-array extent. Output is
an effect handle/index or the existing invalid sentinel. Convert only after
checking `index >= 0` and `index < size`; reject every out-of-range value before
iterator arithmetic. Apply the same boundary audit to the remaining native
SmallS functions that convert signed indices or counts to unsigned native
types.

Done evidence:

- minimum, maximum-valid, negative, `INT32_MAX`, and empty-array cases are
  tested;
- ASan/UBSan report no invalid iterator arithmetic; and
- every audited conversion documents clamp, reject, drop, or error behavior.

## Transform and Delivery Order

1. Fix item-property publication first because current active-resource results
   can be stale or unsafe. Measure catalog row/container counts before and
   after; expected runtime cost is one temporary batch and one publication per
   module load.
2. Resolve package combat functions once per service generation and leave event
   storage native. The recurring cost remains one queue event plus the existing
   policy call per attack.
3. Complete the signed-index boundary audit and run sanitizer coverage.

Estimated engineering cost is 1.5–3 days across three reviewable commits. Peak
catalog memory temporarily includes old plus candidate batches during atomic
publication; retained memory should remain one active catalog. This is a
correctness/ownership plan, not a memory or performance claim.

## Simplification Decisions

- Reuse the selected-package path, existing data-spec transform, service
  generation, compiled-function cache pattern, native event queue, and indexed
  catalog publisher.
- Perform catalog construction once per resource generation and function/type
  resolution once per service generation.
- Keep effect traversal linear and pass selectors as flat integers; no new
  object model, callback graph, registry, or generic backend is required.
- Constrain this issue to the four observed defects. Presentation and unrelated
  native storage stay in their focused issues.

Evidence against this plan would be an observed item-property input that cannot
be expressed by the existing spec transform, or a measured combat regression
showing that package-owned selector composition cannot meet the current native
combat cost on the supported platform.

## Completed During This Audit

- [x] Creature equipped item-property initialization now uses the mandatory
  `nwn1.profile.object_instantiated` hook and one linear pass over the fixed 18
  equipment slots; generic `Creature.cpp` no longer calls NWN1 policy.
- [x] Unused native modifier registry/resolution scaffolding, native master-feat
  registry, empty combat callback structs, unused weapon-modifier declarations,
  dead C++ constants, and the unused alignment helper are removed. Qualifier,
  Requirement, and the mandatory profile matcher remain the active protocol.
- [x] `core.combat` now accepts explicit effect/attack selectors and performs
  only bounded native traversal. The `nwn1` package supplies NWN effect types,
  subtype policy, and the attack clamp. Invalid selectors, effect integer
  indices, and stack policies return the existing empty result. Existing NWN1
  combat fixtures cover attack, immunity, reduction, and resistance behavior.
  No traversal benchmark was run and no performance improvement is claimed.
