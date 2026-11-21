# Getting game content out of the test data

Status (2026-11-20): **in progress.** Tier 1 (the rule tables), Tier 2 (the
TLKs), nwsync, the standard palettes and the NWN2 hak are done;
`tests/test_data/` is 163 MB -> 99 MB, of which 67 MB is glTF samples that stay.
What remains is the hard tier: ~13 MB of models and textures, none of which the
server install provides. Raised when adding on-hit weapon
property tables: the right fixture turned out to be a *synthetic* 2da carrying
the real labels at deliberately different row ids, which tests the data-driven
property harder than the shipping table would. That pattern should spread —
`tests/test_data/` currently carries a large amount of shipped game content.

## What is in there

`tests/test_data/` is **163 MB**. The clearly game-derived parts:

| Item | Size | Note |
| --- | --- | --- |
| ~~`root/lang/{en,de}/data/dialog.tlk`, `dialogf.tlk`~~ | ~~40 MB~~ | **removed 2026-11-20** |
| ~~`user/nwsync/`~~ | ~~13 MB~~ | **removed 2026-11-20**, see below |
| `renderer/nwn_dump_user/` | 13 MB, 39 files | game models and textures |
| `renderer/MetalRoughSpheres`, `DamagedHelmet`, `CesiumMan` | 67 MB | **keep** — open-source glTF sample assets, not game content |
| ~~`user/hak/westgate2.hak`~~ | ~~7.9 MB~~ | **removed 2026-11-21** — NWN2 content (`HAK V1.1`), see below |
| `a_ba.mdl`, `c_orcus.mdl`, `a_fa.mdl`, `c_aribeth.mdl` | ~10 MB | game models |
| ~~`placeables.2da`~~ | ~~2.9 MB~~ | **removed 2026-11-21** |
| ~~`feat.2da`, `spells.2da`~~ | ~~1.3 MB~~ | **removed 2026-11-21** |
| ~~`baseitems`, `classes`, `racialtypes`, `skills`, `polymorph`, `tilecolor`~~ | | **removed 2026-11-21** |
| `.utc` / `.uti` blueprints | | **keep** — 58 of 62 are not in the install; see below |
| `.itp` standard palettes | ~79 KB | **removed 2026-11-21** |
| `.dds` / `.tga` textures | | |

Some fixtures are already synthetic and should be the model: the build generates
42 one-triangle part models from a single template, and `data_spec_rows.2da`,
`empty.2da`, `bad_rules.set` and `bad_doors.set` are hand-authored.

## Why it matters beyond licensing

A real table is a *worse* test for data-driven code. The on-hit work is the
concrete example: its fixture places Vorpal, Sleep and Stun at different rows
than the game does, so anything hardcoded to the real ids fails immediately.
Against the shipping table, a hardcoded implementation passes. The synthetic
fixture is the stronger assertion.

Real tables also hide gaps. `iprp_onhit.2da` has an empty row in the middle,
which produces a default-constructed `load_config!` entry whose string fields
are unreadable. A minimal fixture can include that shape deliberately instead of
relying on the game happening to have one.

## nwsync: removed outright

Not a fixture question. `docs/structure/resources.rst` already records that
nwsync support was removed deliberately -- "not applicable to module/persistant
world development, nor do I see it as having any 'future tense'" -- and there is
no nwsync implementation anywhere in `lib/`. The 13 MB of
`nwsyncdata_0.sqlite3` / `nwsyncmeta.sqlite3` was loaded by nothing: resman
never mounts that directory and no test referenced it.

Three stale references went with it:

- `docs/index.rst` still advertised "a Resource Manager that can load all NWN
  containers (e.g. erf, key, nwsync)", directly contradicting `resources.rst`.
- `kernel/Config.hpp` documented `include_user` as overriding an
  `include_nwsync` option that does not exist.
- `ResourceManager.hpp` carried a TODO listing `nwsync_savegame` as unhandled,
  implying a gap where there is a decision.

`tests/test_data/user/settings.tml` keeps its nwsync sections on purpose: it is
a 69 KB fixture for parsing a *real* settings file, and the game writes those
keys whether or not this engine honours them. Dropping them would weaken the
parser test rather than remove game content.

## Migration, easiest first

**Tier 1 — rule tables. DONE 2026-11-21.** Diffing each test copy against the
install decided it, rather than guesswork: `baseitems`, `classes`, `placeables`,
`racialtypes`, `skills`, `spells` and `tilecolor` were **byte-identical** to the
install's, and `polymorph` identical modulo trailing whitespace. All eight were
pure duplication and are gone. No synthetic replacements were needed, because
the server install provides them with real bytes.

