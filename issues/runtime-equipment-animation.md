# Runtime equipment action and animation timing

The atomic equipment batch prepares final inventory/slot state and revalidates
it at commit. NWN1 publishes committed old/new Item identities and slots after
effects are updated. The notification includes an explicit `authoring` flag;
editor commits and undo set it, and gameplay commits clear it. Animation
consumers must ignore authoring notifications. A headless caller can use this
without client commands, undo history or a renderer. These facts are the animation
integration boundary.

Submit pending gameplay rows through `nwn1.item.change_equipment` at the chosen
commit point. A false result means validation rejected without changes or events;
a callback fault is an execution error after storage committed, so callers must
not treat that error as a safe retry. Prepared native snapshots must be retained
unchanged. They are an in-process protocol, not a serialized action format.

Before adding animated gameplay swaps, choose from actual gameplay requirements:

- Which actor action/tick owns the request, and which animation marker commits
  it? Define fallback timing when the character has no matching clip.
- When may movement, combat, another equipment request or actor destruction
  cancel it? Cancellation before commit must not change ownership/effects.
- Whether capacity is reserved during wind-up or revalidated at commit. Current
  behavior revalidates; an old request may fail if inventory/equipment changes.
- Which source/target weapon classes select stow/draw clips and attachment
  visibility? Keep old/new visual presentation separate from authoritative
  membership and never replay an animation for editor undo.

Reuse the existing action/event and model-animation facilities once these inputs
are defined. No guessed durations, resource-name heuristics, second scheduler,
or renderer-dependent gameplay rules are implied. Verify interrupted/deferred
requests, missing clips and stale state with a headless simulation test and a
rendered animation-marker test before claiming animated swaps are implemented.
