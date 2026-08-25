# Navigation cache lifetime and persistence

Do not add revision tracking or a disk format until cold navigation construction exists and is measured. After the Detour build stage, compare cold construction with cached preview entry on representative and largest available areas. Add only an in-process area-revision cache if the measured entry latency requires it; version a disk format only if process-local caching is insufficient.

## 2026-08-22 measurement

Release benchmarks on Linux x86-64 measured:

- synthetic Detour construction for 1,024 NWN-sized tiles and 2,048 triangles: 0.350 ms median;
- complete cold preview entry for the committed 4x4 DockerDemo area, including walkmesh extraction, blocker construction, actor cloning, and Detour construction: 10.3 ms median;
- one fixed preview tick: 31.0 ns median; six fixed ticks: 167 ns median;
- one renderer spatial-row update over a 573-model synthetic scene: 11.5 us median.

These measurements are evidence only for the listed shapes and this machine. No representative large authored area is committed to the test corpus, so large-area cold entry remains unmeasured. The current data does not justify cache state or revision tracking; cold construction remains the straight-line path. Reconsider this issue only after a representative large area is available to the benchmark harness or measured interaction shows an entry-latency problem.
