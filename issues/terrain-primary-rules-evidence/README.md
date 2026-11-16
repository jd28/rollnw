# Terrain Painter Primary Rules — Evidence & Verification

## 1. Stock Corpus Audit

Evaluated against the installed `data/nwn_base.key` (`SHA-256: 09cdafb6dbfb9cdb544993154c514c65ef9fd5be4e0c1b8fd971ed05aff90935`), resolving all 33 `ResType::set` (2013) resources through KEY/BIF precedence:

| Metric | Value |
| --- | ---: |
| Total SET resources | 33 |
| SETs with `[PRIMARY RULES]` rows | 23 |
| SETs with empty `[PRIMARY RULES]` (`Count=0` or omitted) | 10 |
| SETs with identity-only `[PRIMARY RULES]` (e.g., `ttf01` with 9 rows) | 11 |
| SETs with at least one neighbor-changing row | 12 |
| Total `[PRIMARY RULE*]` rows across corpus | 699 |
| Identity rows (`(Adjacent, AdjacentHeight) == (Changed, ChangedHeight)`) | 438 |
| Changing rows (`(Adjacent, AdjacentHeight) != (Changed, ChangedHeight)`) | 261 |
| Height-changing rows (`AdjacentHeight != ChangedHeight`) | 96 |
| Largest primary-rule table (`tno01`) | 192 rows |
| Duplicate `(Placed, PlacedHeight, Adjacent, AdjacentHeight)` keys | 0 |
| Encoded height values outside `{0, 1}` | 0 (1,491 zeros, 606 ones) |

## 2. Corpus-Derived Semantics Contract (Aurora Parity Unverified)

> [!IMPORTANT]
> No live Aurora Toolset (`nwtoolset.exe`) instance or external Aurora-saved `.are` before/after capture was available in this Linux environment. The rules below were derived from structural constraints in the 33 stock `.set` files (where pair-coexistence checks prove which corners cannot coexist on any tile without primary-rule rewriting, and where `(0, 0)` vs. `(1, 1)` rows in `tno01` reveal relative height encoding). Every unverified Aurora behavioral detail is explicitly labeled `ASSUMPTION` below.

1. **Lattice Adjacency (`Placed` vs. `Adjacent`):**
   - `ASSUMPTION: 8-connected corner adjacency on the (width + 1) x (height + 1) lattice matches Aurora's Placed/Adjacent neighbor relation — affects whether diagonal corner-sharing cells are updated during a single-cell paint.`
   - Each painted or erased cell updates its 4 corners `(x, y + 1)`, `(x + 1, y + 1)`, `(x, y)`, `(x + 1, y)` on the `(width + 1) x (height + 1)` corner lattice and seeds them in gesture order.
   - Each raised or lowered corner updates its height and seeds that corner index in gesture order.
   - `apply_primary_rules` propagates from each seeded source corner to its 8-connected neighbor corners (`dx in {-1, 0, 1}`, `dy in {-1, 0, 1}`, excluding `(0, 0)`), which are the corners sharing a cell with the source corner.
   - All 4 tile orientations (`0..3`) and area border edges are handled uniformly by the existing `fit_area_tile_edits` stage after rule propagation completes.

2. **Relative Height Normalization:**
   - `ASSUMPTION: PlacedHeight, AdjacentHeight, and ChangedHeight normalize relative to base_height = min(src_height, adj_height) — affects rule matching at non-zero elevations and step boundaries.`
   - Given a source corner `(src_terrain, src_height)` and an 8-connected neighbor `(adj_terrain, adj_height)`, relative heights normalize against `base_height = std::min(src_height, adj_height)`:
     - `rel_placed = src_height - base_height`
     - `rel_adjacent = adj_height - base_height`
   - When `rel_placed <= 1 && rel_adjacent <= 1`, the table is scanned for `(src_terrain, rel_placed, adj_terrain, rel_adjacent)`.
   - On a match, the neighbor becomes `new_terrain = rule.changed` and `new_height = base_height + rule.changed_height`.
   - This normalization is translation-invariant across negative, zero, and positive area base elevations (verified in unit tests at elevations `-2`, `0`, and `+5`).

