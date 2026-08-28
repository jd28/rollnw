# NWN tile path-node hierarchy

Status: retained and audited at the NWN adapter; unused by local routing.

SET `PathNode` and `Orientation` are authored coarse tile-edge connectivity,
not a replacement for WOK floor geometry or local Recast routing. They remain
flat retained bytes on tileset definitions and do not enter the shared nav
protocol.

The 2026-08-27 dedicated-server corpus contained 33 SET files and 12,303 tile
definitions. It observed no missing path nodes, one multi-byte malformed value,
no one-byte values outside `A-Z`/`a-p`, and no orientation outside a multiple
of 90 degrees. All supported byte values occurred. Orientations were -270 (8),
-180 (5), -90 (174), 0 (7,744), 90 (1,950), 180 (1,430), and 270 (992).
The common bytes were `A` (3,694), `I` (1,651), and `H` (1,121).

The generated-polygon CSR now measures 18.242 us p95 for one 300 m obstructed
request and 0.598 ms for 128 requests, passing the 25 us and 3.2 ms gates.
Enabling the SET hierarchy would add a second topology protocol, rotation and
elevation composition, mismatch fallback, and another correctness surface
without removing measured work required by the current platform. The
simplification decision is therefore to do nothing with these bytes.

Revisit only if a measured area outside the recorded corpus misses the
obstructed long-range gate. Any future use must first decode every observed
byte, compose placed-tile orientation/elevation, validate exits against
generated Recast seams, and fall back on a malformed or mismatched row. It may
never fabricate connectivity.
