# Modern Runtime Sockets And Attachments

## Data

NWN MDL assets use dummy nodes and named anchors for equipment, VFX, spell
layout, and cross-model placement. The current bridge keeps those rules in the
NWN sidecar through node lookup and transform-anchor policy.

For a modern runtime, the useful data is the socket/locator intent, not the
legacy node tree itself.

## Direction

- Compile authoring dummy nodes into explicit runtime socket records.
- Store sockets by stable index: source/runtime node index, `local_transform`,
  and semantic tag or name table index. A later compiled skeletal asset can map
  that node index to a joint index where that is the runtime representation.
- Resolve equipment and VFX attachments through socket indices, not string
  lookup in update/render paths.
- Keep rig compatibility and cross-skeleton policy as explicit asset/runtime
  data.

## Bridge Implementation

- `nw::render::ModelSocket` is the common socket record. It stores a stable
  source/runtime node index, local transform, bind transform, and name.
- `nw::render::nwn::ModelInstance` builds `sockets_` once after load by scanning
  source nodes and keeping named NWN dummy nodes plus NWN mesh-backed hand anchor
  aliases lowered into socket rows. Lookup by name returns a socket index for
  setup/debug paths; runtime users should carry that index.
- glTF has no strict socket/marker mapping yet. Modern glTF assets should not be
  assumed to use NWN dummy-node or bone names until a separate authoring contract
  defines how nodes, bones, and extras map into `ModelSocket` rows.
- NWN sidecar transform anchors resolve destination/source socket indices in
  `set_transform_anchor`; per-frame root transform evaluation uses those indices
  before falling back to legacy name lookup.
- Particle emitter imports carry common attachment-point indices for emitter and
  target nodes. During the NWN bridge those indices map 1:1 from source-node
  indices; the source-node indices and names stay as fallback payload only.
- Mudl VFX authored-axis preview helpers read common attachment-point transforms
  first, then fall back to imported source-node indices and names.
- Mudl VFX authored-axis and projectile-transport selection match particle
  systems by common owner handle/model index before falling back to sidecar
  pointers for standalone bridge cases.
- Mudl VFX projectile playback steps resolve source anchors into socket indices
  during setup; per-frame projectile root placement uses that index before name
  fallback.
- Mudl VFX target-anchor points resolve target sockets during setup and use the
  cached socket index before name fallback.
- Mudl VFX caster anchor sampling resolves source/caster sockets during setup
  and uses the cached socket index before name fallback.
- Preview particle target updates accept owner `ModelInstanceHandle` plus owner
  model index; mudl VFX playback uses that stable owner key instead of matching
  raw sidecar pointers.
- Preview scene model attachments record child/owner common handles, scene model
  indices, and destination/source socket indices when attached sidecar models are
  added. Common instance sync can use that scene-owned binding for attached root
  transforms before falling back to the legacy sidecar pointer anchor.
- Dynamic creature equipment/body-part/wing/tail setup uses the scene-owned
  attached-model path, passing owner model indices and socket names into setup so
  the scene records common owner handles and socket indices before runtime sync.
- Particle system runtime owns a flat per-emitter attachment binding array
  from imported emitter indices plus the common owner handle/model index. The
  bridge frame sync consumes that array before falling back to import fields.
- The bridge resolves particle attachment bindings into per-frame transform rows
  before emitter sync. NWN source-node lookup stays in that resolver during the
  bridge rather than in the emitter state update loop.
- Compiled particle emitters carry attachment-point indices, bridge fallback
  source indices/names, and default placement data. The preview runtime builds
  owner bindings from compiled particle data instead of the NWN import init side
  array.
- Common `ModelInstance` records carry a dense, source-node-indexed attachment
  transform cache for the bridge. Particle attachment resolution reads that
  common cache first and falls back to NWN sidecar lookup only for compatibility.
- RenderModel-to-RenderModel attached root transforms are evaluated by the
  common batch helper from owner `ModelInstance` transform rows plus owner/child
  `RenderModel` socket indices. The viewer still owns scene binding storage and
  NWN sidecar compatibility fallback during the bridge.
- Preview runtime sync reports attachment binding/resolution/drop counters so
  missing handles, sockets, or transform rows are visible as frame data instead
  of silent per-frame state.
- Mudl frame JSON and screenshot logs expose those runtime sync counters so
  parity captures can identify attachment-root failures without debugger access.
- Remaining bridge fallbacks still keep NWN sidecar pointer/string anchors for
  compatibility until attachment evaluation is fully common-runtime.

## Do Not Carry Forward

- Treating the NWN sidecar node tree as runtime truth for new assets.
- Hot-path `find(anchor)` lookup by string.
- Renderer behavior hidden in legacy body-part or dummy-node naming rules.
- PLT, dangly, emitter, or cross-skeleton quirks as core modern model contracts.
