# Metal backend: GPU-driven `cmd_draw_indexed_indirect_count` via ICB

Status: open (deferred follow-up from Phase 1 Metal backend implementation).

## Context

In `lib/nw/gfx/backends/metal/metal_context.mm`:

- `cmd_draw_indexed_indirect` maps natively to Metal's
  `drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:`
  loop because `nw::gfx::IndexedIndirectDrawCommand` is byte-for-byte identical
  to `MTLDrawIndexedPrimitivesIndirectArguments` (20 bytes).
- `cmd_draw_indexed_indirect_count` currently reads the draw count from
  `count_buffer` on the CPU when `count_buffer` is `cpu_visible` (`storageModeShared`),
  and falls back to issuing `max_draw_count` indirect draws when `count_buffer` is
  device-private (`storageModePrivate`, relying on zeroed `instance_count == 0`
  trailing slots being GPU no-ops).

Today no production render pass executes a GPU-written private-count
`cmd_draw_indexed_indirect_count` (the only caller in the repository is the
failed-bind rejection test in `tests/render_gfx_resources.cpp`).

## What remains

When a real GPU cull/compaction pass feeds `cmd_draw_indexed_indirect_count`
with a device-private GPU-written count buffer:

1. Avoid issuing `max_draw_count` empty draw calls or relying on pre-zeroed
   trailing command slots by encoding valid draws `0..min(*count, max_draw_count)`
   into an `MTLIndirectCommandBuffer` from a small compute kernel (or extending
   the cull/compaction shader) and executing it via `executeCommandsInBuffer:withRange:`.
2. Add a backend-agnostic `RenderGfxResources` test that writes both the
   `IndexedIndirectDrawCommand` array and the `uint32_t` count from a compute
   shader into `cpu_visible = false` buffers and verifies pixel output on both
   Vulkan and Metal.