3. **No-Match, Identity Rows, and Duplicate Keys:**
   - `ASSUMPTION: a missing primary-rule match leaves the neighbor corner unchanged and defers rejection to tile fitting — affects sets with partial primary-rule tables.`
   - When no rule matches `(src_terrain, rel_placed, adj_terrain, rel_adjacent)`, the neighbor corner is left unchanged; tile fitting then determines whether a valid tile exists for the resulting corner combination.
   - Explicit identity rows (`rule.changed == adj_terrain && base_height + rule.changed_height == adj_height`) produce no state change and do not re-enqueue the neighbor.
   - Duplicate `(Placed, PlacedHeight, Adjacent, AdjacentHeight)` keys, negative or `> 65536` counts, missing fields, unknown terrain names, or encoded heights outside `[0, 1]` mark `Tileset::primary_rules_valid = false` and clear `primary_rules` while keeping `Tileset` rendering intact. Any brush operation that uses primary rules rejects atomically when `primary_rules_valid == false`.

4. **Propagation, Gesture Order, and Termination Bounds:**
   - `ASSUMPTION: changed neighbors re-enqueue into a FIFO worklist seeded in gesture order — affects multi-step cascades in tables like tno01 and competing-source tie-breaking.`
   - Seeds are pushed into a vector-backed FIFO queue in exact gesture order (`ordered_tile_indices` / `ordered_corner_indices`) with a `pending` byte mask per corner.
   - Neighbor corners are enqueued only when `(new_terrain, new_height) != (adj_terrain, adj_height)` and not already pending.
   - Total rule-neighbor evaluations are bounded by `max(256, 16 * corner_count)`. Cyclic synthetic rule tables exceed this bound and reject atomically (`ObjectEditStatus::invalid_batch`) without mutating live area tiles, doors, or undo history.

5. **Brush Operations, Protected Groups, Door Hooks, and Eraser Preview:**
   - `ASSUMPTION: terrain, raise/lower, eraser, and group placement share the same primary-rule stage while preserving fixed group footprints — intentional editor safety policy.`
   - Terrain, raise/lower, eraser, and group placement all run `apply_primary_rules` before `fit_area_tile_edits`.
   - Crosser-only, variation, light-only, and structural void edits do not run terrain primary rules.
   - During group placement (`overlay_group`), corners belonging to the incoming fixed group cells (`fixed[tile_index].id >= 0`) act as `Placed` sources for surrounding non-group terrain but are skipped as `Adjacent` targets so the group's own authored topology is never overwritten.
   - Any rule that would alter a corner touching an unselected existing placed group on the map rejects atomically with `"The brush would split a placed tileset group"`.
   - Occupied door hooks on neighbor tiles changed by primary rules are validated by `validate_area_tile_edits`; neighbor adjustments never implicitly authorize deleting doors outside the explicit erase targets.
   - `preview_area_tile_stroke` merges explicit erase target cells (retaining their original tile IDs for removal indication) with any additional neighbor cells changed by primary-rule propagation (`removal_preview.tiles.rows`).

## 3. Memory Layout and Cost Measurements

- **Machine & Build Configuration:**
  - CPU: AMD Ryzen AI 9 HX 370 w/ Radeon 890M (12 cores / 24 threads, 1024 KB L2 cache per core)
  - OS: Linux x86_64
  - Build: Release (`-O3 -DNDEBUG`)
- **Static rule storage:** `sizeof(nw::TilesetPrimaryRule) == 24` bytes (`static_assert` verified in `lib/nw/formats/Tileset.hpp`).
  - Largest stock table (`tno01`, `R = 192`): `4,608` bytes.
  - Typical interior/exterior table (`tin01` `R = 25`, `ttr01` `R = 28`): `600`–`672` bytes.
