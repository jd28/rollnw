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

- [NWN1 combat rules completion plan](nwn1-combat-rules-completion.md) — verified coverage ledger and priority list; headline bug: Weapon Focus/Spec/Crit feats are unarmed-only for every real weapon.

## Smalls runtime

- [`load_config!` early-call corruption](smalls-load-config-early-call-corruption.md) — fixed in `ResourceManager`; root cause confirmed and closed, not just avoided.

## Engine / memory

- [Arena/memory-safety audit](arena-memory-safety-audit.md) — ranked leak findings from a full sweep of arena/scope allocation (`ObjectPool`, effects, `Module`, `Dialog`, `Palette`); fix plan written, nothing implemented yet.
