# Runtime world placement and item transfers

Status: open. Required boundary for game use of the current authoring work.

The shared renderer grounds area-item models from live spatial state in both
game and toolset modes. Engine navigation exposes strict batch position queries.
Neither is an authoritative runtime drop/pickup operation.

Runtime commands must validate live identity, ownership, destination, and any
game-rule eligibility before committing inventory/area membership and spatial
state together. A failed batch must preserve ownership and location. The
renderer and editor mouse hits must not establish authoritative legality.
The editor's unrestricted item/placeable height is an authoring capability,
not a player's drop-placement rule.

Reuse the simulation's navigation world and current obstacle state where a
navigation query is needed. Do not call editor undo commands, depend on toolset
mutation epochs, or rebuild an area navigation mesh for each gameplay drop.
The existing ground-pose cache is renderer-owned asset-derived data; it must
not require a renderer on a headless simulation server.

Resolve from actual game requirements before implementation:

- What chooses a drop target: an explicit valid position, the actor's position,
  or a bounded search around the actor? What surface/height rules apply to items?
- What range, reachability, and item rules govern pickup and drop?
- Which runtime command/tick boundary owns atomic membership transfers and
  publishes changed spatial/visual rows to rendering?
- What measured drop/spawn volume and latency budget must incremental scene
  updates support? Full editor scene rebuilds are not a measured runtime design.

No physics engine, network protocol, selection tolerance, or new persistent
schema is implied by this issue. Add only the mechanisms required by the chosen
gameplay operations and verify rejection without mutation, successful transfers,
and agreement between simulation coordinates and rendered ground poses.
