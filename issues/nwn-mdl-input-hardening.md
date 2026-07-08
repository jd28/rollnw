# NWN MDL Input Hardening (Controllers, Legacy Upload)

## Problem

The recent binary-parser memory-hardening pass has not reached two MDL
consumption paths: animation controller data and the legacy mesh/skin GPU
upload. Both trust counts/indices read from untrusted `.mdl` bytes. Found
in the 2026-07 gfx/render audit; verified in source.

## Findings

- `lib/nw/model/MdlBinaryParser.cpp:~271-288`: `time_index`, `data_index`,
  `rows`, `columns` are copied raw from the file into `ControllerKey` with
  no check against `controller_data.size()`.
- `lib/nw/model/Mdl.cpp:234-247` (`Node::get_controller`): builds spans
  `{&controller_data[c.time_offset], c.rows}` and
  `{&controller_data[c.data_offset], c.rows * c.columns}` from those raw
  offsets — out-of-bounds indexing before any span-size check can help.
  Every consumer trusts the spans
  (`render/nwn/model_loader.cpp:3790-3843`, `nwn/nwn_animation.cpp:240-256`
  index `data[i*4+3]` etc. directly). A corrupt/malicious model is an OOB
  read (crash or heap disclosure) at load or animation time.
  `c.rows * c.columns` is also an unchecked int multiply.
- `render/nwn/model_loader.cpp:3940-4062` (`create_mesh_buffers`,
  `create_skin_buffers`): face indices from the parser are memcpy'd into
  the GPU index buffer with no check against vertex count; the parser
  (`MdlBinaryParser.cpp:399-406`, `521-528`) pushes raw indices too.
  Out-of-range indices become GPU-side OOB vertex fetches at draw time.
  The modern path already validates (`primitive_indices_in_range`,
  `model_asset.cpp:54-63`); this legacy path is still live — e.g. particle
  chunk meshes load through it
  (`render_asset_cache.cpp:546-549` -> `ModelLoader::load`).

## Fix

Same reject/drop-and-count policy the modern path and the TLK hardening
pass already use:

- In `MdlBinaryParser.cpp`, validate
  `time_index >= 0 && time_index + rows <= controller_data.size()` and
  `data_index >= 0 && data_index + size_t(rows) * columns <=
  controller_data.size()` before pushing the key; drop and count malformed
  keys (the file already does this for faces/arrays elsewhere).
- In `create_mesh_buffers`/`create_skin_buffers`, drop faces with any index
  `>= vertex_count` before upload, counted for diagnostics — the same
  policy as `primitive_indices_in_range`.

## Verification

Extend the existing MDL fuzz coverage (see `text-mdl-fuzz-harness.md`) with
binary inputs that mutate controller offsets/rows/columns and face indices;
ASan run over the corpus. Add one regression test per rejected-input class.
