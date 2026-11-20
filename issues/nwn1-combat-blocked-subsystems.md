# NWN1 combat: subsystems blocking the last three rules

Status (2026-11-19): open, nothing implemented. Split out of
[the combat rules completion plan](closed/nwn1-combat-rules-completion.md) when
that closed. Every other item on that ledger is done; these three are blocked on
a missing subsystem or missing game data, not on rules work, and each blocker is
shared rather than per-feat. Ordered by how much new design each needs.

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
  disease are damage/condition over time. `Effect` has `duration`, `expire_day`
  and `expire_time` fields and **nothing anywhere processes expiry** — see the
  shared blocker below.

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

## Shared blocker: effect expiry

Cutting across items 1 and 3, and worth fixing on its own: **effect durations
are inert engine-wide.** `Effect::duration`, `expire_day` and `expire_time` are
stored and never processed, so every effect persists until explicitly removed.
This predates the parent issue — Haste has always behaved this way — but it now
also means the `paralyze`/`stunned`/`sleep` conditions added for flat-footed
support have no natural end. Anything with a timed condition or damage over time
needs this first.
