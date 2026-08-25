# NWN walkmesh corpus findings

The dedicated-server audit in `tests_ext/all_walkmesh` is the authoritative repeatable measurement. On 2026-08-22 it parsed every visible NWN walkmesh resource with no invalid files:

- 13,075 WOK resources: 683,793 vertices and 858,891 triangles;
- 1,090 PWK resources: 7,769 vertices and 7,103 triangles;
- 249 DWK resources: 7,965 vertices and 10,932 triangles.

The largest WOK contained 684 vertices and 352 triangles. Across 33 SET files, 12,303 tile definitions referenced only six models without a WOK. Those six definitions exercise the MDL AABB fallback rather than changing the common WOK path.

DWK node names classified without exceptions: 288 closed nodes, 198 `open1` nodes, 190 `open2` nodes, and two other open nodes. The first preview pass therefore bakes closed door geometry and keeps open-state switching deferred until runtime door operation exists.

`creaturespeed.2da` contained nine finite `WALKRATE` values ranging from 0 to 5.5. Row 0 (`PLAYER`) is 2.0 and remains the measured fallback; zero-valued `NOMOVE` is preserved as authored truth.

The resource corpus does not contain authored area dimensions. Detour tile-reference capacity therefore remains checked and rejected at build time rather than inferred from asset counts. Add area-dimension evidence here when a representative authored-area corpus becomes a committed extended-test input.

## Open topology validation

The resource audit proves parse coverage, not navigation-graph fidelity. Before the WOK-to-Detour conversion is considered production-proven, add a representative authored-area audit that records:

- walkable triangles and Detour polygons per connected component;
- unmatched internal walkable edges, distinguished from authored boundaries;
- candidate, linked, and rejected cross-tile portal edges;
- reachable-poly coverage from the authored entry point; and
- sampled route deviation from the Detour straight corridor on open coplanar geometry.

An observed `tall_a01_01` sample contained 160 counter-clockwise horizontal faces, 2 clockwise faces, and 94 faces degenerate in horizontal projection before surface filtering. Detour derives funnel portal left/right from clockwise polygon order. `build_nav_world` therefore normalizes each accepted polygon while remapping adjacency slots to the same geometric edges; the source geometry remains unchanged. The remaining authored-area audit must report accepted winding and projected-degenerate counts after walkability filtering across the corpus.

The ordinary tests retain curated exact, differently subdivided, unmatched, and real-WOK seam cases. The full area audit belongs in the extended/canary path because it requires authored module data and is not needed on every CI run.
