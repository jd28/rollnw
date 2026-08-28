# Navigation obstacle rasterization

Status: resolved; the legacy whole-triangle blocker approximation and its
owner/triangle mutation API have been removed.

The NWN adapter emits flat PWK/DWK obstacle triangles with dense snapshot-local
state indices. Recast rasterizes only active non-walkable obstacle faces into
the affected 10 m tiles and their standard borders. A small blocker therefore
removes cells under its authored footprint instead of disabling an entire WOK
triangle.

Obstacle state changes use a state-to-tile CSR. A batch sorts and deduplicates
changed states, rebuilds every affected tile once, and commits the new payloads
transactionally. Contradictory rows, state indices outside the catalog, tile
build failures, and insufficient output storage reject the batch without
changing the installed state.

The long-range and tile-rebuild performance gates pass on the recorded release
platform. The old builder and flag-mutation path were therefore removed in the
same change; there is one production obstacle policy.
