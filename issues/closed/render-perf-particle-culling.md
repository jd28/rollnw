# RP-14: Off-screen billboard batches

Status: closed, 2026-10-07. [Parent](../rendering-performance-overhaul.md).

Reject off-screen billboard batches before texture lookup, sorting, uploads and
draws. Reuse area frustum math, calculated once per scene particle pass. Bound
live particles, not emitter owners; simulation remains active for re-entry.
One intersecting particle retains the batch without scanning the remainder.

Contract/cost: borrowed packet indices read live position/size columns and return
one visibility decision; synchronous lifetime, linear indexed reads, stack bounds,
no allocation or retained cache. Four billboard modes are supported. Unsupported
modes, malformed data and invalid frusta retain the batch; empty valid batches
contain no geometry. Sizes clamp to the renderer's minimum. No asset-name rules.
Observed city input: 172 packets / 5,648 particles; 112 packets contain 40 particles.

Matched baseline `e36753ee4`, Ryzen AI 9 HX 370 / Radeon 890M, 1080p Release,
Tracy/validation off during timing. Three alternating pairs, 180 warmup + 1,200
advancing frames each; medians of per-run statistics, no excluded runs:

| View | Host median ms | Host p95 ms | Particle CPU median ms | Particle draws |
| --- | ---: | ---: | ---: | ---: |
| Full-area overview | 4.165 → 4.142 | 5.664 → 5.381 | 0.380 → 0.383 | 172 → 172 |
| Elevated player | 2.345 → 2.143 | 3.511 → 3.649 | 0.388 → 0.096 | 172 → 20–21 |
| Legacy city control | 1.738 → 1.680 | 3.359 → 2.941 | 0.100 → 0.089 | 43 → 39 |
| Outdoor control | 0.954 → 0.928 | 1.946 → 1.942 | 0.020 → 0.019 | 11 → 9 |

Player host/particle medians improve in all three pairs; whole-frame variation
is substantial and player p95 remains unresolved. Overview pair results are mixed:
no established total-frame improvement there. GPU scene medians: overview
1.447 → 1.446 ms, player 1.492 → 1.468 ms. No cross-session additive claims.

Verified: 340 tests; 14,400 timing and 480 capture frame pairs preserve logical
state/non-particle commands; total command changes equal particle-pass changes.
Sixteen validation captures have no warnings/errors. Overview/outdoor images are
exact; player/city differences fit same-binary baseline repeat ranges (6–9 baseline
captures, 2–3 final candidate captures). This is not pixel-exact city acceptance.
Actual toolset UI, moving gameplay and frame tails remain under RP-01/06/08.

Raw runs, source/binary hashes, reproduction scripts and visual investigations:
`/home/josh/projects/rollnw-evidence/2026-10-07/particle-cull.tar.gz`.
