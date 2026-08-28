# Navigation radius classes and crowd movement

Play preview resolves the selected creature's finite `appearance.2da`
`PERSPACE`, adds the authored 0.1 m safety margin, and rounds upward to a count
of 0.125 m Recast erosion cells. `NavWorldState` then represents exactly that
already-eroded class. Tiled path, registration, and movement requests reject a
nonzero per-request clearance; locomotion uses `moveAlongSurface` without
runtime wall-distance correction.

Equal erosion-cell counts are the same class. F9 builds only the selected
actor's class. Additional game-mode classes remain lazy because the observed
838 finite rows collapse to 23 classes at the selected cell size, while 14,262
appearance rows have no finite `PERSPACE` and cannot be assigned a fabricated
radius. That unresolved input policy is recorded separately.

DetourCrowd and agent-to-agent avoidance remain deliberately absent. Add them
only after representative multi-agent runtime behavior supplies an actor
count, update rate, and measured collision requirement; retain the existing
flat batch input/output boundary.