- **Transient builder scratch for `apply_primary_rules`:**
  - `seeded` (`C` bytes) + `seeds` (`4 * C` bytes) + `pending` (`C` bytes) + `queue` (`4 * C` bytes) = `10 * C` bytes, where `C = (width + 1) * (height + 1)`.
  - Combined builder scratch (topology decode + affected/fit/fixed buffers + primary-rule worklist):
    - `4x4` area (`N = 16, C = 25`): `934` bytes total (`250` bytes rule scratch).
    - `8x8` area (`N = 64, C = 81`): `3,254` bytes total (`810` bytes rule scratch).
    - `32x32` area (`N = 1024, C = 1089`): `46,750` bytes total (`10,890` bytes rule scratch).
- **Preview rebuild frequency:**
  - Preview rebuilds run only when the hovered/dragged target cell or corner changes (`cursor_target_index` or `append_area_tile_grid_line` advances); stationary pointer movement does not rebuild candidate edits.

### Build-Stage Latency Matrix (200 iterations per workload, `8x8` area `N = 64, C = 81`)

| Workload | Tileset | Category | N | R | T | A | W | Scratch (B) | Before Median / p95 (us) | After Median / p95 (us) |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Single click (1 cell) | `ttf01` | Previously valid (identity rules) | 64 | 9 | 168 | 9 | 32 | 3,254 | 11.19 / 11.32 | 11.46 / 11.78 |
| Drag stroke (4 cells) | `ttf01` | Previously valid (identity rules) | 64 | 9 | 168 | 18 | 80 | 3,254 | 23.61 / 35.39 | 24.11 / 24.79 |
| Near-full batch (56 cells) | `ttf01` | Previously valid (identity rules) | 64 | 9 | 168 | 64 | 520 | 3,254 | 93.90 / 100.83 | 96.94 / 109.34 |
| Single click (1 cell) | `tin01` | Newly enabled (room wall join) | 64 | 25 | 299 | 25 | 128 | 3,254 | rejected (N/A) | 51.38 / 54.96 |
| Drag stroke (4 cells) | `tin01` | Newly enabled (room wall join) | 64 | 25 | 299 | 40 | 224 | 3,254 | rejected (N/A) | 71.97 / 76.36 |
| Near-full batch (36 cells) | `tin01` | Newly enabled (room wall join) | 64 | 25 | 299 | 64 | 608 | 3,254 | rejected (N/A) | 131.42 / 138.71 |
| Raise single corner (1 corner) | `ttr01` | Previously valid (Grass slope) | 64 | 28 | 283 | 4 | 8 | 3,254 | 8.97 / 9.08 | 9.08 / 9.35 |
| Raise drag (4 corners) | `ttr01` | Previously valid (Grass slope) | 64 | 28 | 283 | 10 | 32 | 3,254 | 22.22 / 22.58 | 22.94 / 23.69 |
| Raise plateau (25 corners) | `ttr01` | Previously valid (Grass slope) | 64 | 28 | 283 | 36 | 200 | 3,254 | 87.79 / 108.64 | 88.89 / 93.73 |
| Single click (1 cell) | `tno01` | Previously valid (192-rule table) | 64 | 192 | 1,287 | 9 | 32 | 3,254 | 62.54 / 66.77 | 65.09 / 77.55 |
| Drag stroke (4 cells) | `tno01` | Previously valid (192-rule table) | 64 | 192 | 1,287 | 18 | 80 | 3,254 | 135.14 / 142.40 | 136.92 / 143.42 |
| Near-full batch (36 cells) | `tno01` | Previously valid (192-rule table) | 64 | 192 | 1,287 | 49 | 392 | 3,254 | 486.37 / 520.36 | 505.79 / 531.56 |

- **Focused Release test suite wall times:**
  - `KernelTilesets.*` (3 suites including stock `ttr01`, `tin01`, `tno01`, `ttf01`, `rules_primary`, `bad_rules`, and 11 malformed variants): `7 ms` total.
  - `ClientAreaTileEdits.*` (46 suites): `12 ms` total.
  - `ClientAreaTileEditor.*` (7 suites): `< 1 ms` total.
