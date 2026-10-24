# Storage descriptor reuse

Status: closed, 2026-10-04. Implementation: `f32f02b52`.
[RP-04](../render-perf-evidence/history/render-perf-uniform-descriptors.md); frame-pacing follow-up remains open.

Tier 1: remove repeated storage descriptor encoding in `bind_pipeline_resources`.
Inputs are 0–5 validated buffer spans per bind; outputs remain opaque descriptor
bytes in fresh ring regions. Model draws change uniforms while repeatedly using
palette, lighting and dummy spans. ASSUMPTION: adjacent storage spans usually
match — affects profitability; encode/reuse counters will test it.

Platform: Ryzen AI 9 HX 370 / Radeon 890M, RADV, Release/O3, Tracy off. CPU command
recording is serial; mapped GPU memory need not be host-cached. Keep five indexed
keys (buffer generation, address, effective range) and device-sized descriptor
bytes in host memory per command list. Reset keys on recording; retain capacity.
Reject stale/wrong-device/empty/out-of-bounds spans before lookup. Size zero means
the existing remainder-of-buffer contract. Ring overflow keeps its existing
failure path. No borrowed resource/ring pointers survive a call.

Reuse the existing bind loop, ring, failure policy, stats and compute smoke
shader. Whole-bind reuse cannot cover changing uniforms. No new module or shader
protocol. Simplification: encode only changed storage slots; five linear entries
instead of a general lookup table. Cost: five keys + five device-sized payloads
per command list, comparisons/copies, two counters; unchanged GPU allocations.
Allocation failure follows the existing vector exception policy. Descriptor bytes
remain opaque and device-specific; [Vulkan's descriptor contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDescriptorEXT.html)
continues to require the referenced buffers to outlive GPU access.

Modify Vulkan command state/binding, command stats, viewer deltas/JSON and resource
tests. Verify GPU readback for changing buffers/offsets/ranges, fresh uniforms,
invalid binds, ring exhaustion/recovery, pipeline/frame changes; run viewer tests
and validation captures. Compare five alternating pairs on all three fixtures,
including tails/GPU time and non-timing fields. Keep only a measured benefit;
otherwise discard the cache and record rejection. Broader layouts remain RP-04.

Results (same hardware, five alternating pairs per scene; medians of per-run stats):

| Host frame, ms | 60 warmup / 360 measured | 180 warmup / 1,200 measured |
| --- | ---: | ---: |
| Max city median | 6.761 -> 6.445 | 6.560 -> 6.099 |
| Max city p95 | 9.954 -> 10.158 | 7.962 -> 7.583 |
| City median | 2.122 -> 2.195 | 2.051 -> 1.817 |
| Outdoor median | 1.040 -> 1.031 | 0.987 -> 0.943 |
| Outdoor p95 | 1.852 -> 2.275 | 1.410 -> 1.624 |

The longer run addressed the initial noisy tails/city median increase; both sets
are retained. Max-city host median improves 4/5 short and 5/5 long pairs. Opaque
CPU medians improve in every pair on all three scenes. Longer max-city render
CPU: 4.545 -> 4.138 ms; GPU scene: 3.586 -> 3.559 ms (no GPU-work reduction claimed).
Outdoor p95 increases in all five longer pairs; [RP-08](../render-perf-evidence/history/render-perf-frame-pacing.md)
must resolve the pacing tradeoff before full acceptance. Retain the small cache
for measured CPU/median gains; no claim of universal latency improvement.

Max-city storage encodes: 484, reuses: 48,773/frame. The old 49,257 encodes are
inferred from the unchanged bind loop/counts, not an old counter. Device descriptor
size is 16 bytes; cache costs 224 bytes/command list, two lists, plus 16 bytes per
CommandStats value. GPU allocation/draw/uniform counts remain unchanged. Capacity
is retained after the first storage bind; no per-frame cache allocation.

176 tests pass, zero skips. New readback checks cover all five slots, changed
uniforms/buffers/ranges/offsets, layouts, three recordings, stale/wrong-core/invalid
spans and ring exhaustion/recovery. Both graphics test shader resource managers
are now local: the first run exposed duplicate global registration, leaving the
registry unfrozen and failing a later animation test. Fixed run passes both.

23,400 advancing and 48 frozen frame pairs preserve every old non-timing field;
only new encode/reuse counters are excluded. 32 captures have zero validation
errors/warnings. Outdoor and near images match exactly; six repeats per city
binary confine differences to same-version variability (231/105 pixels), not
exact city parity. Formatting, whitespace and cluster links checked. Full gameplay
budgets remain unverified. Evidence (external artifact: `../render-perf-evidence/storage-descriptors.json`)
retains both comparisons, hashes, properties and qualitative descriptor call stacks;
the latter lost 56.36% of samples, so their percentages/timings are excluded.

Reproduce with `tools/mudl/compare_render_workloads.py` and baseline
`build/tools/mudl/mudl-before-storage-descriptors` (source `9b5cb1ba5`), five pairs.
Longer comparison uses the same commands with `--warmup 180 --frames 1200`.
Raw runs/scripts: `/tmp/rollnw-storage-descriptors/`.
