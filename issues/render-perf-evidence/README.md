# Renderer performance evidence

Start with [current renderer status and backlog](../rendering-performance-overhaul.md).
This directory preserves investigation history; it is not an active work queue.
Rejected candidates remain rejected. Archived unresolved questions are carried
in the current backlog rather than represented as completed work.

## Reports kept in Git

| Record | Contents |
| --- | --- |
| [Pre-consolidation status](history/renderer-status-2026-10-23.md) | Retained capabilities, measured results, issue map and acceptance limits before the recovery checkpoint. |
| [Detailed evidence index](history/evidence-index-2026-10-23.md) | Original protocols, artifact locations, archive hashes and experiment chronology. Its time-relative labels are historical. |
| [Gameplay acceptance](history/render-perf-gameplay-acceptance.md) | Corrected-reference client results, large-view checks and incomplete acceptance. |
| [Shared glTF geometry](history/render-shared-gltf-geometry.md) | Retained runtime ownership, memory/load results and tests. |
| [Shared material textures](history/render-shared-surface-textures.md) | Retained upload reuse, exact mip/image checks and measured loading costs. |
| [Preparation and threading](history/render-perf-parallel-frame-preparation.md) | Original Tier 2 plan, attribution, retained binding-summary pass, rejected trials and remaining questions. |
| [Particle execution](history/render-perf-particle-execution.md) | Retained Taskflow jobs and the accepted tradeoff; rejected storage migration. |
| [Particle presentation](history/render-perf-particle-presentation.md) | Rejected CPU/GPU preparation experiments and uncertainty. |
| [Last particle batching trial](history/render-particle-draw-batches.md) | Draw reductions, exact images and failed frame-time retention gate. |
| [Presentation pacing](history/render-perf-frame-pacing.md) | Rejected policies, cadence limits and unresolved stalls. |

Other original contracts and results remain alongside these in [history/](history/).
They retain historical status text and proposed work as evidence, not current
instructions. Links have been updated for their archive location.

## Raw artifacts and old commits

One-off scripts, captures, logs, frozen binaries and raw timings remain outside
Git. Many recent runs are staged under ignored `build/render-*/` directories;
some have verified archives under `/home/josh/projects/rollnw-evidence/`.
The detailed index records which were archived and their hashes. This history
cleanup neither copies nor re-verifies those external payloads; local-only
artifacts remain local-only. A report in Git does not guarantee raw-artifact
availability in another checkout.

The local branch `backup/main-before-renderer-squash-20261023` preserves the full
pre-consolidation history and pending particle-trial notes. Historical commit IDs
remain reachable there. Production files are unchanged by consolidation; no new
benchmark, runtime verification or performance gain is claimed for this step.
