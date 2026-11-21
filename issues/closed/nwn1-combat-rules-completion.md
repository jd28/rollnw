# NWN1 combat rules: completion plan

Status (2026-11-19): **closed.** Every item on the original ledger that the
current engine layer can express is implemented and regression-tested. Three
items could not be completed and are blocked on missing subsystems or missing
game data rather than on rules work; they moved to
[blocked combat subsystems](../nwn1-combat-blocked-subsystems.md) with their
blockers named. Feeds the Drafting Table's Phase 5 (`issues/server-client-plan.md`, which lives on the `server` branch)
("make combat actually express more than one game"); this doc only ever covered
rule-content completeness for the NWN1 profile, not the policy-module seam
Phase 5 is really about. Supersedes the stale gap list in the
`project_combat_features` memory.

Verification: full suite **1847 tests passing** on Linux Release, including 12
new regressions added for this work (`Smalls*` 836 -> 848). No manual or
in-client verification was performed — see Verification limits.

## What was fixed

### Correctness bugs on features that looked implemented

**Weapon Focus / Specialization / Improved Critical / Overwhelming Critical /
Devastating Critical / Weapon of Choice did nothing for any real weapon.**
`init.smalls` only ever registered the `BaseItemType(-1)` unarmed sentinel for
these six categories, so every map lookup for a real weapon missed and silently
returned 0/false. Fixed by a lazy `_ensure_weapon_category_feats()` in
`master_feats.smalls`, called on first `resolve()`/`has_feat_check()`. Also
fixed a wrong constant in the same block (`feat_epic_devastating_critical_unarmed`
where `feat_epic_weapon_specialization_unarmed` belonged).

Registration is lazy rather than eager because an eager loop over
`baseitems.count()` inside `Init.init()` broke six unrelated
equipment/visual tests. That had an independent root cause — a freshly
constructed `ResourceManager` left its registry unbuilt between
`module_pre_load` and `module_post_load`, so a resman query in that window got a
real-at-the-time "not found" that then cached permanently. Root-caused and fixed
separately in `lib/nw/resources/ResourceManager.cpp`; see
[the dedicated writeup](../smalls-load-config-early-call-corruption.md). Lazy
registration was kept regardless.

**Attacks per round were under-counted at every tier.** Found while fixing Rapid
Shot, not on the original ledger. `resolve_number_of_attacks_onhand()` computed
`ab / iter`, but the 3E/NWN progression is one attack plus one per `iter` points
of BAB *past the first*. The old formula gave a BAB 1-4 character **zero**
attacks — which made `resolve_attack_type()` return `attack_type_invalid`, so
low-level creatures could not attack at all — and one attack too few from BAB 6
up. The 4-attack and 6-attack caps already in that function are only correct for
the fixed formula, which is what confirmed the intent. Now
`1 + (ab - 1) / iter`.

**Rapid Shot and Flurry of Blows were strict downgrades.** Both registered their
-2 attack penalty and never granted the compensating extra attack
(`attacks_extra` was only ever touched by Haste). Both now add one attack per
round, gated on the mode's existing `can_use()` so the required weapon still has
to be equipped. Flurry had the identical defect and was not on the ledger.

**Vulnerability was clamped away in three places.** `damage_immunity_effect_delta()`
has always been able to return a negative delta (increase - decrease), and the
native layer supports it, but smalls discarded it at every step:
`_make_elemental_damage_result()` and `_resolve_base_damage_impl()` clamped the
percentage to `[0, 100]`; `resolve_damage_base_immunity()` seeded its
best-of-physical-types maximum at `0`, flooring any all-negative result; and
`resolve_damage_immunity()` returned `max(modifier, effect_delta)`, so a `0`
modifier erased a `-50` effect outright. Negative immunity now survives as
vulnerability, floored at `-100` (200% damage) to mirror the existing `+100`
ceiling. Immunities from different sources still do not stack; a vulnerability
combines additively with whatever immunity is present, matching how
`_make_elemental_damage_result()` already summed the two sources.

**Flat-footed was checked but never set.** Two consumers read the
`target_state` flat-footed bit — Deflect Arrows' "not flat-footed" guard and
Sneak Attack's eligibility — and nothing in the codebase ever set it, so one
check always passed and the other never fired. A creature is now flat-footed
while helpless (paralyzed, stunned, or asleep), derived by probing for those
effects rather than caching a new propset field.

**Dodge AC was kept while flat-footed.** Now suppressed in both
`_compute_ac_creature()` and `explain_armor_class()` — both, because the attack
path takes its authoritative armor class from the trace's total on one branch,
so fixing only the computation would have made the two disagree. Dex-to-AC
suppression was deliberately *not* added: NWN keeps Dex while flat-footed, and
matching NWN is the goal here.

### Features that were registered and inert

**Devastating Critical** now does something. It was registered as a has-feat
flag with no consumer anywhere. On a critical hit the target makes a Fortitude
save (DC 10 + half character level + Strength modifier) or dies.

