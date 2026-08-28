# Native and baked area navigation input

Status: runtime-neutral in-memory input exists; persistent native serialization
is intentionally not implemented.

Existing NWN areas place tile WOKs and object PWK/DWK state meshes. Those files
contain authored floor height/material and collision state that render MDLs do
not reliably preserve, so importing existing NWN content requires decoding
them once. They are not the shared navigation model.

The implemented boundary is:

`NWN WOK/PWK/DWK adapter -> flat area arrays -> tiled Recast/Detour snapshot`

`NavAreaBuildSource` contains only vertices, indices, surface rows,
walkability rows, dense obstacle ownership/state, door-link rows, and area tile
dimensions. Recast, queries, movement, and tile rebuilds do not name or parse
WOK, PWK, DWK, MDL, a toolset object type, or renderer state. A native map
producer can populate that same flat protocol directly and bypass all three
NWN formats.

A serialized native/baked tile cache was not added because the repository has
no observed versioning, source-hash, radius-class cache-key, endianness, or
stale-cache invalidation contract. Inventing those now would add a file format
and compatibility burden without an input asset that consumes it. When a
native map format exists, measure its cold-build frequency first; add a baked
Detour cache only if synchronous generation is a demonstrated load-time cost.
Invalid or stale native rows must reject the complete snapshot, never fall back
to guessed render collision.
