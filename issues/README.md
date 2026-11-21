# Local issues

Active issues live directly in this directory. Completed issues and their
supporting history live in [closed/](closed/README.md).

Close an issue when its stated work has completion evidence or explicit user
acceptance. Record the reason and verification limits, then move it to closed/
and update local links. Preserve the original contracts, regression evidence and
checkpoint history. Related work that remains unresolved stays active.

## Platform work

- [macOS/iPadOS port and nw::gfx Metal backend](macos-ios-metal-port-plan.md) — planning only; phased native Metal backend, macOS client/server, then touch-adapted full toolset on iPad.

## Renderer work

Start with [renderer status and next work](rendering-performance-overhaul.md)
and [workload requirements](render-perf-workload-contract.md). The status page
owns the current priority and unresolved backlog; it replaces the old collection
of open performance experiments as a work queue.

The [performance evidence archive](render-perf-evidence/README.md) retains completed,
rejected and deferred investigations. Archived status does not imply a problem
was solved. Their historical plans/checklists do not authorize new work; remaining
risks and deferred opportunities are explicit on the current status page.

## Combat / rules work

- [Getting game content out of the test data](test-data-game-content-removal.md) — `tests/test_data/` is
  163 MB and carries complete game TLKs, models, haks and rule tables. Synthetic fixtures are also the
  stronger test for data-driven code; migration tiers plus `probe_nwn_install()` + `GTEST_SKIP()` for
  what cannot be synthesised.
- [Spatial queries](spatial-queries.md) — plan for "what is near this point", the capability attacks of
  opportunity, reach, Cleave, Mobility and firing-into-melee all wait on. Recommends a per-area array
  sorted by `position.x` (sweep and prune, as NWN did) over a 10 m tile grid, because combat radii are
  ~2 m. Carries a declared workload contract, eight registered benchmarks, and a stress plan whose
  differential oracle is the retained phase-0 linear scan.
- [Blocked combat subsystems](nwn1-combat-blocked-subsystems.md) — what is left of the combat ledger,
  grouped by shared blocker: the `iprp_onhittype` data, spatial/movement events for attacks of
  opportunity, a cast pipeline, and a missing initiative system. Effect expiry is inert engine-wide and
  blocks two of them. Also carries the `feat.2da` backlog and the modifier types with no dispatch.
- [NWN1 combat rules completion plan](closed/nwn1-combat-rules-completion.md) — closed 2026-11-19;
  everything the engine layer can express is implemented and regression-tested.

## Smalls runtime

- [`load_config!` early-call corruption](smalls-load-config-early-call-corruption.md) — fixed in `ResourceManager`; root cause confirmed and closed, not just avoided.

## Engine / memory

