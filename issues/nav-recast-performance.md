# Tiled Recast navigation performance

Status: every recorded release-platform hot gate passes.

Measurements were taken on 2026-08-27 in release `-O3` on desktop x86-64
(Ryzen AI 9 HX 370). Google Benchmark p95 is the nearest-rank statistic across
ten repeated timing windows. The measured radius class uses 0.125 m cells and
one erosion cell; zero-radius timings are not used for the gate.

| Transform | Measured p95 | Gate | Result |
| --- | ---: | ---: | :---: |
| Direct path, one request | 3.757 us | <= 5.5 us | pass |
| Closed-door tagged-link path, one request | 7.452 us | <= 25 us | pass |
| 300 m long-wall detour, one request | 18.242 us | <= 25 us | pass |
| 300 m long-wall detour, 128 requests | 0.598 ms | <= 3.2 ms | pass |
| Door-sized seam obstacle, four rebuilt tiles | 1.379 ms | <= 2.0 ms | pass |

The obstructed fallback uses a flat generated-polygon graph: contiguous
references and centers, edge CSR, tile/polygon-to-node rows, and stamped A*
scratch. Detour still owns projection, direct raycast, straight-path funnel,
and movement. Requests resolving to the same start/end polygons reuse the
corridor inside one batch while extracting their own endpoint-specific
straight paths. This replaced the measured pointer/hash-heavy Detour A* miss;
the SET path-node bytes are not needed for the current gate.

A door transition first builds all independent tile payloads without touching
the live mesh. Batches use the calling thread plus at most three short-lived
workers, then remove/add Detour tiles and rebuild the graph serially as one
transaction. Before that partitioned batch, the same one-cell four-tile case
measured 2.468 ms p95. The concurrent result above is the measured wall
latency; the measured per-tile duration counter was 4.536 ms in aggregate and
is not wall time.

Cold generation remains proportional to tile count and geometry. The exact
5x3 area selected build took 4.733 ms and stored 12,472 Detour bytes plus
10,776 bytes of reserved active/spare polygon-graph capacity. A synthetic
32x32 flat area took 740.003 ms total for 1,024 tiles and polygons, 381,952
Detour bytes, and 187,408 bytes of graph capacity; graph construction itself
took 0.060 ms. This is load/F9 work rather than per-frame work. Whole-area
generation is still synchronous and has not been claimed to meet a realtime
large-area cold-start target.

`perf` on the pre-partitioned four-tile rebuild attributed the tile cost to
standard Recast work: ledge filtering (17.81%), compact-heightfield build
(14.90%), polygon clipping/raster division (14.50%), erosion (14.28%), layer
regions (11.08%), rasterization (8.95%), detail mesh (6.64%), and contours
(5.67%). The graph was 0.56%. No custom Recast allocator or non-standard mesh
pipeline was introduced.
