# NWN1 combat rules: completion plan

Status (2026-11-17): headline finding (P0 weapon-category bug) fixed and
regression-tested on `combat/weapon-feat-wiring` (branched from `main`, not
`server`); P1+ items below remain plan only. Feeds
[the Drafting Table](server-client-plan.md)'s Phase 5 ("make combat actually
express more than one game") — this doc only covers rule-content completeness
for the NWN1 profile; it does not cover the policy-module seam Phase 5 is
really about. Supersedes the stale gap list in the `project_combat_features`
memory; this ledger is verified against current source, that one was not.

## Headline finding

**Weapon Focus, Weapon Specialization, Improved Critical, Overwhelming
Critical, Devastating Critical and Weapon of Choice do nothing for any real
weapon — only unarmed strikes get these bonuses.** This is a correctness bug
in already-shipped code, not a missing feature, and it silently defeats a
large piece of the fighter-type feat tree for every character except monks.

- `baseitems.2da` carries a per-base-item feat id for each of these categories
  (`weapon_focus_feat`, `weapon_specialization_feat`,
  `weapon_improved_critical_feat`, `epic_weapon_overwhelming_critical_feat`,
  `epic_weapon_devastating_critical_feat`, `weapon_of_choice_feat`, plus their
  epic variants), loaded into `BaseItemRules` and exposed by accessors in
  `lib/nw/smalls/scripts/nwn1/baseitems.smalls:227-288`
  (`weapon_focus_feat(base_item)` etc.).
- `master_feats.resolve()`/`has_feat_check()`
  (`lib/nw/smalls/scripts/nwn1/master_feats.smalls:35-95`) look up the feat for
  a category by `base_item as int`. `combat.smalls:231` and friends already
  call `mfeat.resolve(obj, Const.mfeat_weapon_focus, base_item)` for real
  weapon attacks — the call sites are live.
- But `lib/nw/smalls/scripts/nwn1/init.smalls:829-835` only ever calls
  `mfeat.add(..., BaseItemType(-1), ...)` — the unarmed sentinel — for these
  six categories. There is no loop over real base items, so the map lookup for
  a longsword, greatsword, or anything else never matches and silently
  returns 0/false.
- Separate data bug in the same block: `init.smalls:832` registers
  `mfeat.add(Const.mfeat_weapon_spec_epic, BaseItemType(-1),
  Const.feat_epic_devastating_critical_unarmed)` — wrong feat constant; it
  should be `feat_epic_weapon_specialization_unarmed`, which is otherwise
  referenced nowhere in the codebase.

Fix shape as implemented: `baseitems.count()`/`baseitems.is_weapon(base_item)`
already existed, no new infrastructure needed. The registration loop ended up
living in `master_feats.smalls` as a lazy `_ensure_weapon_category_feats()`,
called from `resolve()`/`has_feat_check()` on first real use — **not** an
eager loop inside `init.smalls`'s `init_master_feats()`, which was the first
thing tried. Fixed the `feat_epic_devastating_critical_unarmed` typo
(should be `feat_epic_weapon_specialization_unarmed`) in the same pass.

**Why lazy, not eager — a second, independent finding.** An eager loop over
`baseitems.count()` inside `Init.init()` is logically correct (verified: the
real per-base-item data comes back right) but broke 6 unrelated tests
(`Nwn1EquipCallbacks*`, `Nwn1HumanoidMirrors*`, `Nwn1ItemResolveVisual*`,
`Nwn1StandaloneItemVisual*`, `Nwn1EquipmentArmorClassProducer*`) — confirmed
by bisection down to a single bare `baseitems.count()` call with zero `mfeat.add`
calls attached. The cause is still open: forcing `baseitems` to load during
`Init.init()` itself, rather than at its previous first-use point (the first
real call into `nwn1.baseitems` from combat/equipment code), corrupts
something those tests depend on — not `baseitems`' own data (checked directly:
correct in both cases), so something downstream of *when* it loads, not *what*
it loads. Moving the registration to run lazily on first `resolve()`/
`has_feat_check()` call keeps `baseitems`' first-load point exactly where it
already was and all 199 `Smalls*` suite tests pass. This is a workaround, not
a root-cause fix. Root cause investigated separately and narrowed to a strong,
not-yet-confirmed lead (a `load_config!`-backed module's data getting
permanently cached empty when first touched too early in native module-load
sequencing) — see
[the dedicated writeup](smalls-load-config-early-call-corruption.md). Could
bite the next thing that eagerly touches `baseitems` (or similar lazily-loaded
config) from `Init.init()`; out of scope for this doc beyond the cross-link.

