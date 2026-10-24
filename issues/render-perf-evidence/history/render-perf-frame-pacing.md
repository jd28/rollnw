# RP-08: Outdoor frame tails

Status: **historical record**, archived 2026-10-23. This file preserves the
original results, limits and plans; its old status labels and checklists do not
define current work. See the [current renderer status and backlog](../../rendering-performance-overhaul.md).

## Recorded investigation

Status: open, partially attributed. [Parent](../../rendering-performance-overhaul.md).

## 2026-10-22 one-pending presentation wait — rejected

Waiting via `vkWaitForPresent2KHR` for the previous FIFO presentation before the
next input/frame-clock sample removes the long backlog but fails cadence. All
twelve scheduled starts finish with 14,400 valid calibrated presentation records,
7,200 exact logical pairs and no retries. Sixteen preflight images and 2,400
logical pairs match exactly; validation, wait ordering and epoch/ID checks pass.
Both modes request and receive 16 images. Only 16/48 screen guards pass.

Median-of-three presented rates fall gate 62.07 → 53.97 fps and city 62.15 →
56.78 fps; individual candidate runs range 37.69–58.27 fps. Display-interval p95
rises 16.669 → 25.001 ms/gate and 16.668 → 25.000 ms/city. Frame-to-presentation
p95 also rises 20.499 → 25.120 and 23.521 → 25.161 ms. Maximum calibration
uncertainty is 0.000423 ms. CPU/frame includes the wait call and rises in five
of six pairs beyond the +5% guard. Reduced GPU time/second comes with fewer
displayed frames; it is not accepted as a gain. Memory counters remain separate.

Production sources and all 192 runtime files remain byte-exact. Evidence is in
`build/render-present-wait-2026-10-22/`, including complete per-run results,
costs and rejected source. No SDK, production API or pacing change is retained.
One outstanding presentation is too restrictive on this compositor. Further
pacing work needs an explicit rendering-ahead allowance and latency/cadence
verification together; this screen selects no replacement policy. Native input,
normal-clock broader workloads and historical rare stalls remain unverified.

## 2026-10-22 simple FIFO screen — rejected

The first complete gate pair improves first-pixel interval p95 from 16.669 to
8.335 ms, but median frame-start-to-presentation delay rises 16.895 → 146.316 ms
(p95 20.853 → 148.778 ms). All four latency guards fail. Steady process CPU cost
rises 0.142 → 0.216 CPU-seconds/second and outer GPU timers 136.473 → 214.465
ms/second. A higher displayed rate alone does not justify this policy.

Sixteen correctness images and 2,400 logical pairs match exactly; the timing
pair adds 1,200 exact logical pairs. Clock calibration uncertainty is below
0.000334 ms. These are presentation-engine first-pixel-out measurements, not
physical photon or native-input latency. The third start, city FIFO, completes
all 1,200 rows but returns one unavailable pixel timestamp at frame 1,135.
Strict verification stops the twelve-start schedule after three starts; no
retry or complete-cohort claim. All raw results and the failure are preserved in
`build/render-display-paced-2026-10-22/`. Six source files and all 192 production
runtime files remain exactly restored. No production pacing change is retained.

The existing [RP-11 image-count investigation](render-perf-swapchain-images.md)
already measured 16 returned Mailbox images and a 133.5 MiB depth-memory saving
at four images, with timing regressions that prevented retention. The new result
here is calibrated FIFO latency, not discovery of the image-count issue.

Concrete follow-up: `create_swapchain` uses the 16-element array capacity as its
requested image count, bounded only by the surface maximum. The observed Wayland
surface advertises FIFO minimum 3 and Mailbox minimum 4, with no maximum. This
probe did not record returned image count. Verify requested/returned count and
bound outstanding presentations before another FIFO trial; measure acquire
blocking and allocation cost as well as latency. The long queue explanation is
a hypothesis supported by the request and delay, not yet isolated causally.

## 2026-10-22 wait and presentation attribution