**Death Attack** was bonus damage dice and nothing else; the save-or-die that
defines the feat was missing. Now a Fortitude save (DC 10 + assassin level +
Intelligence modifier) or death. NWN implements only the death variant, so the
3E paralysis alternative is deliberately absent.

Both were extracted into named `resolve_devastating_critical()` /
`resolve_death_attack_kill()` functions rather than left inline, so the rule is
testable without controlling the d20.

**Improved Parry** (feat.2da row 18, `ImpParry`, Int 13) was declared in
`constants.smalls` and referenced nowhere. It grants +4 to Parry skill checks —
**not** an extension of Parry's per-round attempt limit, which is what this
plan's earlier draft assumed. Registered through `mod_type_skill`, which makes
it live in combat's parry resolution via
`resolve_skill_check` -> `get_skill_rank_full`.

**Base-tier Resist Energy** now resolves through `mfeat_resist_energy`, which
had been declared and never populated. The epic tier deliberately stays a
feat-range scan: ten graded ranks per element cannot be expressed by a
one-feat-per-key map.

**Three master-feat ids carry no mechanic by design**, and are now documented as
such in `constants.smalls` instead of looking unimplemented:
`mfeat_arcane_defense` (needs spell school threaded into saving throws, which
arrives with the casting layer), `mfeat_epic_spell` (a grouping row only — NWN
attaches no modifier to it), and `mfeat_resist_energy_epic` (not expressible,
above). **The ids were not deleted**: they mirror `masterfeats.2da` row order,
so renumbering them would desync the mapping from game data.

### Systemic guard

The audit's own observation was that several findings shared one shape — a
master-feat category whose registrations and whose consumers disagree, so
`resolve()` can only ever return 0 while the feature looks implemented.
`master_feats.validate()` now makes that shape mechanically detectable: it
reports any category that has a bonus but no registrations, registrations but no
bonus, or is documented-inert yet carries either. It separately asserts that
every weapon-keyed category registers real base items and not just the unarmed
sentinel — which is the exact check that would have caught the headline bug
without needing an audit. `Nwn1MasterFeatRegistrationsAreSelfConsistent` fails
with the offending category id rather than a bare count.

### New engine capability this required

**Hit points were never applied.** `resolve_attack()` produced a `damage_total`
that every caller discarded; `commit_attack_effects()` applied effect intents
only, and was itself dead code called from nowhere. A resolved attack therefore
had no consequence at all. Added:

- `hitpoints.apply_damage()` / `heal_damage()`, which write the health propset
  and sync native vitals — the mirror that `combat_scheduler`'s
  `hp_current <= 0` is-dead check actually reads.
- An **optional** `commit_attack_damage` combat-policy hook, resolved by name
  like the existing policy functions. Optional on purpose: a policy without it
  behaves exactly as every profile did before, so no other profile is broken.
- `nw::combat::commit_attack()`, which applies damage and then effect intents
  (that order, so an effect keyed on the target being dead sees post-damage
  state) and sets `AttackData::is_killing_blow`.
- Both scheduler event callbacks now resolve *and* commit, instead of resolving
  and throwing the result away.

**A status-effect vocabulary.** There were 20 registered effect types and none
of them were conditions. Added `death`, `paralyze`, `stunned`, and `sleep`,
registered through the existing smalls-side effect metadata and dispatched by
`on_effects`. Death drops the creature to 0 hit points; the three helpless
conditions carry no apply/remove bookkeeping because their presence *is* the
state, probed by `combat_primitives.is_helpless()`.

These four ids are allocated from a private range (900+) and are **not** NWN
engine effect ids, unlike every id above them in `constants.smalls`. The engine
ids for these conditions could not be confirmed against game data, and guessing
them would bake in a silent mismatch. Effects are runtime-only — the nwn1 GFF
propset policy carries no effect payload — so renumbering onto the engine ids
later is a contained change with no save-compatibility cost.

**Evasion / Improved Evasion**, via a new `saving_throws.resolve_reflex_damage()`
— the seam where a Reflex save becomes a damage amount, which is the only place
Evasion means anything. No feat: half on save, full on failure. Evasion: none on
save. Improved Evasion: none on save, half on failure.

**Ammunition consumption.** A ranged attack now spends one unit per shot whether
or not it lands, and an empty quiver auto-fails the attack before anything is
rolled. Thrown weapons (dart, shuriken, throwing axe) draw from the weapon stack;
arrows/bolts/bullets from their equipment slots.

**Three baseitems.2da columns the data spec never loaded** — `MaxRange`,
`AmmunitionType`, `ArcaneSpellFailure` — are now in `BaseItemRules` with
accessors. `load_config!` materializes data specs straight from the 2da at
runtime, so no config regeneration was needed. `arcane_spell_failure` is
verified real (small shield 5, large shield 15, tower shield 50 — matching NWN)
and is summed over equipped gear by `equipment.arcane_spell_failure()`.

