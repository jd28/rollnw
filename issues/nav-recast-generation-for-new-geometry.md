# Navigation generation for new geometry

The shared tiled Recast builder now consumes only neutral flat surface
vertices/indices/material rows, obstacle triangles/state rows, door links, and
area dimensions. It has no WOK, PWK, DWK, object, renderer, or toolset types.

The current NWN adapter populates that protocol from authored WOK floor data
and PWK/DWK obstacles. A native editor or imported scene can therefore bypass
the legacy formats by producing the same validated arrays, or by loading a
future baked Detour tile asset. That is the supported alternative to making
legacy resource formats part of the runtime contract.

No visual-MDL-to-navigation policy is provided. Render meshes do not encode
authoritative walkability, holes, floor layers, or material policy. Add a new
geometry importer only with representative source data and explicit traversal
rules; non-finite geometry, bad indices, unrepresentable Detour references, or
failed tile creation reject the complete snapshot.
