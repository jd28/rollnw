# glTF Import Input Hardening

## Problem

`import_gltf.cpp` is the one importer that does not share the codebase's
input-hardening discipline: node-graph traversal is unguarded recursion and
no accessor float is checked for finiteness. The sibling NWN loader does
both. Found in the 2026-07 gfx/render audit; verified in source.

## Findings

- `lib/nw/render/gltf/import_gltf.cpp:161-175` (`import_node_hierarchy`)
  and `:1050-1071` (`walk_node_primitives`): recursion over
  `cgltf_node::children` with no visited-set and no depth cap.
  `cgltf_validate` only cycle-checks the single-valued `node->parent`
  chain; it never detects a node index repeated across `children[]`
  arrays. A small "acyclic" file that repeats child indices at several
  levels causes exponential re-traversal; a long single-child chain causes
  unbounded recursion depth. Both are stack-overflow/DoS vectors on the
  mod/tool import path.
- No `std::isfinite` check exists anywhere in the file — vertex positions,
  joint TRS, inverse-bind matrices, weights all flow into
  `ModelAssetPrimitive::bounds`/`transform` and `Skeleton` unchecked
  (contrast `render/nwn/model_loader.cpp:224,229,659,669,700,784`).
  NaN bounds silently defeat culling; NaN transforms corrupt world
  matrices with no counted failure.

## Fix

- Convert both traversals to an explicit worklist with a
  `std::vector<uint8_t> visited(nodes_count)` guard and a hard depth cap;
  count skipped/rejected nodes in an import stat rather than recursing.
- Validate `isfinite` at the accessor read sites (positions, TRS, IBMs,
  weights); drop the primitive/joint/channel with a counted stat,
  mirroring the NWN loader's policy and the ModelAsset validation stats.

## Verification

Hand-crafted glTF fixtures: repeated-child-index graph, deep chain, NaN/Inf
accessor values — each must import with counted drops, not crash or silent
acceptance. Worth adding to whatever fuzz umbrella covers asset parsers.