## Deliberately not done, with reasons

- **Ranged range gating** is not implemented even though `MaxRange` is now
  loaded. The column's unit is documented nowhere and is plainly not metres
  (a longsword carries 100, a longbow 200, and one row holds `*`). Gating
  attacks on a guessed unit would be worse than not gating them.
- **Armour-derived arcane spell failure** is excluded from
  `equipment.arcane_spell_failure()`. In NWN it comes from `armor.2da` keyed by
  the armour's AC tier; that column is not in the `ArmorEntry` data spec, and
  `armor.2da` is not in this repo's test data, so it could not be verified.
- **Dex-to-AC suppression while flat-footed** — NWN keeps Dex. See above.
- **Mounted combat** — absent from base NWN1 too. Its absence is correct.

**Correction (2026-11-20):** the original plan listed "called shots, Point
Blank / Precise Shot / Rapid Reload / Deadly Shot" as "absent from base NWN1
too; their absence is correct", and this record repeated it. That was wrong on
the facts: Called Shot is `feat.2da` row 5 (`CShot`) and Point Blank Shot is row
27 (`PBShot`), both in base NWN1. Point Blank Shot does carry no mechanic of its
own in NWN — it is a prerequisite feat — so nothing is owed there, but Called
Shot has real mechanics (targeting a limb for a penalty and an effect) and is a
genuine gap. It is tracked with the rest of the feat backlog in
[blocked combat subsystems](../nwn1-combat-blocked-subsystems.md).

## Verification limits

- Automated only. Nothing was exercised in the client or against real game
  data; the repo carries 13 2das and no module, so "matches NWN" rests on the
  2da columns present plus rules knowledge, not on observed in-game behaviour.
- **The two save-or-die rules are not pinned end-to-end.** `resolve_attack()`
  cannot be driven to a guaranteed critical hit, and `roll_uniform_int` is
  native with no seam for seeding. The tests pin the extracted rule functions
  (never fires without the feat; reachable with it; DC formula) and the
  Devastating Critical data wiring, not a full crit-to-corpse path.
- Evasion and the save-or-die rules are tested by outcome *set* over 400/200
  trials rather than per-roll, for the same reason. These tests are
  probabilistic: they rely on a natural 1 and a natural 20 both appearing, which
  is overwhelmingly likely at those trial counts but not guaranteed.
- `commit_attack()` is covered by the damage-application and death-effect unit
  tests; the scheduler callbacks that now call it are not themselves covered by
  a new test. `start_auto_attack` has pre-existing coverage in
  `tests/objects_creature.cpp` that still passes.
- Ammunition consumption is tested for the melee no-op and the empty-quiver
  refusal. The decrement-a-real-stack path is not covered: no fixture in the
  repo has a quiver equipped.
- The new effect-type ids are provisional, as described above.
- Effect **durations remain inert engine-wide**. `Effect::duration`,
  `expire_day` and `expire_time` are stored and nothing ever processes expiry,
  so the new conditions — like Haste before them — persist until explicitly
  removed. This is pre-existing and affects every effect type equally, but it
  means "paralyzed" currently has no natural end.

## Evidence

| Rule | Regression |
| --- | --- |
| Attack progression, including the zero-attack low-BAB case | `Nwn1AttackCountProgressionIsOneBasedNotFloorDivision` (two fixtures: epic for the caps, weak for the low end) |
| Rapid Shot / Flurry extra attack; Power Attack unaffected | `Nwn1RapidShotAndFlurryGrantTheirExtraAttack` |
| Improved Parry +4, Parry-only | `Nwn1ImprovedParryAddsFourToParrySkill` |
| Base Resist Energy through mfeat, element-specific | `Nwn1ResistEnergyResolvesThroughMasterFeat` |
| Master-feat registration consistency; weapon categories carry real base items | `Nwn1MasterFeatRegistrationsAreSelfConsistent` |
| Vulnerability survives; positive immunity unchanged | `Nwn1NegativeDamageImmunityIsVulnerability` |
| Helpless -> flat-footed; dodge AC lost; trace agrees with computation | `Nwn1HelplessTargetIsFlatFootedAndLosesDodgeArmorClass` |
| Flat-footed reaches `target_state` | `Nwn1HelplessTargetReportsFlatFootedTargetState` |
| Damage application, overkill clamp, healing, death effect | `Nwn1DamageApplicationAndDeathEffectReachHitPoints` |
| Evasion / Improved Evasion outcome sets | `Nwn1EvasionChangesReflexDamageOutcomes` |
| Devastating Critical wiring + DC; Death Attack gating + DC | `Nwn1SaveOrDieRulesRequireTheirFeatAndCanFire` |
| Ammunition consumption; shield arcane spell failure | `Nwn1RangedAttacksConsumeAmmunition` |
| Weapon-category feats for real weapons (pre-existing) | `Nwn1WeaponCategoryMasterFeatsResolveForRealWeapons` |
