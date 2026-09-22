# Area map regeneration boundaries

## Current contract

Area maps are derived project data written to
`.rollnw/cache/area_maps/<area-resref>.png`.

- JSON module import copies each live area's flat map inputs (`resref`, tileset,
  dimensions, and tile rows), releases the larger object graph, and composes all
  maps as one batch.
- Saving a live area composes the same data path with a batch of one and replaces
  that area's map. The successful save result carries the replaced path to the
  UI thread, which releases that exact RmlUi texture source so the next Home
  render reloads the new PNG bytes.
- Invalid dimensions and inconsistent tile counts drop only the affected
  derived map. Missing `ImageMap2D` entries and missing textures write an
  opaque checkerboard for the affected tiles and mark that map as degraded.
  Neither case discards a valid CAF file or fails an otherwise valid module
  import.

The observed The Awakening corpus contains 604 areas. The most common dimension
is 8x8; the observed maximum is 32x20 (512 tiles). Eight areas reference the
same unavailable map texture; those maps retain the remaining tiles and expose
the missing input as checkerboards. The current output uses 32 pixels per tile
until either output axis would exceed 2048 pixels, then reduces
the integral pixels-per-tile value so the whole area remains visible. An area
with an axis greater than 2048 tiles is rejected rather than cropped.

On the 604-area corpus, complete JSON imports including map generation took
5.700 and 5.852 seconds of wall time and wrote 52 MiB of compressed PNG data.
The pre-map client binary imported the same module in 1.798 seconds on the same
machine, putting the observed added batch cost at 3.9--4.1 seconds. The Home
browser materializes only its visible fixed-height card rows, so it does not
submit all 604 PNGs to RmlUi at once. Decoded-image memory while scrolling has
not yet been measured.

## Creation integration

Native area creation now copies one flat `AreaMapSource` from each newly
instantiated Area while preparing the CAF batch. Publication creates every CAF,
refreshes the resource registry once, and then passes the contiguous source
batch to `write_project_area_maps` before the command refreshes the area catalog
and opens the new Area tab.

The CAF remains the authoritative output. A failed or degraded derived map
returns a successful command with a concrete warning; it does not remove the CAF
or hide the new area. A malformed prepared source batch rejects publication
before any file write.

Direct external edits to CAF files do not currently regenerate the cache. Do not
add a file watcher until external concurrent editing is an actual supported
workflow; an explicit project refresh command is the smaller fallback if that
need appears first.

## Completion evidence

- A creation regression writes a 3x2 CAF and loads its derived 96x64 PNG before
  checking the new catalog row.
- A blocked map-cache directory produces one failed map result while preserving
  the published CAF and resource entry. The command remains successful, opens
  the new Area tab, and reports `area map unavailable` on the warning channel.
- Import, save, and creation use the same `write_project_area_maps` batch
  transform; no second composer or singular map path was added.
- A renderer regression loads one area-map source, releases the successfully
  replaced path, and verifies that the same source is loaded again on the next
  render. Failed map writes return no invalidation path.
- `rollnw_test --gtest_filter='ClientAreaCreation*'` passed all 6 tests.
- The focused save, creation, and same-source reload run passed all 15 tests.
- `rollnw_test --gtest_filter='Client*'` passed all 509 tests across 77 suites.
- The Release `rollnw_test` and `rollnw-client` targets built successfully.

The user visually inspected the newly created and live-refreshed Home cards
and reported that they look good.
