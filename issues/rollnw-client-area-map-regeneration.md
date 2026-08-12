# Area map regeneration boundaries

## Current contract

Area maps are derived project data written to
`.rollnw/cache/area_maps/<area-resref>.png`.

- JSON module import copies each live area's flat map inputs (`resref`, tileset,
  dimensions, and tile rows), releases the larger object graph, and composes all
  maps as one batch.
- Saving a live area composes the same data path with a batch of one and replaces
  that area's map.
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

## Open integration point

The client does not yet have an area-creation workflow. When that workflow is
added, its successful initial CAF save must call `write_project_area_maps` with
the newly created live area before publishing the new area catalog row.

Direct external edits to CAF files do not currently regenerate the cache. Do not
add a file watcher until external concurrent editing is an actual supported
workflow; an explicit project refresh command is the smaller fallback if that
need appears first.

## Done evidence for the future creation workflow

- A newly created area writes its CAF and map before appearing in Home.
- A map failure leaves the area selectable and reports the first concrete map
  error.
- The import and save paths continue to use the same batch transform; no second
  map composer is introduced.