Three fixtures stay, and are genuinely synthetic rather than trimmed copies:
`cloakmodel.2da` (different columns entirely from the game's — a deliberate
parsing stub), `legacy_classes.2da` and `data_spec_rows.2da` (not in the install
at all), plus `empty.2da` and the two `iprp_onhit*` fixtures.

`feat.2da` was the interesting one. It was **not** a verbatim copy: the test
version hand-corrects two rows that are malformed in the shipping data, where
the name and description strrefs had collapsed into the LABEL column
(`FEAT_EPIC_HARPER_SCOUT_83522_83543` with `**** ****` for both strrefs; same
shape at row 991). Nothing in the codebase references either feat, and the feat
count is read dynamically rather than asserted, so the copy went too -- but the
malformed rows are a real defect in the game's own data, worth knowing about
before anything starts depending on those two feats' strrefs.

Four tests were loading 2das from disk to exercise the parsers, and their
assertions encoded game facts rather than parser behaviour -- feat.2da's 1116
rows, the label in row 4, its 43rd column. They now run against
`twoda_parse.2da`, a 4-column 5-row fixture with an all-`****` row, so the
empty-cell path is covered by construction instead of by whichever row of the
shipping table happened to have gaps.

**Tier 2 — the TLK. DONE 2026-11-20.** No generator was needed: the
dedicated-server data already ships `lang/en/data/dialog.tlk`, so
`nw::test::dedicated_dialog_tlk()` points the three consuming test files
(`legacy_tlk.cpp`, `kernel_strings.cpp`, `rollnw_client_commands.cpp`) at the
install. Identical content -- same 112,177 strings, same strref 1000.

The German TLK had no install equivalent, and the test using it turned out to be
checking nothing: it saved, reloaded into `t2`, and then asserted on the
*original* `de` object, so the round trip was never verified. Replaced with a
non-ASCII round trip through the English table that does assert on the reloaded
copy -- **and it immediately failed, exposing a real bug.**

`Tlk::save_as()` computed each modified element's size from the
codepage-converted string but then wrote the *unconverted* UTF-8 bytes. Every
non-ASCII modified string was truncated and double-encoded, and every string
after it had its offset shifted. ASCII was unaffected, which is why the existing
`Tlk.Save` test passed. Fixed.

Still open in the same function, not fixed here: `save_as()` uses one
`max_strref` as both a count and an index (`ele.resize(max_strref)`,
`header_.str_count = max_strref`), so setting a strref at or beyond the current
count silently drops it, and building a table from a default-constructed `Tlk`
can fault.

## The V1.1 hak was NWN2 content

`westgate2.hak` was 7.9 MB and existed to satisfy one test, `Erf.v1_1`, which
read it and asserted `version == ErfVersion::v1_1`. Its header is `HAK V1.1`,
NWN2's ERF variant rather than NWN1's `V1.0` — and "Mysteries of Westgate" was
an NWN2 expansion, so this was content from a different game.

The test now generates its fixture: it opens the 325-byte
`hak_with_description.hak` (a purpose-built V1.0 fixture), flips `version` to
`v1_1`, saves, reloads and demands the resource back. That is **better
coverage** than before — V1.1 keys use 32-byte resrefs where V1.0 uses 16, so
the round trip exercises the width-dependent offset arithmetic on both the write
and read side, where the old test only read an externally produced file and
checked one header field.

Worth knowing when writing similar fixtures: a default-constructed `Erf` plus
`add()` plus `save_as()` does **not** round-trip — the reloaded resource comes
back empty. The first attempt here did that and looked like a V1.1 bug; it is
not. Build from an existing container, as every other `Erf` test does.

## Blueprints: diffed, and mostly *not* duplication

Worth recording as a negative result, since the 2da outcome made duplication the
expected answer. Diffing all 62 `.utc`/`.uti`/`.itp`/`.utp`/`.utd`/`.utt`/
`.uts`/`.ute`/`.utm`/`.utw`/`.dlg` files in test data against the install:
**only 4 are identical, 82 KB in total.** The other 58 are not in the install at
all — they are purpose-built fixtures (`drorry.utc`, `dexweapfin.utc`,
`spell_test_1.utc`, `dlg_with_params.dlg`, the `*palcus.itp` custom palettes)
or module content, not copies of shipped blueprints.

