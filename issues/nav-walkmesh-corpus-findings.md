# NWN walkmesh corpus findings

The dedicated-server audit in `tests_ext/all_walkmesh` is the authoritative
repeatable resource measurement. On 2026-08-27 it parsed every visible NWN
walkmesh with no invalid files:

- 13,075 WOK resources: 683,793 vertices and 858,891 triangles;
- 1,090 PWK resources: 7,769 vertices and 7,103 triangles; and
- 249 DWK resources: 7,965 vertices and 10,932 triangles.

The largest WOK contained 684 vertices and 352 triangles. Across 33 SET files,
12,303 tile definitions referenced only six models without a WOK. Those six
definitions exercise the MDL AABB fallback. An empty present WOK remains
authoritative and does not fall back.

Surface policy is not uniform across object walkmeshes. Of 7,103 PWK faces,
162 use authored walkable surface rows; of 10,932 DWK faces, 72 are authored
walkable. The NWN adapter therefore applies `surfacemat` rather than assuming
all PWK/DWK faces obstruct. The remaining 6,941 PWK and 10,860 DWK faces are
non-walkable in this corpus.

DWK node roles classified as 288 closed, 198 `open1`, 190 `open2`, two other
open, and zero unclassified. The adapter builds the three supported state
streams once and assigns dense snapshot-local obstacle states. The two other
open nodes remain unused because the corpus provides no fourth serialized door
state.

The two door appearance tables contained 302 populated model rows and 254
unique model resrefs. Thirteen had no registered MDL and the same thirteen had
no matching DWK. The other 241 MDL entries had no readable payload in this
dedicated-server install, so clip families, animation bounds, and zero-duration
clip incidence are unobservable here rather than assumed absent.

`creaturespeed.2da` contained nine finite `WALKRATE` values from 0 to 5.5. Row
0 (`PLAYER`) is 2.0 and remains the measured preview fallback; authored
zero-valued `NOMOVE` remains zero.

## Recast measurement

The production input is an assembled area. Isolated WOK builds lack their
placed neighbors and are coverage/cost diagnostics only. With the selected
0.125/0.10 m configuration, one erosion cell, layer regions, no small-region
removal, six vertices per polygon, and a detail mesh, the full resource sweep
measured:

- 12,425 WOK resources with geometry;
- 11,479 built, 443 rejected, and 503 empty;
- 805,650 input triangles merged into 105,678 polygons;
- 223,819 polygon vertices and 359,098 detail triangles;
- 19,582,724 Detour payload bytes;
- 8.456 s accumulated Recast build time; and
- 359,040 height samples, 58 source-projection failures, 12,620 one-cell
  violations, and 1.4541373 m maximum isolated-resource error.

Those isolated failures require placed-area evidence. They neither select the
configuration nor authorize accepting a failed tile.

The exact 5x3 `ms_sv_gooddwarf` area assembled 15 placed WOKs, 381 surface
triangles, 150 walkable triangles, 104 active-object triangles, and one current
traversable component. The mechanical coarsest-passing selection chose 0.125 m
cells, 0.10 m cell height, one erosion cell, and detail sample distance 3.0.
It built 8 non-empty and 7 authoritative-empty tiles with zero rejects,
produced 67 polygons and 12,472 payload bytes, projected all 150 samples with
0.0957935 m maximum surface error, preserved all 150 component routes, and
took 4.7331 ms. The public flat polygon graph independently preserved all 150
projected routes with zero endpoint clamps or losses; its active/spare reserved
capacity was 10,776 bytes and construction took 0.004158 ms. The synthetic
32x32 flat area took 740.003 ms total for 1,024 tiles, stored 381,952 Detour
bytes, and reserved 187,408 graph bytes. Cold-build time varies with machine
load; these are recorded observations, not a realtime guarantee.

## Radius and coarse-route inputs

The appearance audit observed 15,100 rows: 838 finite `PERSPACE` values and
14,262 missing values. Finite values range from 0.01 m to 6.0 m and collapse to
23 erosion-cell classes at 0.125 m after adding 0.1 m. Missing rows remain an
explicit input-policy issue; no name-based or zero-radius inference is used.

SET path-node retention observed 12,303 rows with no missing values, one
multi-byte malformed value, no unknown one-byte value, and only quarter-turn
orientations. All `A-Z` and `a-p` values occurred; `A` (3,694), `I` (1,651),
and `H` (1,121) dominate. These rows remain NWN-adapter audit data until exact
decoding and Recast seam validation make them safe as optional long-range
hints. The generated-polygon graph passes the current obstructed-route gates,
so enabling this second hierarchy would add work without a measured need.

Ordinary tests cover border CSR construction, seams, slopes, stacked floors,
holes, erosion, empty and missing WOK policy, active obstacles, door state/link
behavior, tile-batch rollback, route invalidation, and caller-capacity
rejection. The full resource and exact-area audits remain extended/canary work
because they require installed NWN and module data.