## Coverage ledger

Verified 2026-11-17 against `lib/nw/smalls/scripts/{nwn1,core}/*.smalls` (combat,
combat_modes, combat_primitives, master_feats, on_hit, modifier, creature,
saving_throws, baseitems, rules, constants, init, special_attack, effects) plus
a project-wide sweep for TODO/FIXME/stub markers. "Evidence" cites the file
proving the status; see git history for exact current line numbers if they
drift.

| System | Status | Evidence | Note |
| --- | --- | --- | --- |
| Weapon Focus/Spec/Improved Crit/Overwhelming Crit/Devastating Crit/Weapon of Choice, real weapons | **Fixed** (`combat/weapon-feat-wiring`) | `master_feats.smalls` (`_ensure_weapon_category_feats`), regression: `Nwn1WeaponCategoryMasterFeatsResolveForRealWeapons` | Headline finding above. |
| Sneak Attack, Favored Enemy damage, Massive Criticals, Overwhelming Critical (unarmed), Knockdown | Implemented | `combat.smalls` (`_resolve_base_damage_impl` and neighbors) | |
| Devastating Critical | Registered, inert | `init.smalls:822-824`; no `resolve()`/`has_feat_check()` call site found | Self-documented: `init.smalls:822-823` TODO says dice/damage behavior was deliberately deferred. |
| Two-weapon fighting (penalties, TWF/Improved TWF, Ambidexterity, off-hand resolution) | Implemented | `combat.smalls:521-550,1149-1159,1608-1613` | |
| Concealment / miss chance, Blind-Fight reroll, Epic Self Concealment | Implemented | `modifier.smalls:846-872`, `combat.smalls:814-828,2287-2436` | |
| Damage immunity / reduction / resistance, NWN-correct ordering | Implemented | `combat.smalls:1270-1935` | immunity → reduction → resistance, charge-consuming effects handled. |
| Vulnerability (negative immunity, extra damage taken) | **Missing — clamped out** | `combat.smalls:1876,1753` clamp negative immunity to 0 | Native layer (`core_combat.cpp:28-40`) already supports a negative delta; smalls throws it away. |
| Size → AB/AC modifiers | Implemented | `creature_size.smalls:33-49`, `modifier.smalls:613,690` | |
| Reach weapons | Missing | no hits anywhere | Consistent with no AoO (below). |
| Mounted combat | Not in scope | no constants, no logic | Appears intentionally absent from the current ruleset, not a gap to close. |
| Epic Prowess, Armor Skin, Dodge, Toughness, DR, Energy Resist, Great Ability, Skill/Spell Focus, Bane of Enemies | Implemented | `init.smalls` (various), `combat.smalls:2487-2497` | |
| Knockdown / Improved Knockdown | Implemented | `combat.smalls:2510-2518` | |
| Death Attack | **Partial** | `combat.smalls:2521-2526` | Grants bonus damage dice only; the actual save-or-die/paralysis effect that defines the feat is missing — no Fortitude save call found. |
| Smite Good/Evil | Implemented | `init.smalls:273-311` | |
| Rapid Shot — AB penalty | Implemented | `init.smalls:227-229,345-347` | |
| Rapid Shot — extra attack | **Bug — net downgrade** | `combat.smalls:419-421,1060,1097,1167,2746`: `attacks_extra` only ever set by Haste | The -2 AB penalty applies; the compensating extra attack never does. As implemented, taking Rapid Shot makes a character strictly worse. |
| Ammo consumption, firing-into-melee penalty, range falloff | Missing | no hits | |
| Point Blank Shot, Precise Shot, Rapid Reload, Deadly Shot, called shot | N/A | not part of base NWN1 ruleset | Correctly absent, not a gap. |
| AC: natural/armor/shield/deflection (max-of-effects), dodge (sum), Dex cap | Implemented | `modifier.smalls:594-650` | |
| AC: dodge bonus lost while flat-footed | **Missing** | `modifier.smalls:609-653` applies dodge unconditionally; flat-footed only consulted elsewhere (Deflect Arrows, Sneak Attack) | |
| Saving throws (3 saves, class bonus, effects, NWN-faithful flat-sum stacking) | Implemented | `saving_throws.smalls` | |
| Evasion / Improved Evasion | Missing | no hits | |
| Concentration checks, AoO while casting, somatic/spell-failure from armor | Missing | `spells.smalls`/`metamagic.smalls` are metadata-only | The "casting is an interruptible combat action" layer is unbuilt, separate from melee/ranged resolution. |
| Attacks of opportunity | Missing | no concept anywhere, smalls or native | |
| On-hit weapon properties (Vorpal, Wounding, etc.) | **Partial — framework only** | `on_hit.smalls`: dispatch is real, default handlers are no-op stubs | Already tracked (see prior conversation); restated here for completeness. |
| Improved Parry (extends Parry's per-round limit) | Registered, never referenced | `constants.smalls:378`; base Parry limit in `combat.smalls:1174-1182` | |
| `mfeat_arcane_defense`, `mfeat_epic_spell` | Dead | `constants.smalls:586,593` | Declared, never `add`'d or resolved. |
| `mfeat_resist_energy[_epic]` | Dead, superseded | `constants.smalls:587,596` | Base/epic Resist Energy is instead computed by direct `Cre.has_feat` in `init.smalls:559-561`, bypassing the mfeat system entirely — not wrong, just makes the mfeat category pure dead weight. |

## Priorities

**P0 — correctness bugs on features assumed to already work.** Do these
first; everything else is a missing feature, these are features silently not
doing what the code already claims:
1. ~~Weapon Focus/Spec/Improved Crit/Overwhelming Crit/Devastating Crit/Weapon
   of Choice unarmed-only bug~~ — fixed on `combat/weapon-feat-wiring`.
2. Rapid Shot's missing extra attack — either grant the attack (match Haste's
   `attacks_extra` mechanism) or pull the mode until it does; shipping a feat
   that makes the character worse is worse than not having it.

