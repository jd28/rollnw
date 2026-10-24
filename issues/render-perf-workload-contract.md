# Renderer workloads and budgets

Status: active requirements. [Current status and backlog](rendering-performance-overhaul.md).

Measure normal elevated gameplay and full-area toolset overviews separately.
Basic NWN1 gameplay must reach at least 60 fps (16.667 ms/frame), with substantial
headroom toward the DA:I/Ryse art goal. Toolset overviews target 30 fps
(33.333 ms/frame). Report median, p95, p99 and budget exceedances; a median alone
cannot certify consistent frame rate. Earlier 4/2 ms overview aspirations are
not mandatory gates. No supported scene-size or camera restriction is imposed.

## Inputs and scope

Existing NWN controls include the 32x32 city (1,680 models/600 particle systems),
original 16x16 city and 32x16 outdoor (578/16), with separate overview/orbit and
gameplay routes. The 64/96 repetitions are stress cases, not ordinary gameplay
or supported-area declarations. Preserve features, authored populations and
explicit missing-resource diagnostics. Particle-free model workloads are separate
inputs; do not turn features off in an existing acceptance scene and call that
an equivalent optimization result.

Existing glTF fixtures test the common runtime renderer and source independence.
They do not establish final-art capacity. The next scaling check varies geometry,
material diversity and animation using these inputs, records the actual visible
work and identifies the limiting transform. Keep loading, CPU preparation,
submission, GPU work and presentation measurements separate. Test shadows where
the input exercises them; report absent features explicitly.

Runtime conversion remains supported. Offline asset compilation is deferred.
No new art assets are required to measure current scaling, but representative
meshes/materials/textures/lighting/populations are required before claiming that
the final art target is supported. No exact additional NWN headroom target has
been selected.

## Measurement contract

Use fixed inputs/camera/animation steps, complete draws, available GPU queries,
explicit failure counters and image comparisons. A drop in submitted work is
valid only when the same required output is preserved. Record geometry/material
counts, texture/upload bytes and measured memory with their precise scope.
Offscreen host/GPU durations do not measure presented FPS or display latency.
Report hardware/driver and uncertainty; avoid universal claims from one machine.

Reuse `mudl model-benchmark`, `tools/mudl/prepare_max_city.py`,
`tools/mudl/benchmark_area_views.py` and `tools/mudl/compare_render_workloads.py`.
Fixture upkeep is the cost; no new benchmark framework is required. A bounded
initial screen must justify broader validation. Keep NWN regression controls and
run relevant image/correctness checks before retaining any implementation.

Historical protocols, failed attempts and scope limits are in the
[evidence archive](render-perf-evidence/README.md). Remaining acceptance gaps are
listed once in the [current backlog](rendering-performance-overhaul.md#unresolved-backlog).
