# Offline Model Compiler

## Data

The current live adapters prove the modern runtime data shape in memory:

- glTF imports feed `ModelAsset` / `RenderModel` geometry, PBR material,
  animation, skin, socket, and particle payloads.
- NWN imports feed the same common data where possible, but still keep a
  sidecar for PLT recoloring, legacy node-tree behavior, emitters, dangly
  deformation, humanoid body-part assembly, and cross-skeleton policy.
- Viewer and mudl use runtime switches to compare common paths against legacy
  fallback paths while the contracts are still moving.

That is useful for bridge work, but it is not the final ownership model for a
modern game. The final common case should load a compiled modern asset directly
instead of re-decoding NWN MDL, glTF, MTR, TXI, PLT, particle, and socket
metadata at preview/runtime load time.

## Direction

Build an offline model compiler after the in-memory contracts are stable enough
to freeze a file format.

The compiler should transform source assets into a versioned modern runtime
asset package:

- source-neutral geometry streams, primitive ranges, bounds, and shadow flags
- PBR material records plus explicit fallback/material diagnostics
- texture payloads in renderer-ready orientation and format policy
- skeletons, animations, skins, socket/marker rows, and attachment bindings
- particle emitter payloads in the common particle protocol
- deformer records for legacy dangly intent, mapped to modern runtime deformer
  kinds where possible
- source diagnostics that explain every dropped, clamped, approximated, or
  legacy-only feature

The compiler is allowed to know about source formats. The renderer should not.
Legacy quirks become compiler inputs and diagnostics; renderer submission should
consume only the compiled common protocols plus explicit debug fallback payloads
when parity is required.

## Boundary

This issue does not replace the current live adapters yet. Keep live glTF and
NWN import paths while the in-memory contracts are still changing and while
visual parity is being proven.

Do not use the compiler as a reason to defer correctness fixes in the live path
when those fixes define the target data contract. The live path is still the
fastest way to discover what data the compiler must emit.

## Issues This Can Split Or Close Later

- `remove-legacy-render-paths.md`: once compiled assets feed prepared surfaces
  directly, live source adapters become tooling/parity inputs instead of runtime
  ownership.
- `modern-runtime-sockets.md`: sockets and attachment bindings should become
  compiled asset rows with stable indices, not per-load string lookup.
- Legacy dangly data should compile into explicit modern deformer records or
  diagnostics, matching the bridge policy documented in
  `lib/nw/render/docs/nwn_model_conversion.md`.
- `nwn-modern-pbr-material-calibration.md`: source material calibration belongs
  in compiler material transforms and validation reports.
- `render-asset-cache-lifecycle.md`: compiled packages can make asset lifetime
  and residency policy more explicit, but cache eviction still needs measured
  runtime data.

## Open Questions

- What is the minimum frozen file contract: one monolithic package, separate
  mesh/material/animation/texture blobs, or a package manifest plus external
  payload files?
- Which identifiers are stable across compiler runs: source resource names,
  content hashes, explicit GUIDs, or compiler-assigned table indices?
- Where does PLT recoloring land for a modern game: compiled material variants,
  runtime palette parameters, or legacy-only fallback?
- Which NWN humanoid assembly data is worth compiling forward, and which should
  become authoring-time conversion output only?
- How will compiler diagnostics be compared against live bridge diagnostics so
  missing source behavior is visible rather than silently baked out?
- What measured load-time, memory, or iteration-time requirement justifies
  switching a specific workflow from live import to compiled assets?

## Do Not Carry Forward

- Renderer branches that infer policy from model, texture, or node names.
- Per-frame legacy source decoding or string lookup in runtime draw paths.
- Treating NWN sidecar data as the production runtime model format.
- Baking away source failures without a compiler diagnostic.
