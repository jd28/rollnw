# RP-08: Outdoor frame tails

Status: open, partially attributed. [Parent](rendering-performance-overhaul.md).

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

Current-source [diagnostic](render-perf-evidence/README.md), baseline
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

[Same-process pose attribution](render-perf-evidence/README.md)
holds layout fixed: sampler work drops in all 120 paired blocks, yet uncapped
outdoor tails still vary. Slow frames expose GPU fence waits and CPU/preemption
costs; paced work differs from uncapped work. Production repeat outdoor p95
1.035 -> 1.620 ms. Presented gameplay and full tail causation remain unverified.

[Presented control](render-perf-evidence/README.md): Wayland/mailbox,
1080p, three starts per scene/cadence. Max frame median/p95 improves in all six
comparisons; outdoor tails worsen in 2/3 uncapped and 1/3 paced comparisons.
Outdoor tail cohorts mainly contain CPU work/fence waits; acquire/present calls
are small except isolated paced outliers. This locates the costs without fully
explaining their variation. Pose reuse remains; full gameplay/tail acceptance open.

[Actual moving gate](render-perf-evidence/README.md): host
1.886/3.038 ms median/p95; opening frames 5.117–6.956 ms include 2.438–3.001 ms
rebuilding two navigation tiles. [RP-15](render-perf-navigation-transitions.md)
tracks this measured cost. [Interior city/outdoor movement](render-perf-evidence/README.md)
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
