# RP-01: Workloads and budgets

Status: open. [Parent/current results](rendering-performance-overhaul.md).
[Historical protocols, hashes and commands](render-perf-evidence/README.md).

Required: full-area toolset overview and normal elevated gameplay, measured
separately. User considers 30 fps reasonable for overview; earlier 4/2 ms
aspirations are not mandatory gates. Complete gameplay budget and supported
view/content limits remain to be settled; no new limit has been implemented.

Fixtures: 32×32 city (1,680 models/600 particle systems), original 16×16 city
control, and 32×16 outdoor (578/16). F9 adds the configured actor. Preserve visual
features and authored populations; do not invent a crowd or full-game result.
Legacy roof/emitter cameras and uncleared-color runs are historical comparisons,
not current player/overview acceptance. Known missing models stay explicit.

- [x] Fixture recipe and separate overview/orbit, gate and interior/outdoor routes.
- [ ] Representative actor activity, door closing and native-input/pacing checks.
- [ ] Common-baseline CPU/GPU/tails, total memory/uploads and asset fingerprints.
- [ ] Final scoped budgets and acceptance in RP-06; retain detailed evidence outside Git.

Maintained harnesses: `tools/mudl/prepare_max_city.py`,
`tools/mudl/benchmark_area_views.py`, `tools/mudl/compare_render_workloads.py`.
Cost: fixture/verification upkeep, no shipping feature required.

Scaling investigation: isolated 16/32/64/96/128-square repetitions of the actual
city; separate fixed local view, coverage, content and resolution controls.
These synthetic repetitions are not supported-area declarations. Record active
models/particles, submitted geometry and pixels alongside tiles. Require complete
submissions: retained chunks and zero existing failure counters did not detect
[uniform exhaustion](render-perf-uniform-descriptors.md). No camera limit selected.
