# RP-04: Uniforms and descriptors

Status: open. [Parent](rendering-performance-overhaul.md). Depends on
[RP-02](render-perf-submission-profile.md).

Historical max-city: 13.38 MB uniform writes, 19,806 uniform allocations and 9,989
descriptor allocations/frame. `model_renderer.cpp` repeats scene/surface payloads.

Completed step: [reuse unchanged storage descriptors](closed/render-perf-storage-descriptors.md).
Rejected step: [pass/transform split with push constants](closed/render-perf-model-constants.md).
Uniform writes fell 13.38 -> 1.61 MB, but host median rose 6.29 -> 6.84 ms.
Source restored; prioritize submission cost alongside bytes. Actual payload
distribution is retained with the experiment.

2026-10-08 scaling found a correctness/reporting gap. The fixed 16 MiB uniform
ring returns an empty span on exhaustion; model and particle callers skip
submission before command failure counters see it. Full 96×96 and 128×128 city
overviews consequently report zero failed/dropped draws while omitting particles
and transparent geometry. Their production-buffer timings are invalid for the
complete workload. Vulkan validation also reports no error.

An isolated 64 MiB uniform-ring probe restores the 96×96 workload: 17,140 draws,
10,440 transparent draws, 1,548 particle draws and 270,144 prepared surfaces;
21,502,336 payload bytes before alignment. Three runs give host median/p95
39.998/42.623 ms and GPU median 5.098 ms. The local-view control matches production
pixels exactly. Extra retained memory is 96 MiB across two frame slots; source
and production executable were restored exactly. This is diagnostic evidence,
not a chosen growth policy or optimization.

The probe's 128×128 overview then exceeds the 2 MiB descriptor ring: 2,960,192
bytes required and 9,672 reported failed/dropped draws/frame. Exclude those frames
too. [Raw evidence](render-perf-evidence/README.md) retains both negative cases.

## 2026-10-12 allocation observability

Implemented frame/pass uniform failure, capacity and aligned-demand counters;
`area-benchmark` preserves JSON, marks `complete: false`, and exits 1 on measured
allocation/bind/draw failures. Buffer sizes and allocation success behavior remain
unchanged. Unsupported effective alignments reject; empty/null requests are no-ops;
uint64 demand saturates and may be a lower bound if callers skip further requests.

Actual 96/128 overviews now report 10,884 / 32,192 uniform failures and
24,345,536 / 40,093,632 aligned requested bytes against 16,777,216 capacity.
Both previously exited 0. Three valid views and both negative-case captures match
RGBA and all prior counters exactly; new pass deltas sum to frame totals.
117 tests pass without skips, covering overflow/recovery/alignment/frame reset.

Cost: +24 bytes per CommandStats, scalar accounting per request, acceptance scan
after timing; no additional buffer or cache. Three alternating pairs/view (10,800
frames) give host medians 1.359 → 1.357 / 7.363 → 7.377 / 12.891 → 12.867 ms.
Small-view p95 rises 8.15%; a separate predefined six-pair follow-up does not
reproduce it (1.905 → 1.856 ms, 2/6 pairs worse). Preserve both results; no speedup
or full-client pacing claim. An initial unpinned follow-up is retained separately
and excluded for its setup mismatch. [Raw evidence](render-perf-evidence/README.md).

- [x] Count uniform allocation failures and aligned demand/capacity; reject incomplete
  measured frames in area-benchmark while preserving JSON. Exact-fill, overflow,
  recovery, alignment, saturation and frame-slot reset regression passes.
- [x] Set fixed uniform capacity to 32 MiB for the measured complete 96×96
  workload; preserve explicit rejection beyond capacity (2026-10-14 below).
- [ ] Separate actual frame/pass/material/instance dependencies; remove identical
  copies and binds using existing protocols where possible.
- [ ] If a new GPU layout is justified, define batch layout, ownership, index
  bounds, failure behavior and in-flight lifetime before implementation (Tier 2).
- [ ] Verify palettes, lighting, skinning, reloads and allocation failures.
- [ ] Measure bytes/counts, retained memory, CPU/GPU time and whole frames;
  give RP-05 the resulting binding constraints. Reject unprofitable machinery.

Cost: counters and failure-test upkeep first; possible retained tables, dirty
tracking, shader reads, per-frame buffer memory and protocol changes afterward.
Done: measured improvement with correct lifetimes/visuals, or a recorded rejection.

## 2026-10-14 measured production capacity

Raised the existing per-frame uniform buffer from 16 to 32 MiB. The complete
96×96 standalone overview requests 24,345,536 aligned bytes; the full client,
including UI and overlays, requests 24,393,288. The original production buffer
reproduces 10,884 failed allocations. The new capacity completes the workload
without changing allocation, binding, fence reuse, shaders or camera policy.
Cost: 32 MiB more allocated buffer capacity per graphics context across two frame
slots; resident memory is not assumed to increase by that amount. No dynamic
growth or new allocator state. 128×128 still rejects explicitly: 10,348 uniform
failures and 3,121 descriptor/draw failures in the sampled frame.

109 renderer tests pass without skips, including overflow/recovery/alignment and
slot reuse. Five capture pairs match the complete 64 MiB reference byte-for-byte;
all non-timing sample fields in 18,000 measured frames match except the capacity
field. Three alternating starts/view, 180 warmup + 600 measured frames, main
thread CPU2, 1920×1080. Reference → production host median/p95 (ms):

| View | Median | p95 |
| --- | --- | --- |
| Overview 32 | 2.371 → 2.410 | 3.217 → 3.262 |
| Overview 64 | 10.488 → 10.509 | 12.068 → 13.448 |
| Overview 96 | 24.750 → 24.977 | 27.248 → 28.036 |
| Local 32 | 1.341 → 1.373 | 2.122 → 2.163 |
| Local 96 | 7.685 → 7.516 | 9.505 → 9.031 |

This is a capacity/correctness fix, not a speedup. No persistent >5% median
regression; one 32-overview pair rose 5.30%. The 64-overview p95 increase of 11.43%
is unresolved and retained alongside the existing tail instability; no clean
tail-acceptance claim or additional cherry-picked cohort. Full-client results
are tracked in [RP-06](render-perf-gameplay-acceptance.md), raw evidence
[outside Git](render-perf-evidence/README.md).
