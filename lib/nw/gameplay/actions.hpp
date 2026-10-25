#pragma once

#include "../objects/Inventory.hpp"
#include "../objects/item_transfers.hpp"

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace nw::gameplay {

constexpr uint32_t action_ticks_per_second = 60;

enum class ActionKind : uint8_t { pickup,
    equip,
    unequip,
    drop,
    move_to,
    cancel,
    move_inventory };
enum class ActionPhase : uint8_t { idle,
    requested,
    moving,
    grabbing,
    recovering,
    finished,
    drawing };
enum class ActionStatus : uint8_t {
    none,
    accepted,
    applied,
    cancelled,
    rejected,
    invalid_input,
    inactive,
    profile_failed,
    movement_failed,
};
enum class ActionMovementKind : uint8_t { none,
    move_to,
    stop };
enum class ActionMovementStatus : uint8_t { moving,
    arrived,
    failed };

struct ActorActionRequest {
    uint32_t actor_index = UINT32_MAX;
    ActionKind kind = ActionKind::cancel;
    ObjectHandle target{};
    glm::vec3 destination{0.0f};
    int32_t slot = -1;
    InventorySlot inventory_cell; // all -1 means automatic placement for unequip
};

struct EquipmentActionTiming {
    uint32_t duration_ticks = 0;
    uint32_t equip_tick = 0;
    uint32_t unequip_tick = 0;
};

// Version 3 in-process state. One row per actor, in stable owner-defined order.
// The owner binds actor once, retains rows between ticks, and destroys them with
// the world. All action progress is explicit data; no VM, callback or allocation
// is retained. Object handles are existing kernel identities, not portable IDs:
// reconstruction must remap them along with the world before resuming these rows.
struct ActorActionState {
    ObjectHandle actor{};
    ObjectHandle target{};
    glm::vec3 destination{0.0f};
    int32_t slot = -1;
    InventorySlot inventory_cell;
    // Owner-resolved clip duration, rounded up to 60 Hz ticks. Zero means the
    // actor has no pickup clip and uses immediate transfer. Retained on replace.
    uint32_t pickup_ticks = 0;
    // Owner-resolved right/left hand timing. Commit ticks must not exceed the
    // duration. Zero duration selects immediate equipment changes. Retained on replace.
    std::array<EquipmentActionTiming, 2> hand_timings;
    uint32_t elapsed_ticks = 0;
    ActionKind kind = ActionKind::cancel;
    ActionPhase phase = ActionPhase::idle;
    ActionStatus status = ActionStatus::none;
};

struct ActionMovement {
    glm::vec3 destination{0.0f};
    ActionMovementKind kind = ActionMovementKind::none;
};

struct ActorActionResult {
    ActionStatus status = ActionStatus::none;
    ItemTransferStatus transfer_status = ItemTransferStatus::success;
    ObjectHandle item{};
    bool changed = false;
    bool equipment_changed = false;
    [[nodiscard]] bool ok() const noexcept { return status == ActionStatus::applied; }
};

// Ordered owner-thread requests index the action table. Valid requests replace
// the actor's previous action; malformed indices/tags/targets/slots/coordinates
// reject that request without changing its previous state. No world mutation.
// Results parallel requests; mismatched sizes reject the entire batch unchanged.
bool submit_actor_actions(std::span<ActorActionState> states,
    std::span<const ActorActionRequest> requests, std::span<ActorActionResult> results);

// Tick protocol: submit -> prepare -> existing movement update -> complete.
// State, movement and result rows have identical actor indexing. Inputs/outputs
// are borrowed only for the call. Missing/stale actors or targets finish with an
// explicit failure and stop movement. Idle/finished rows produce empty outputs.
// Pickup owns a move_to request, reissued only when its target moves; movement
// state/path storage belongs to the movement system, never to a second mover.
bool prepare_actor_actions(ObjectHandle area, std::span<ActorActionState> states,
    std::span<ActionMovement> movement, std::span<ActorActionResult> results);

// Revalidate and commit after movement, once per 60 Hz tick, in actor-row order.
// Pickup stops within reach and enters grabbing at elapsed tick zero. Its clip
// end commits the transfer; rendering never advances authoritative progress.
// The owner clears the route immediately when completion enters grabbing;
// subsequent prepare calls also emit stop while that phase is active.
// Cancellation/replacement before commit preserves ownership. Target position
// and ownership are checked again at commit. Blocked/out-of-reach attempts continue
// only while movement is still progressing. No route/target failure is retried
// automatically. Terminal results emit once and require stopping that action's
// route. Equipment callback errors after commit carry changed=true.
// Finite positive reach and equal row counts are required; invalid batch shape
// or reach rejects before mutation. Native commit validation/allocation is reused.
// Zero pickup_ticks is the explicit no-clip fallback: commit when reachable.
// A successful grab emits its result immediately, then spends pickup_ticks in
// recovery playing the clip backward. Recovery never transfers again; replacing
// it cannot undo the committed item. Its end produces no second result.
// Hand equipment plays drawing through duration_ticks, committing once at the
// selected equip/unequip tick. status remains accepted before commit, then holds
// its commit result while the clip finishes. Replacement after commit preserves
// equipment. Missing timing and other slots retain immediate behavior.
bool complete_actor_actions(ObjectHandle area, const nav::NavWorldState& navigation,
    float pickup_reach, std::span<ActorActionState> states,
    std::span<const ActionMovementStatus> movement, std::span<ActorActionResult> results);

} // namespace nw::gameplay
