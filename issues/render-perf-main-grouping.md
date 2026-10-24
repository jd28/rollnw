# Main-pass attribution

Diagnostic result for [RP-02](render-perf-submission-profile.md), source `ed5aebb11`.
No production change. Evidence (external artifact: `render-perf-evidence/main-grouping.json`).

Ryzen AI 9 HX 370 / Radeon 890M, 1080p, Release / Tracy off. Three alternating
pairs per fixture; 180 warmup + 1,200 advancing frames. Six timestamps per call;
rows buffered until exit. Separate surface captures discard timing.

| Median CPU phase, ms | Max city | City | Outdoor |
| --- | ---: | ---: | ---: |
| Filter indices | 0.095 | 0.015 | 0.016 |
| Build runs | 0.030 | 0.008 | 0.004 |
| Build compatible groups | 0.129 | 0.030 | 0.048 |
| Instanced submission | 0.510 | 0.172 | 0.127 |
| Scalar submission | 0.587 | 0.368 | 0.366 |

Medians of run medians, summing six main-pass calls within each frame first.
Submission includes validation, constant construction, allocation and recording;
these timers do not separate those costs. Allocation/destruction outside the
measured scopes is excluded. Instrumentation-versus-baseline host medians:
6.109→5.895, 1.791→1.842, 0.898→0.929 ms. Per-pair changes span -6.95% to +9.38%:
run variance is not isolated from probe cost. Treat attribution as approximate,
not a speedup. Uninstrumented max GPU scene remains 3.548 ms.

| Opaque draw opportunity | Max city | City | Outdoor |
| --- | ---: | ---: | ---: |
| Existing static instanced draws | 2,146 | 802 | 554 |
| Dynamic scalar draws | 2,373 | 518 | 55 |
| Dynamic unskinned, no deformer | 2,255 | 493 | 52 |
| Compatible primitive/material keys in that subset | 115 | 115 | 47 |

Keys include shared model identity, primitive, material/override, skin and material
flags. This is a grouping opportunity, not implemented draw counts or measured
time savings. Max has 78 skinned and 40 deforming opaque surfaces outside this
subset. Its static draw has a median 21 vertices / 36 indices; material indices
are distinct across those primitives, so index equality alone cannot combine them.
Equivalent material payloads remain a separate packing investigation.

Next: test rigid animated surface instancing using the existing prepared world
and normal matrices. Max input costs 2,255 × 128 = 288,640 bytes of transient
instance rows plus grouping; no geometry duplication is needed. Existing root
instancing cannot accept these rows unchanged: it also applies the primitive's
bind transform. Preserve animated poses, ordering constraints, overrides and
fallbacks; compare complete frames, GPU, tails, memory and images before keeping it.
Retained main compatibility groups are lower priority at ~0.13 ms.

Verification: all non-timing fields equal across 10,800 matched frame pairs and
nine separate surface-capture pairs. Counts are constant throughout timing runs.
No image comparison/new production tests for temporary diagnostics. Source restored;
normal executable rebuilt and checked for absence of both probe strings.

Reproduction: timing patch (external artifact: `render-perf-evidence/main-grouping-probe.patch`),
surface patch (external artifact: `render-perf-evidence/main-surfaces-probe.patch`),
[harness](render-perf-evidence/README.md),
[capture](render-perf-evidence/README.md),
[summary](render-perf-evidence/README.md). Apply each patch separately
to the source revision, build/copy its executable, then restore/touch source and
rebuild. Scripts retain the original `/tmp/rollnw-main-groups` paths; fixture setup
is still the [RP-01](render-perf-workload-contract.md) limitation. Raw reports there.
