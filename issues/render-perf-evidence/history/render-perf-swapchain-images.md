# RP-11: Presentation image count

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: open. [Parent](../../rendering-performance-overhaul.md). Depends on RP-01.

Presented 1080p Wayland runs use 16 mailbox swapchain images. `create_swapchain`
requests the fixed array capacity; `create_context` creates a depth target for
every returned image. Renderer submission uses two frames in flight.

- [x] Surface min=3/max=0; request 16 returns 16, requests 3/4 return 4.
  All images are used. At 1080p, depth allocations fall 186,646,656 -> 46,661,664
  bytes (133.5 MiB); live VMA allocation and block bytes confirm the difference.
  WSI color memory/residency are unmeasured. Resize returns baseline allocations.
- [ ] If justified, select a bounded count from the real requirements; handle
  returned counts explicitly and exercise recreation/resizing.
- [ ] Finish CPU/GPU/tail/image comparisons after [RP-13](../../closed/render-perf-benchmark-clear.md).
  43,200 timed rows and clean validation did not establish correct frames:
  image-count changes also changed uncleared color history. No count policy
  shipped. [Retained experiment](../README.md).

[Corrected uncapped comparison](../README.md):
16 -> 4 images saves the measured memory but outdoor host median is .039–.072 ms
slower in all three pairs; max is mixed. GPU scene medians increase in all six
pairs. Corrected images have no stable mode-specific difference. Cadence/resize
and same-process attribution remain before a policy decision.

Cost: diagnostic runs; any change must stay within the existing swapchain path.
Done: measured improvement with parity, or a recorded no-change decision.