**P1 — small, same-shape wiring (the Devastating Critical pattern).** Each is
one `resolve()`/`has_feat_check()` call site added where data already exists,
or one dead registration removed:
3. Devastating Critical's actual effect (the `init.smalls:822-823` TODO points
   at the real reason: needs dice/damage result plumbing, not just a flag).
4. Improved Parry's limit extension.
5. Decide and act on `mfeat_arcane_defense`/`mfeat_epic_spell`/
   `mfeat_resist_energy[_epic]`: wire them for real, or delete the dead
   categories so they stop looking implemented.

**P2 — moderate, self-contained gaps:**
6. Vulnerability (stop clamping negative immunity to 0; native support exists).
7. Dodge AC suppression while flat-footed.
8. Death Attack's actual save-or-die/paralysis effect.

**P3 — larger, genuinely new systems:**
9. On-hit weapon properties (Vorpal, Wounding, etc.) — framework exists, needs
   real per-property handlers.
10. Attacks of opportunity (and reach) — touches positioning/triggers, not
    just damage math; likely the largest single item here.
11. Evasion / Improved Evasion.
12. The casting-in-combat layer: concentration checks, AoO while casting,
    armor spell-failure. Currently `spells.smalls`/`metamagic.smalls` hold only
    metadata — this is closer to Phase 3's event-loop work than to the rest of
    this ledger, and may belong there instead of here.
13. Ammo consumption, firing-into-melee penalty, range falloff.

**Explicitly out of scope, not gaps:** mounted combat, called shots, Point
Blank/Precise Shot/Rapid Reload/Deadly Shot — none of these exist in base NWN1
either; their absence here is correct, not incomplete.

## Systemic observation

Three of the findings above (Devastating Critical, the weapon-category mfeat
data, two dead mfeat categories) are the same shape: a feat gets registered
through `master_feats.smalls` but nothing ever calls `resolve()`/
`has_feat_check()` for it, or vice versa — a live call site with no matching
data. This is cheap to catch mechanically: a startup consistency check in
`master_feats.smalls` (or a test) that, after `init()` runs, asserts every
`MasterFeat` category with at least one `add()` call also has at least one
resolve/has-check call site logged against it (and flags the reverse). That
doesn't fix any of the gaps above, but it would have caught the weapon-category
bug immediately instead of needing an audit to surface it, and it's a small
addition on top of a system that already exists — worth doing alongside P0/P1,
not instead of them.

## Build sequence

1. P0 fixes, each with a focused regression test per feat category confirming
   the bonus now applies for a real weapon (not just unarmed).
2. P1 items.
3. Add the consistency check from Systemic observation; run it against current
   `init()` registrations to confirm no further instances exist.
4. P2 items, each independently testable.
5. P3 items are separate pieces of work, not a single slice — pick one at a
   time per the gated-work discipline the broader plan already uses elsewhere.
