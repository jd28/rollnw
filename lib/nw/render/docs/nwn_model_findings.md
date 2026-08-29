# NWN Model Findings

This is an evidence ledger for NWN MDL behavior discovered while parsing,
lowering, and rendering real assets. It is not a complete MDL specification.
Entries belong here when they change an implementation contract or prevent a
failed hypothesis from being retried later.

Each finding should name the observed input, the required output, the current
error policy, and the code or test that preserves it. Asset-specific behavior
does not become policy merely because one model exposed it.

## Current Data Path

```text
binary or ASCII MDL
  -> nw::model::Mdl parser representation
  -> import_nwn_model_asset
  -> ModelAsset validation
  -> RenderModel upload
  -> common ModelInstance animation and skin matrices
```

The parser preserves source geometry, node hierarchy, controller data, and
source skin slots. The NWN importer converts that data into the bounded common
renderer contract. Problems should be localized to one boundary before changing
transform math downstream.

## Skin Influence Lanes Are Independent

A binary skin vertex stores four `uint16_t` bone-slot lanes and four matching
floating-point weights. `0xffff` means that one bone lane is unused. It does
not terminate the remaining lanes and it does not request a fallback bone.

The observed sparse row that exposed this was:

```text
bones   = [0xffff, 3, 0xffff, 0xffff]
weights = [0.0,    1.0, 0.0,    0.0]
```

Stopping at lane zero discarded the valid lane-one influence before lowering.
The affected skin meshes then failed import. This presented visually as missing
model parts, including the head and tail, rather than as merely incorrect
deformation.

Current contract:

- The binary parser examines all four lanes.
- `0xffff` becomes `-1` in `SkinVertex::bones` for that lane only.
- A lane with weight less than or equal to zero is ignored during lowering.
- A positive-weight lane with an out-of-range source slot rejects that skin.
- A used source slot whose bone-node row is `-1` is a model-space identity
  influence. The common skin table preserves it as `kModelSkinIdentityJoint`;
  every other negative or out-of-range node rejects the skin.
- Accepted weights are clamped independently to `[0, 1]` when packed. The
  importer does not invent another joint or renormalize the source row.

The parser cost is four fixed lane checks per vertex. The importer performs
another four lane checks per vertex, followed by one linear scan of the 64-slot
source bone table. These are CPU import-time costs, not per-draw costs.

Evidence:

- `Mdl.BinarySkinPreservesSparseBoneLanes` encodes the sparse row directly in a
  synthetic binary MDL.
- `Model.Skin` covers packed ASCII skin rows from `c_satyr` and
  `c_satyrarcher`.
- `RenderModelLoader.ImportsNwnModelAssetSkinMeshes` verifies that valid parsed
  skin meshes survive the `ModelAsset` boundary.
- `c_halaster` exposed a positive-weight source slot 17 mapped to bone node
  `-1`; rejecting it dropped the entire `skin_robe` mesh.
- `RenderModelLoader.ImportsNwnIdentitySkinBoneRows` verifies that the identity
  row and its packed vertex influence survive common lowering and validation.

The ASCII parser currently expects its textual bone-name list to be packed and
stops at the first empty name. That is a separate source representation; the
binary sparse-lane finding is not evidence that ASCII rows have the same shape.

## Inverse Bind Data Is Derived From The Hierarchy

Binary skin nodes contain `qbone_ref_inv` and `tbone_ref_inv` arrays. The wire
fields remain in `MdlBinarySkinNode` so the binary layout is correct, but rollnw
does not copy their payload into `SkinNode` or use it to build renderer skin
matrices.

The required inverse bind transform is derived from the parsed bind hierarchy:

```text
inverse_bind = inverse(bone_world_bind) * mesh_world_bind
```

The `ModelAsset` importer computes one matrix per used joint from the bone and
mesh bind poses. This removes two source arrays from the live model
representation and avoids carrying redundant transform state into runtime
rendering.

Historical evidence is commit `223dbbad8`, which removed
`bone_rotation_inv`/`bone_translation_inv` storage after the hierarchy-derived
path was established. The `qbone_ref_inv` and `tbone_ref_inv` payloads should
not be restored unless a reproducible corpus case demonstrates information that
cannot be reconstructed from the parsed hierarchy.

This conclusion is narrower than saying those fields have no meaning in the
original format. They are unnecessary inputs to rollnw's current transform.

