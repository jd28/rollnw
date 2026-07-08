# Gfx Frame Loop Acquire/Present Recovery

## Problem

The Vulkan frame loop has no working recovery path for non-success
acquire/present results. One of the two in-flight frame slots can be
permanently deadlocked by a routine `VK_SUBOPTIMAL_KHR` during a resize or
DPI change. Found in the 2026-07 gfx/render audit; verified in source.

## Findings

- `lib/nw/gfx/backends/vulkan/vulkan_context.cpp:2773-2788` (`begin_frame`):
  `vkResetFences` runs before `vkAcquireNextImageKHR`. Any acquire result
  other than `VK_SUCCESS` (including `VK_SUBOPTIMAL_KHR`) returns `nullptr`
  without submitting work that would re-signal the fence, and `frame_index`
  only advances in `end_frame`, which callers skip when `begin_frame` fails.
  The next `begin_frame` on that slot blocks forever in
  `vkWaitForFences(..., UINT64_MAX)`.
- `vulkan_context.cpp:2864-2874` (`end_frame`): the `vkQueuePresentKHR`
  result is discarded. `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR` from
  present are the standard swapchain-recreation signal; nothing reacts to
  them. The only recreation path is an externally driven `resize_context`.
- `end_frame` transitions the swapchain image to `PRESENT_SRC_KHR` only when
  `frame.cmds.rendered` is true, but presents unconditionally when not
  headless. A frame that records no render pass (compute-only frames, e.g.
  the `run_compute_smoke` shape in `tools/mudl/app_runtime.cpp`) presents an
  image in an invalid layout — validation-visible spec violation.
- Result-code checking is inconsistent across the loop: acquire checked but
  mishandled, present unchecked, `vkEndCommandBuffer` unchecked in
  `end_frame` but `VK_CHECK`'d in `upload_texture_pixels`.

## Fix

Make the frame loop one explicit state machine:
acquire -> record -> submit -> present -> recreate-on-stale.

- Reset the in-flight fence only after a successful acquire, immediately
  before the submit that signals it.
- Treat `VK_SUBOPTIMAL_KHR` on acquire as a drawable frame; flag the
  swapchain stale for recreation instead of failing.
- Capture the present result; on `OUT_OF_DATE`/`SUBOPTIMAL`, mark the
  context for swapchain recreation before the next `begin_frame`.
- Transition to `PRESENT_SRC_KHR` unconditionally before present, sourced
  from the tracked `swapchain_layouts[]` (valid even from `UNDEFINED`).

## Open Questions

- Should recreation happen inside `begin_frame` (self-healing) or remain the
  caller's job via a queryable stale flag? The current callers already
  handle `begin_frame == nullptr`, so a self-healing loop is the smaller
  external contract change.