Three of the four identical files were the standard palettes
(`creaturepalstd.itp` 71 KB, `creaturepal.itp`, `tde01palstd.itp`), and they are
gone: `nw::Gff` already accepts `ResourceData`, and one palette test was already
demanding through resman, so the rest just needed the same treatment. No
temporary files involved.

The fourth, `nw_chicken.utc` (3 KB), is deliberately kept for now. It is used by
six test files, and while some already load it through resman
(`objects().load<Creature>("nw_chicken")`) the rest reference it by path --
mostly renderer tests that need a full install for models regardless, so they
belong to the Tier 3 skip decision rather than to this tranche. Churning six
files for 3 KB ahead of that decision is not worth it.

**What is actually left.** `renderer/` is 79 MB but 67 MB of that is glTF
sample models (MetalRoughSpheres, DamagedHelmet, CesiumMan), which are
open-source assets and stay. After the hak went, the remaining game content is
roughly 13 MB: `renderer/nwn_dump_user/` (13 MB), the
`a_ba`/`c_orcus`/`a_fa`/`c_aribeth` models and `.dds`/`.tga` textures in
`user/development/`, `renderer/equipped_bow/`, and `nw_chicken.utc`. The
blueprints have been diffed and are *not* duplication — see above — so what is
left is genuinely models and textures, all of it Tier 3.

**Tier 3 — models, textures and haks.** The hard tier, and harder than it first
looked: the server install provides none of these, so they cannot simply be
sourced from it. Options per fixture are (a) a generator, as the build already
does for the 42 one-triangle part models, (b) `GTEST_SKIP()` behind
`has_full_game_assets()` and accept the coverage only runs on a developer
machine with a full install, or (c) keep it, if it is small and genuinely
irreducible. Deciding (b) versus (c) per fixture is the actual work here.

## The two install tiers, measured

This is the fact the whole migration turns on, and it is not what the KEY index
suggests. The dedicated-server data (`scripts/ci/ensure_nwn.sh`) indexes the
full game's resources, but **ships zero bytes for every visual payload**:

| Real bytes — safe to source from the server install | Indexed but **empty** |
| --- | --- |
| `wok` 13,075 · `nss` 4,138 · `ncs` 4,089 · `uti` 2,763 · `utc` 1,582 · `2da` **597** · `itp` 75 · `dlg` 40 · `set` 33 | `mdl` **32,787** · `tga` **28,196** · `dds` **6,737** · `plt` **1,565** |

All 20 BIFs the KEY references are present on disk, and every model, texture and
PLT entry in them has size 0. So **"is it in the index" and "does the container
exist" both answer yes for resources the server install does not provide** —
only the payload tells the truth. `nw::test::has_full_game_assets()` therefore
probes by demanding `plc_a01.mdl` and checking for bytes, and
`GameInstallTiers.RulesResourcesArePresentAndVisualTierIsProbedByPayload` pins
that it tracks payload rather than index membership.

Consequence for the tiers below: rule tables, scripts, blueprints, walkmeshes,
tilesets and palettes can all be sourced from the server install that CI already
fetches. **Models, textures and PLT layers cannot** — those tests need a full
consumer install and must skip without one, so their fixtures are the ones that
need real thought rather than deletion.

**Tier 4 — two install tiers, and the requirement is already enforced.**
`tests/main.cpp` already calls `configure_dedicated_server()` and **returns 1
when `$NWN_ROOT/data/nwn_base.key` is missing**, so the suite already refuses to
run without dedicated data. `scripts/ci/ensure_nwn.sh` fetches it and CI sets
`NWN_ROOT`. What was missing is the second tier, now added as
`nw::test::has_full_game_assets()` — see the measured inventory above for what
each tier actually provides.

Worth knowing for the tiers above: because the server data has every 2da with
real bytes, including `armor.2da`, `masterfeats.2da` and the `iprp_*` tables,
Tier 1 does not have to choose between a synthetic fixture and no coverage. A
rule test that wants real breadth can read the install; a test that wants to pin
behaviour should still use a deliberately-renumbered synthetic fixture.

## Note for rule work

Feat and spell mechanics are verified against the install's
`lang/en/data/dialog.tlk` — `nw::test::dedicated_dialog_tlk()` in tests, or
`$NWN_ROOT` directly. Verify rule numbers against the game's own description
text rather than from memory; doing that caught two wrong implementations
(see [the combat backlog](nwn1-combat-blocked-subsystems.md)).
