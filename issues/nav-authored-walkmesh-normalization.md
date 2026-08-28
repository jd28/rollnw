# Recast authored-height normalization

Status: resolved for the exact placed-area gate; isolated-resource failures
remain diagnostic.

The input boundary is an instantiated area, not an isolated WOK resource. A
WOK is one reusable tile model. `mudl nav-audit` assembles every placed WOK
with its authored translation, quarter-turn rotation, and height, then builds
each 10 m Recast tile from all intersecting area triangles plus its erosion
border. PWK/DWK triangles remain a separate obstacle stream.

Raw Recast detail sampling displaced valid authored slopes by more than one
cell height. The tile transform now reprojects every generated polygon and
detail vertex onto the authored walkable triangle covering the same horizontal
point. If no source layer exists, two source layers are equally plausible but
more than one cell-height apart, or the quantized Detour height is not
representable, the complete tile is rejected. No nearest-layer guess crosses
that boundary.

On 2026-08-27, release `-O3` on the Ryzen AI 9 HX 370, the exact 5x3
`ms_sv_gooddwarf` audit selected 0.125 m cells, 0.10 m cell height, one erosion
cell, and detail sample distance 3.0. The selected build:

- built 8 non-empty and 7 authoritative-empty tiles with zero rejects;
- produced 67 polygons, 12,472 payload bytes, and 16 reciprocal seam links;
- projected all 150 radius-valid authored samples;
- had 0.0957935 m maximum surface error and zero one-cell-height violations;
- retained all 150 routes in the one authored component; and
- took 3.30328 ms for the 15 declared tile positions.

The full WOK sweep is intentionally harsher because each reusable resource is
built without its placed neighbors. For the selected configuration it observed
12,425 resources with geometry, 11,479 built, 443 rejected, 503 empty, 105,678
polygons, 19,582,724 payload bytes, and 8.432 s accumulated tile-build time.
Its 58 missing height projections and 12,620 one-cell violations identify
resources that require placed-area evidence; they do not override a passing
area snapshot or authorize accepting a rejected production tile.

The exact-area evidence removes the height blocker for this representative
area. It does not prove every module: additional instantiated-area corpus rows
must pass the same topology, seam, doorway, and height gates. A failure rejects
that Recast snapshot and reports navigation unavailable; it does not select a
second builder or silently change walkability.