## Skin Lowering Is A Bounded Compaction

`SkinNode::bone_nodes` is a 64-entry source-slot table. The common importer:

1. Scans positive-weight vertex lanes and marks the source slots actually used.
2. Validates each used slot against the source bone table and model node table.
3. Compacts used slots into a `Skin::joints` array.
4. Rewrites each vertex lane through the source-slot-to-joint remap.

The common renderer accepts at most `kModelMaxSkinBones` (currently 64) joints
per skin. Empty skins, out-of-range source slots, unresolved nodes, and skins
over that bound are rejected at import. The caller increments
`skipped_skin_mesh_count`; malformed rows do not reach GPU submission.

This means a missing skinned part should be diagnosed in this order:

1. Did the parser produce the skin node, vertices, indices, lanes, and weights?
2. Did `skipped_skin_mesh_count` increase during NWN-to-`ModelAsset` lowering?
3. Did `ModelAsset` validation accept the skin and primitive indices?
4. Did runtime skin assignment and matrix generation retain the accepted rows?

Changing bind matrices before answering those questions mixes distinct data
failures and can make a parser omission look like an animation bug.

## Hierarchy And Animation Findings

- Skeleton rows are built from parsed source-node indices and parent links.
  Names are for lookup and diagnostics; indices carry runtime identity.
- Bind transforms are accumulated down that hierarchy before inverse binds are
  calculated.
- Animation clips from a model and its supermodel chain are lowered against the
  loaded model's skeleton. A missing track leaves that node at bind/static pose.
- Static primitives use the sampled node transform as their draw world.
  Skinned primitives remain at `root * mesh_world_bind`: their sampled pose is
  already in the skin matrix table, and the primitive inverse-mesh transform
  cancels the bind transform exactly once. Applying the sampled mesh-node world
  as well double-transforms skins whose mesh node is not neutral.
- Zero-duration clips are real source data. When every translation, rotation,
  and scale key is at time zero, the Ozz backend treats the clip as a static
  pose: its internal encoding uses a unit duration and sampling remains fixed
  at time zero. A zero-duration clip containing any nonzero key time is
  malformed and is dropped without disabling valid sibling clips.

## Dangly Mesh Findings And Limits

- Dangly source vertices still need valid rest positions, normals, tangents,
  material sources, and indices at the `ModelAsset` boundary. Invalid basis
  vectors are repaired and counted during import.
- Dangly intent lowers into an explicit `ModelDeformer` row. The common path
  currently renders unsupported secondary-motion deformers as static source
  geometry; it does not run the legacy per-frame CPU vertex rewrite.
- The binary skin `qbone_ref_inv` and `tbone_ref_inv` arrays are not dangly
  simulation state.
- Model or node names are not deformation policy. Remaining NWN name heuristics
  are contained at import and counted by `foliage_name_heuristic_count`.

A previously observed large robe displacement was not reproduced reliably, so
it is not recorded as a format conclusion. If it returns, capture the source
model, transition sequence, deformer counters, and first divergent vertex
before changing the transform.

## Diagnostics

Use the load report before visual trial-and-error:

```bash
mudl report <resref-or-object-json> --module <module-path>
```

Relevant geometry fields include `skin_count`, `skinned_primitive_count`,
`deformer_count`, `skipped_skin_mesh_count`, `normal_repair_count`, and
`tangent_repair_count`. A useful regression records the command, revision,
model source, and these counters, then adds the smallest synthetic parser test
that represents the failing row shape.

## Rejected Debugging Shortcuts

- Do not select behavior from a model, texture, bone, or node name.
- Do not treat an unused influence lane as the end of a fixed-width row.
- Do not substitute bone zero for a missing or invalid influence.
- Do not restore redundant inverse-bind arrays without a failing corpus case.
- Do not repair a parser omission with renderer-side model-specific logic.
- Do not promote an unreproduced visual anomaly into a format rule.

## Updating This Ledger

Add a finding only after the input row or transition can be observed. Include:

- the source kind and representative asset or synthetic row
- the values that matter, including sentinels and valid ranges
- the transform boundary where behavior diverged
- explicit reject, drop, clamp, or fallback behavior
- a regression test or a reproducible `mudl report` command

Open design decisions belong under `issues/`; this document records established
behavior and the evidence that supports it.
