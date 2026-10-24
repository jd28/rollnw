# RP-09: Propagate required model shader failures

Status: closed. [Parent](../rendering-performance-overhaul.md).

All eight shipped model shaders and 23 supported pipeline slots are required.
Missing passes have no visual fallback. Initialization rejects incomplete sets;
RenderService performs existing cleanup and propagates failure through startup
and reload. Unsupported pipeline combinations remain unsupported.

Removed partial-success branches and warning-only pipeline helpers. Fixed batch
checks run only during startup/reload; no frame-loop work, layout change, retained
state or new API. Backend initialization remains a singleton configuration step.

Verified on Radeon 890M/Vulkan: 189 tests pass, zero skips. For every model shader,
missing and malformed source fails initialization/reload, restoration recovers,
and supported pipeline keys are valid. All 16 CLI failure cases return 1 without
writing a timing report or screenshot. Three healthy area captures report zero
validation errors/warnings; all nine frames preserve prior non-timing fields.
Existing Vulkan PERFORMANCE messages are separate from validation counters.
Driver pipeline-allocation failure was not injected; no speedup claim.

Results (external artifact: `../render-perf-evidence/shader-failures.json`). Raw scripts/logs/initial
plan: `/tmp/rollnw-shader-failures/`; isolated capture binary/shaders:
`build/tools/mudl-shader-failure-check/`. Initial false success was observed in
`/tmp/rollnw-model-constants/smoke.log`.
