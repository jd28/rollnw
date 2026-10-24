# RP-12: Frame-slot depth targets

Status: open. [Parent](rendering-performance-overhaul.md). Follows RP-11.

RP-11's count experiment saves 133.5 MiB; corrected output matches within repeat
variation, but outdoor timings regress and max is mixed. Default targets own one depth
buffer per swapchain image, although only two renderer frames are in flight.
Their color wrappers resolve the actual image through current_image_index.

- [ ] Measure two frame-slot default targets using the existing fence/lifetime
  path; remove the redundant per-image depth-view table.
- [ ] Preserve per-image color views/layouts/presentation semaphores and all
  custom/headless target behavior; verify resize, load/clear and destruction.
- [ ] Compare memory, actual presented CPU/GPU/tails and images. No saving or
  speedup claimed for this candidate before measuring it.

Cost: small backend ownership/index change and hardware verification; no new API.
Done: measured memory improvement with corrected-frame parity/timing, or rejection.