Eight fixed instrumented starts complete with 9,600 frames and complete
presentation records. Six correctness images and 2,400 logical pairs match
exactly. The actual client uses Mailbox. All paced frames return first-pixel-out
timing: gate/city p95 is 25 ms despite 2.237/2.829 ms median host work.
Of 1,199 displayed gaps each, gate/city/outdoor/orbit32 have 151/81/60/32 gaps
near 25 ms; the rest are near one or two 120 Hz refresh periods.

Ordinary paced CPU misses are dominated by the limiter's integer-millisecond
rounding. On sleeping misses its median contribution is 0.635–0.674 ms, versus
0.058–0.073 ms of extra sleep. Paced fence/acquire/submit/present p95 values are
each below 0.1 ms. This explains ordinary interval overruns in these observations;
it does not accept the previously rejected nanosecond-sleep change.

Uncapped gate/city/orbit32 reported pixel gaps have p95 8.335 ms. Outdoor uncapped
has no nonzero pixel timestamps and remains unverified. All complete records,
including zero values, are preserved. The presentation clock is opaque and
stage-local: no CPU-to-display latency is inferred. Refresh duration is 8,333,333 ns,
but refresh interval is zero, so this API does not report VRR state. It does not
supersede the separate KWin/config evidence below. No historical 51–137 ms host
stall recurred, leaving rare-stall attribution open.

Evidence: `build/render-client-waits-2026-10-22/`. Sampling overhead is included;
these are diagnostic observations, not a candidate performance comparison. Six
source hooks and all 192 production runtime files are restored exactly. Next
bounded comparison: display-paced FIFO versus Mailbox + application sleep, with
CPU/GPU work per second and calibrated submission-to-pixel delay checked for the
cost of higher cadence and possible queue latency. Preserve uncapped throughput
control; no production pacing change has been retained.

## Display synchronization check after the pacing trial

Read-only KScreen and KWin queries report one 3840x2160 output at 120 Hz,
with VRR/Adaptive Sync set to Never. The saved output configuration has the same
policy and a modification time before the completed pacing cohort. This supports
FreeSync being disabled, but it is not a per-frame historical VRR observation.
Raw KWin output, source/evidence hashes and the existing-cohort phase summary are
saved under `build/render-display-sync-2026-10-21/`.

The application prefers Vulkan Mailbox and falls back to FIFO. The uncapped
switch removes only the application sleep; it does not select Immediate mode or
remove compositor/display synchronization. The cohort's interval is measured
between application frame starts, not monitor presentations. Counts above
16.667 ms are application-budget exceedances, not observed missed display
refreshes. No claim about improved visible smoothness follows from those counts.

In the completed pacing cohort, medians of eight per-run begin-frame p95 values
are 0.077-0.080 ms before and 0.077-0.078 ms after; end/submit/present p95 values
are 0.101-0.106 ms before and 0.095-0.111 ms after. These short typical phases,
alongside the explicit 16 ms application target, support investigating sleep and
scheduling for the ordinary interval tails. They do not exclude isolated GPU or
compositor waits, nor establish actual display cadence. A claim about displayed
pacing requires presentation feedback, with requested sleep, actual sleep and
fence/acquire/present waits distinguished. No display setting or production code
was changed, and the trial's original rejection remains unchanged.

## 2026-10-21 nanosecond sleep trial — rejected

Changing only the rendered-frame sleep calculation to SDL_GetTicksNS/SDL_DelayNS
retains the 16 ms target and matches all 20 client RGBA captures and 6,000 replay
rows; five existing frame tests pass. Eight balanced pairs per gameplay case and
cadence complete all 96 starts, 115,200 frame/GPU samples and 57,600 directly
matching logical pairs, with no discarded/restarted runs.

Paced interval p95 (median of eight per-run statistics) improves gate
16.843 → 16.233 ms, city 16.824 → 16.211 ms and outdoor 16.797 → 16.361 ms.
Budget misses per 9,600 frames fall 960 → 264, 1000 → 258 and 1380 → 305.
All three declared pacing targets pass. This is application cadence, not physical
scanout or native-input latency; the earlier rare host stalls remain unexplained.

