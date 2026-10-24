# Model pass/transform constants: rejected

Status: closed (rejected). [RP-04](../render-perf-uniform-descriptors.md).
Production source restored to `bd54dfb73`; no push-constant API/cache retained.

Input: two captured max-city frames each contain 9,817 model draws, eight shared
scene payloads and 162 surface payloads. Full-scene adjacent reuse is only 275
rows; shared-scene reuse is 9,809. Distribution (external artifact: `../render-perf-evidence/uniform-distribution.json`).

Trial protocol: pack SceneConstants bytes 0,128) + [256,1200) into a 1,072-byte
shared b1 row; send model/normal matrices as 128 vertex push bytes. Batch upload
owns one CPU comparison row and borrows fenced uniform-ring spans, resetting
on frame changes. Reject bad payload sizes; failed allocations/binds skip draws.
Cost: one cached row/span/frame ID, comparisons and a push command per draw.
Removed full scene copies; added no material cache, third UBO or thread.

Five alternating pairs, 180 warmup / 1,200 measured frames, fixed gameplay camera,
16 ms steps, 1080p, Release/Tracy off, Ryzen HX 370 / Radeon 890M. Separate shader
copies, same empty working directory. Medians of per-run statistics:

| Scene | Host median ms | Host p95 ms | Render CPU median ms |
| --- | --- | --- | --- |
| Max city | 6.290 -> 6.841 | 8.083 -> 8.813 | 4.218 -> 4.673 |
| City | 1.794 -> 2.126 | 2.527 -> 2.806 | 1.528 -> 1.832 |
| Outdoor | 0.912 -> 1.006 | 1.569 -> 1.742 | 0.805 -> 0.912 |

Host median regresses in 5/5 max/city pairs and 4/5 outdoor pairs. Max-city GPU
scene improves 3.491 -> 3.246 ms, but host time fails the keep criterion. Exact
CPU cause is unisolated; this rejects the combined implementation, not all push
constants or shared material tables.

Max-city uniform writes fall 13,384,144 -> 1,612,320 bytes and allocations
19,806 -> 9,997, plus 9,817 push updates / 1,256,576 bytes. Draws and descriptor
allocations remain 9,989. Uniform ring capacity is unchanged.

Verification: 190 tests pass, zero skips; 18,000 advancing + 48 frozen frame
pairs preserve all preexisting non-timing fields except declared uniform counters.
32 captures have no Vulkan warnings/errors. Outdoor and near views match exactly;
city full-view differences fit repeat variation. Max full-view retains 25 pixels
outside observed repeat variation: unresolved, no exact-parity claim. Full PBR,
reload/editor acceptance was not completed for this rejected protocol.

[Measurements (external artifact: `../render-perf-evidence/model-constants-rejected.json`),
reproducible source patch (external artifact: `../render-perf-evidence/model-constants-rejected.patch`).
Raw scripts/reports/initial plan: `/tmp/rollnw-model-constants/`; capture diagnostic:
`/tmp/rollnw-uniform-profile/`. Preserved trial: `build/tools/mudl-model-constants-trial/`.
The initial DXC declaration failure is excluded; its silent success exposed the
[shader failure reporting issue](render-perf-shader-failures.md).

Next experiments must measure command/submission costs alongside byte reductions.
RP-04 and gameplay acceptance remain open.
