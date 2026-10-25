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

Gameplay actions must be able to produce animation: movement selects locomotion,
pickup can approach then play a grab clip, and equip can stow/draw. Simulation
owns the action phases and their commit/cancel decisions; the renderer evaluates
the pose. Animation timing must remain available without rendering. NWN model
parsers retain named animation events, but those are not yet connected to a shared
actor action lifecycle.

## Agreed simulation boundary

The shared simulation library owns actions, movement, item ownership and commit
timing. A server advances it; preview hosts the same code locally. The client
owns input mapping, camera, target highlighting and presentation. Extract the
existing fixed-step work rather than introducing a second clock or scheduler.

Keyboard, mouse and controller must converge before authoritative movement.
Keyboard/controller samples already enter `translate_pc_input_samples`; direct
movement and click routes already share the navigation-motion step in
`tick_toolset_preview`. Preserve that convergence when extracting the runtime:

- Direct movement carries analog magnitude; routes generate movement intent.
  Both use the same speed, collision, position and locomotion update.
- Pickup approach must use that route path. Direct movement cancels the route
  and pending pickup before its commit; stick drift inside the deadzone does not.
- Mouse picking and controller target focus produce the same target identity
  and interaction request. Highlighting is client state, not item ownership.
- UI capture suppresses gameplay input through the existing routing authority.
  The current keyboard/stick combination sums then clamps; changing device
  precedence needs an explicit policy and mixed-input tests.

The first shared-runtime acceptance case is request pickup, approach, commit,
complete, with the same result headlessly and in preview. Cover keyboard and
controller interruption, click replacement, invalid targets and UI capture.
Runtime cost is active-action iteration plus existing navigation/commit work;
no performance improvement is claimed. Networking, prediction and a second
movement implementation are outside this first step.

Controller focus selection, simultaneous-input precedence and missing-clip
timing remain open decisions here. Mouse hover is implemented separately below
that boundary; it does not establish a controller interaction binding.

## Replay requirement

The shared simulation must support recording and replaying a complete scenario.
The first guarantee is exact authoritative-state replay with the same build,
content and initial state; cross-platform floating-point reproducibility is not
yet established.

- Capture the initial world, active actions, scheduled events, simulation tick
  and random-generator states, plus build/content identities. An initial seed
  alone cannot restore an already running world.
- Record ordered commands after device mapping and before simulation admission,
  with tick and actor/control identity. This includes rejected commands so their
  results can also be compared. External facts that affect gameplay must enter
  through the recorded boundary; wall clock and renderer callbacks cannot drive
  simulation outcomes.
- Replay drives the same tick without SDL, UI, networking or rendering. Use
  simulation-owned time, controlled randomness and stable processing order.
  Existing preview render-frame catch-up/drop policy is not a replay clock.
- Compare canonical authoritative state after each tick, including action state,
  event queues, positions, inventory/equipment and RNG state. Do not hash padding,
  addresses, GPU resources or presentation-only caches. Report the first mismatch.
- Camera/input-device presentation can be recorded separately when visual replay
  needs to reproduce the original view; it must not change gameplay results.

Acceptance recording: click-to-move, controller interruption, approach/pickup,
equip and drop. Run the recording with different render-frame schedules and
headlessly, compare every tick and the final state, and cover interruption before
commit. This is a requirement for the upcoming shared runtime, not a claim that
the current preview is already replayable. Costs are snapshot/log storage and
canonical-state comparison in the validation harness; no per-frame recording or
hashing overhead is imposed on normal play before that feature is implemented.

Reconstruction stores simulation data, never suspended VM execution. Capture
state at a completed tick boundary, after scripts and atomic commits finish.
Scripts run to completion; delayed work is represented by explicit action phases
or scheduled invocation records with a stable function identifier, due tick and
plain argument data. A fresh VM resolves those identifiers and executes a fresh
invocation when due. No VM stacks, instruction pointers or captured closures are
part of the state protocol. State plus subsequent ordered inputs determines the
future; state alone does not predict future player input.

Persistent script variables belong in explicit world/component data, not hidden
VM globals. Acceptance must also reconstruct at a tick boundary mid-action:
discard the live world and VM, restore into a fresh runtime, feed the remaining
commands, and compare every subsequent tick against the uninterrupted run.

The existing `kernel::EventHandle` includes callback/data pointers. Those are
in-process execution details, not a reconstructible event format; they must be
rebuilt from stable identities and explicit payload data at this boundary.

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

A shared action owner now lives in `nw-gameplay`: flat per-actor progress,
movement requests and terminal results. Preview uses its existing mover between
prepare and commit; item operations execute inside the fixed tick. It supports
one active action per actor, replaced by a new request. A general action FIFO,
general animation-marker handling and reconstruction remain unimplemented.
Pickup now has fixed-tick grab and recovery phases. NWN1's `getlow` duration is
resolved from the actor's rig/supermodel chain without a renderer. The supplied
clip lasts 0.5 seconds and has no event marker, so the explicit pickup policy
commits at its end after revalidation. It then plays backward to stand up;
recovery can be interrupted without undoing the transfer. Missing/invalid clips
fall back to immediate pickup. Presentation samples simulation-owned time and
cannot trigger a transfer.

Hand equip/unequip now uses the same fixed tick. NWN1 resolves `drwright` and
`drwleft` plus the earliest valid `draw_weapon` marker on spawn. The supplied
clips last about 0.667 seconds with a marker at 0.3 seconds (duplicate authored
markers become one commit). Equip plays forward; unequip reverses the clip and
marker time. Missing markers commit at clip end; missing/invalid clips and other
slots retain immediate changes. Ownership and capacity are revalidated at the
marker. The action retains its commit status until the pose finishes, so it
never repeats the mutation or result. Replacement before commit leaves ownership
unchanged; replacement afterward keeps the committed state. No general animation
event dispatcher, separate stow/draw swap sequence, or action queue is added.
The stripped NWN dedicated distribution has model index entries without rig
payloads. It therefore takes the no-clip fallback unless animation metadata is
supplied with its content; this is not yet a portable server content package.
The shared action state accepts resolved tick durations independently of rendering.
No second scheduler was introduced. Headless tests cover cancellation before
and after the hand marker, ownership revalidation, exact-cell unequip, and the
zero-timing fallback. Renderer tests cover both hand clips and missing-clip idle
fallback. The GPU item-loop test checks a quarterstaff before and after its
marker, forward/reverse poses, and completion without repeated commits.
