# NWN1 combat: subsystems blocking the remaining rules

Status (2026-11-20): open. Split out of
[the combat rules completion plan](closed/nwn1-combat-rules-completion.md) when
that closed, as the items blocked on a missing subsystem or missing game data
rather than on rules work. Each blocker is shared rather than per-feat. Ordered
by how much new design each needs.

Updated 2026-11-20 after sweeping `feat.2da` against the feat ids the rules
actually name, and the modifier-type enum against what has dispatch behind it.
That found a fourth blocker -- no initiative system -- and a batch of base NWN1
feats that do nothing. The unblocked ones from that batch have since been
implemented (saving-throw feats, combat-mode and special-attack feat gating,
the passive skill bonuses, Disarm); the rest are under Feat backlog below.

None of these should be attempted by inferring the missing pieces. The whole
point of the audit that produced the parent issue was that guessed-at or
unreachable rule code looks implemented and silently does nothing.

## 1. On-hit weapon properties — blocked on game data

`on_hit.smalls` has real dispatch: handlers register by
`(property_type, property_subtype)` and `resolve_weapon_on_hit()` fans item
properties out to them, accumulating `negate_hit`, `base_damage_bonus` and
damage rolls. What it has for handlers is three no-op stubs, and
`register_default_handlers()` **is never called from anywhere**, so
`has_handlers()` is false and the entire path is inert at runtime.

Two independent blockers:

- **The subtype vocabulary is not in the repo.** Which on-hit effect a property
  means (Vorpal, Wounding, Stun, Blindness, Slay Race, Ability Drain, ...) is a
  row index into `iprp_onhittype.2da`. This repo's test data carries 13 2das and
  none of them are `iprp_*` or `itempropdef.2da`. Handlers keyed on hardcoded
  row ids would be unverifiable and silently wrong for every property. The
  engine's `ip_option_name()` native reads these tables, but returns a strref,
  so name-based matching needs a TLK that is also absent.
- **Several properties need per-round ticking.** Wounding, item poison and
  disease are damage over time. Single-shot expiry now works (see the shared
  blocker below), but a *recurring* per-round tick does not exist — that needs a
  repeating event, not a scheduled removal.

Reachable subset once the 2da exists: Vorpal and the three Slay variants map
onto the `death` effect type added by the parent issue, and Stun/Sleep map onto
`stunned`/`sleep`. Those need no tick loop.

## 2. Attacks of opportunity and reach — blocked on spatial/movement events

No concept of either exists anywhere, smalls or native. AoO needs three things
that are all absent:

- **Threat ranges.** Reach is the same gap: nothing reads weapon reach, and
  `baseitems.2da` has no reach column.
- **A provoking event.** Specifically "a creature left a square I threaten", plus
  the other provoking actions. There is no movement event. `core.object` exposes
  `get_position`/`set_position` and no notification; `core.area` exposes only
  width, height, tile count and lighting.
- **A spatial neighbour query.** There is no "creatures near X" in smalls at
  all. `is_flanked()` currently fakes awareness by reading
  `CreatureCombat.off_target`, which is really a modifier-cache invalidation
  key, not a gameplay target.

**The same missing neighbour query blocks the firing-into-melee penalty**, which
is why that one item from the parent ledger is also unfinished: a flat -4 is
trivial, but deciding whether the target is engaged in melee with someone else
requires enumerating nearby creatures.

This is the largest of the three and overlaps a design decision that is already
open elsewhere: the real-time vs turn-based scheduling split in
`combat_scheduler`. It should not be designed as a side effect of a rules fix.

The neighbour-query half now has its own plan:
[spatial queries](spatial-queries.md). That covers the index and the "what is
near this point" capability only — threat ranges, provoking actions and a
reaction slot per round remain this item's own design.

## 3. Casting-in-combat layer — blocked on a cast pipeline

Concentration checks, AoO while casting, and spell interruption all need a spell
cast to be an interruptible action with a beginning and an end.
`spells.smalls` and `metamagic.smalls` are metadata-only and there is no cast
action, so there is nothing to hook. The parent plan already suspected this
belongs with Phase 3's event-loop work rather than with rule content, and that
looks right.

Partially landed already by the parent issue: `equipment.arcane_spell_failure()`
sums arcane spell failure from equipped gear using the real `ArcaneSpellFailure`
column (shields 5/15/50, matching NWN). Still missing is the armour-derived
share, which in NWN comes from `armor.2da` keyed by the armour's AC tier — that
column is not in the `ArmorEntry` data spec and `armor.2da` is not in the test
data.

`mfeat_arcane_defense` also waits on this: Arcane Defense keys off spell school,
and saving throws have no school parameter — `SaveVersus` has 20 values and none
of them are schools. `get_saving_throw()` also does not consult the modifier
registry at all, so `mod_type_save` (declared, id 14) has no dispatch; wiring
that is a prerequisite.

## 4. Initiative — nothing exists

`mod_type_initiative` is declared in `constants.smalls` and that declaration is
its only mention in the codebase. There is no initiative roll, no turn order and
no "has this creature acted yet this combat" state.

Blocks, concretely:

- **Improved Initiative** (`feat.2da` 377), **Blooded** (379), **Epic Superior
  Initiative** (753) — all three absent from the rules.
