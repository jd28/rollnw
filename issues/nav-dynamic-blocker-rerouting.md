# Dynamic obstacle and route invalidation

Status: implemented for the tiled Recast preview path.

PWK/DWK state is a flat active-byte array indexed by dense snapshot-local
state. A state-to-tile CSR feeds `rebuild_nav_tiles`, which sorts and
deduplicates one change batch, builds all replacement payloads before mutation,
replaces each affected tile once, and rolls back on a failed Detour install.
The output is a caller-owned sorted unique tile-key array.

Routes store their ordered generated-polygon corridor plus a sorted unique tile
sidecar. `invalidate_nav_routes` first rejects the common no-intersection case
with the sidecar, then scans only the remaining ordered corridor. Changes to
already-traversed tiles do not invalidate the route. The transform allocates no
memory and rejects malformed ranges or unsorted change sets.

Closed doors bake their closed DWK footprint and enable a tagged off-mesh
connection. Locomotion stops at its approach corner, applies the three-state
door batch, rebuilds affected tiles, and re-paths to the stored destination.
Opening and closing failures clear the pending route/use action. A synthetic
end-to-end test covers closed-link planning, approach stop, open rebuild, and
continuation.

The preview currently has one actor and submits one door transition as a
one-row batch. Eventual game mode must collect all transitions for a tick
before calling the same batch transform; no parallel singular mutation path is
needed.