Only 26/42 declared +5% host/interval/GPU/process-CPU upper guards pass. Uncapped
interval-p95 bounds are +6.08%/+8.54%/+9.92%; outdoor uncapped p95 rises
1.442 → 1.542 ms. All process-CPU guards pass, but that does not prove the
whole-frame controls equivalent or establish the cause of their variation.
The fixed cohort therefore rejects the candidate. No timing-based retry.

Original source and all 192 production runtime files are restored exactly.
Candidate source/binaries, every pair, background/memory observations, manifests,
review and terminal process records remain in
`build/render-client-pacing-2026-10-21/`. Pacing and residual-tail attribution stay
open; this trial adds no retained optimization. Further work must distinguish
wall-time/GPU-wait variation from execution cost before promoting another change.

## 2026-10-21 completed full-client comparison

The [corrected-reference cohort](render-perf-gameplay-acceptance.md#2026-10-21-completed-small-view-common-reference-comparison)
finishes all 160 scheduled starts, 192,000 checked frames/outer GPU samples and
96,000 exact logical pairs. All 40 declared host/interval median/p95 regression
guards pass. Production code and runtime remain unchanged throughout measurement.
Evidence is staged under `build/render-client-acceptance-2026-10-21/`.

Current paced gate/city/outdoor interval p95 is 16.853/16.831/16.787 ms: all eight
runs each fail the 16.667 ms budget, despite host-work p95 of 3.296/4.520/2.182 ms.
Budget misses total 990/1037/880 of 9,600 intervals per case. Uncapped interval
p95 is 2.454/3.181/1.611 ms, with zero misses. Values are medians of per-run
statistics. Ordinary paced misses are distinct from the observed work headroom.
All long stalls remain: current host includes 51.267, 53.695 and 137.266 ms
frames, the last with 129.605 ms in begin-frame. Broad wall times rise in some
stalls; scheduler/GPU-wait/application causation is not established. A finer
sleep calculation cannot by itself explain those stalls.

The application computes remaining sleep using integer-millisecond SDL_GetTicks
and SDL_Delay. Fractional-millisecond truncation is a hypothesis to test after
this cohort, alongside scheduler overshoot and iteration overhead.
The vendored SDL_DelayNS uses the same OS sleep backend with a finer duration;
SDL_DelayPrecise additionally spins. Do not introduce busy-wait or pacing changes
without a measured benefit and CPU-cost check. No pacing implementation is changed.

## Earlier attribution

The 2026-10-08 [shadow sampling correction](render-perf-draw-submission.md)
changes the rendered output and increases player-view GPU medians in its matched
fixed-view comparison. Use the corrected shader for future common-baseline
comparisons; the older measurements below do not establish final acceptance.

Storage descriptor reuse reduces outdoor host median 0.987 -> 0.943 ms, but
p95 rises 1.410 -> 1.624 ms in the longer matched comparison (all five pairs).
Begin-frame median rises 0.003 -> 0.057 ms; render median falls 0.901 -> 0.806 ms.
GPU scene median is 0.910 -> 0.908 ms. Evidence (external artifact: `render-perf-evidence/storage-descriptors.json`).

[Rigid animated batching](render-perf-rigid-instances.md) also has inconsistent
tails across two sessions: max p95 8.286 -> 7.552 ms, then 7.421 -> 7.645 ms.
Opaque CPU/GPU medians improve in both; whole-frame acceptance remains open.

[Rigid shadow batches](render-perf-rigid-shadows.md), final session: max host
median 5.567 -> 5.397 ms, p95 6.884 -> 6.905; outdoor median 0.909 -> 0.918,
p95 1.338 -> 1.371. Outdoor local shadow CPU 0.089 -> 0.092 ms; no draw savings.
Earlier shadow sessions show larger outdoor p95 regressions; all retained in
the evidence. Untouched tick/pre-render phases also vary; cause is unproven.

- [x] Measure frame-fence waits, thread CPU, switches and CPU/frequency samples.
- [ ] Attribute residual CPU tails and paced GPU variation; fixed affinity is
  not a general fix. Full scheduler/power/cache causation remains unresolved.
- [x] Check fixed-camera presented scenes, both uncapped and 60 Hz.
- [x] Record moving-camera/actor baselines; matched gameplay attribution remains open.
- [ ] Fix a gameplay regression, or document a measured harness/platform effect;
  reconsider descriptor reuse if the tradeoff is unfavorable in gameplay.

Cost: diagnostic runs; no pacing policy change without evidence. Done: explained
and verified tail behavior. Required before RP-06/parent closure.

Current-source [diagnostic](../README.md), baseline
`cc4cdaae0`: three alternating pairs per scene/cadence, forced re-encoding vs reuse.
At 60 Hz, host work median/p95: outdoor 1.847/2.693 -> 1.716/2.493 ms;
max 5.738/7.529 -> 5.252/6.639. Sleep excluded; headless, not presented gameplay.
Outdoor GPU median rises 1.654 -> 1.754 ms; per-pair tails remain mixed.

Uncapped outdoor CPU falls 0.991 -> 0.858 ms while fence median rises
0.0005 -> 0.0316 ms. The added begin-frame time is inside the frame-slot wait.
Max tails are mostly CPU execution. Sampled lower-frequency CPUs correlate with
slow frames; fixing only the main thread to CPU 3 improves outdoor p95 in all
three pairs (1.571 -> 1.162 ms), but max p95 worsens (6.474 -> 6.562).
No affinity/power policy or production instrumentation retained. Keep descriptor
reuse; residual CPU tails and full gameplay scope still prevent closure.

[RP-10 candidate](render-perf-constant-poses.md) adds an unresolved concurrent-load
series: outdoor host 1.824 -> 2.155 ms, p95 3.895 -> 4.514 ms. Preparation improves
consistently in max/city; controlled attribution must distinguish this candidate's
costs from scheduling/load before claiming complete-frame acceptance.

[Same-process pose attribution](../README.md)
holds layout fixed: sampler work drops in all 120 paired blocks, yet uncapped
outdoor tails still vary. Slow frames expose GPU fence waits and CPU/preemption
costs; paced work differs from uncapped work. Production repeat outdoor p95
1.035 -> 1.620 ms. Presented gameplay and full tail causation remain unverified.

[Presented control](../README.md): Wayland/mailbox,
1080p, three starts per scene/cadence. Max frame median/p95 improves in all six
comparisons; outdoor tails worsen in 2/3 uncapped and 1/3 paced comparisons.
Outdoor tail cohorts mainly contain CPU work/fence waits; acquire/present calls
are small except isolated paced outliers. This locates the costs without fully
explaining their variation. Pose reuse remains; full gameplay/tail acceptance open.

[Actual moving gate](../README.md): host
1.886/3.038 ms median/p95; opening frames 5.117–6.956 ms include 2.438–3.001 ms
rebuilding two navigation tiles. [RP-15](render-perf-navigation-transitions.md)
tracks this measured cost. [Interior city/outdoor movement](../README.md)
measures 2.713/3.749 and 0.921/1.641 ms median/p95; these baselines do not resolve
the matched tail regressions or their cause.

2026-10-11 [shared particle definitions](render-perf-particle-definitions.md): the
initial small-view p95 rise 1.828 → 2.278 ms does not repeat in a prespecified,
separate six-pair/1,800-frame follow-up (2.049 → 2.018 ms; only 2/6 pairs worse).
Original results remain retained. Same-process shared/private-definition blocks
hold layout/state fixed: host tails are mixed, while thread-CPU p95 improves with
sharing in all four starts. Typical begin-frame wall is 0.550–0.593 ms versus
0.004–0.005 ms CPU; slow cohorts also roughly double update/render CPU time.
All 53,280 new frame counts/commands and four diagnostic captures/state inventories
match. No runtime fix or diagnostic is retained. This narrows the sharing concern
without resolving frequency/cache/scheduler causes or presented gameplay acceptance.