- **The other sense of flat-footed.** What is implemented is the *helpless*
  sense (paralyzed/stunned/asleep). NWN's other sense is "has not acted yet",
  which needs initiative. This matters for correctness of a feat, not just
  coverage: **Uncanny Dodge** (195, 251-255) and **Uncanny Reflex** (226) negate
  the not-yet-acted sense and should *not* negate genuine helplessness, so
  implementing them against the currently-available sense would be wrong rather
  than incomplete.

This also overlaps the real-time vs turn-based scheduling split in
`combat_scheduler`, so it wants designing rather than patching.

## Feat backlog, grouped by what blocks each

Found by diffing `feat.2da` against the feat ids the rules actually name. Every
feat below is in base NWN1 with a standing mechanical effect and does nothing
here. The ones that were *not* blocked have since been implemented -- saves,
the combat-mode and special-attack feats, the passive skill bonuses, Disarm --
so what is left is grouped by its blocker:

| Blocked on | Feats |
| --- | --- |
| Spatial neighbour query (item 2) | **Cleave** (6), **Great Cleave** (391), **Opportunist** (224), **Mobility** (26, +4 AC vs AoO) |
| Initiative (item 4) | **Improved Initiative** (377), **Blooded** (379), **Epic Superior Initiative** (753), **Uncanny Dodge 1-6** (195, 251-255), **Uncanny Reflex** (226) |
| ~~Effect expiry~~ — unblocked 2026-11-20 | **Slippery Mind** (259, a delayed re-save) is now implementable |
| Nothing — just unbuilt | **Called Shot** (5), **Defensive Roll** (killing-blow detection now exists via `AttackData::is_killing_blow`, but once-per-day tracking has no substrate), **Crippling Strike** (sneak attack also deals Strength damage; expressible with the existing `ability_decrease` effect), **Skill Mastery** (225, needs a "take 10" path in skill checks) |

**Cleave specifically.** Kill *detection* is now available — `commit_attack()`
sets `AttackData::is_killing_blow` — but Cleave's extra attack is against
*another creature within reach*, and target selection needs the neighbour query.
The natural decomposition when that exists: the rules layer grants the extra
attack on a killing blow, the action layer picks the target. Granting it now
without a consumer would be exactly the registered-and-inert pattern this
cluster of issues exists to eliminate.

**Point Blank Shot** (27) is in NWN1 but carries no mechanic of its own there --
it is a prerequisite feat -- so nothing is owed. The parent plan's claim that
Called Shot is likewise absent from base NWN1 was simply wrong; see the
correction on that record.

## Smaller inconsistencies worth a pass

Not blocked on anything, just noted during the audit and left alone rather than
changed mid-task:

- **Two formulas for one concept.** `_make_elemental_damage_result()` *sums*
  the modifier-registry immunity and the effect delta;
  `resolve_damage_immunity()` takes the *max* of them (with vulnerability
  special-cased additively). Both are now deliberate and commented, but they
  should probably agree.
- **`is_flanked()` reads `CreatureCombat.off_target`**, which is a
  modifier-cache invalidation key rather than a gameplay target. It works by
  accident, and it is the wrong seam -- the more so now that flanking shares a
  code path with flat-footed in `resolve_target_state()`.
- **`on_hit.register_default_handlers()` is dead code**, called from nowhere, so
  even the stub dispatch never runs. Worth deleting or wiring when item 1 is
  picked up.
- **`armor.2da` is not in the data spec.** `ArmorEntry` carries exactly `id` and
  `cost`. Armour AC tier, the max-Dex cap and armour-derived arcane spell
  failure all live in that 2da, and it is not in the test data either.
- **`baseitems.2da` `MaxRange` is loaded but unused**, because the column's unit
  is undocumented and plainly not metres (a longsword carries 100, a longbow
  200, and one row holds `*`). Ranged range gating waits on knowing the unit.

## Shared blocker: effect expiry — FIXED 2026-11-20

`Effect::duration` was stored and never acted on, so every effect was permanent
regardless of its duration. It turned out `EventType::remove_effect` was already
fully implemented in `EventSystem::process()` and simply never scheduled;
`EffectSystem::apply_to()` now schedules it. Durations are seconds, zero or
negative means permanent, and the condition constructors in `nwn1.effects` take
one. `expire_day`/`expire_time` stay unused — there is no world calendar, and
expiry runs off the tick clock instead.

The canonical rate now lives in one place, `kernel::ticks_per_second` (60), with
`gameplay::action_ticks_per_second` an alias of it rather than a second constant
that merely has to agree.

What this unblocks: timed conditions generally, and specifically the durational
half of **Called Shot** and **Slippery Mind**. Wounding, item poison and disease
additionally need damage *recurring* per round, which is a scheduling question
rather than an expiry one — a repeating event, not a single removal.

Still open, and worth knowing: `round_ticks` in `combat_scheduler` defaults to
**60**, which at 60 ticks/second is a one-second combat round. An NWN round is
six seconds, so that default looks like it should be 360. Nothing depends on it
being wrong today because nothing drives sustained combat, but it will change
attack pacing when something does, so it wants a deliberate decision rather than
a silent fix.
