# RP-15: Navigation startup and door transitions

Status: open. [Parent](rendering-performance-overhaul.md). [Evidence](render-perf-evidence/README.md).

- [x] Fix max-city startup rejection at quantized walkmesh boundaries (`c56017957`);
  regression fails before/passes after; 85 related tests pass.
- [ ] Attribute/avoid repeated work in 1.604–1.656 s preview startup (two nav builds).
  [Interior/outdoor route startup](render-perf-evidence/README.md):
  1.599–1.613 s / 0.591–0.599 s.
- [ ] Reduce measured two-tile door rebuild cost, 2.438–3.001 ms; opening frames
  take 5.117–6.956 ms. Preserve routes, clearance and height correctness.
- [ ] Verify closing/reopening and cold-build controls; current controls are mixed.

Cost: navigation build profiling and bounded changes. Done: matched real
startup/transition evidence and gameplay acceptance.
