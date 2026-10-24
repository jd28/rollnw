#include "actions.hpp"

#include <glm/geometric.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

namespace nw::gameplay {
namespace {

static_assert(std::is_trivially_copyable_v<ActorActionState>);

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool active(const ActorActionState& state)
{
    return state.phase == ActionPhase::requested || state.phase == ActionPhase::moving
        || state.phase == ActionPhase::grabbing || state.phase == ActionPhase::recovering;
}

void finish(ActorActionState& state, ActorActionResult& result, ActionStatus status)
{
    state.phase = ActionPhase::finished;
    state.status = status;
    result.status = status;
    result.item = state.target;
}

bool valid_request(const ActorActionRequest& request)
{
    switch (request.kind) {
    case ActionKind::move_to:
        return finite(request.destination);
    case ActionKind::cancel:
        return true;
    case ActionKind::equip:
    case ActionKind::unequip:
        return request.target.type == ObjectType::item && request.slot >= 0 && request.slot < 18;
    case ActionKind::pickup:
    case ActionKind::drop:
        return request.target.type == ObjectType::item;
    }
    return false;
}

// Runtime handles protect against destruction/reuse between ticks and script
// callbacks. They resolve into existing component storage; the action loop and
// movement protocol remain contiguous, indexed rows. No new object graph.
bool actor_in_area(ObjectHandle actor, ObjectHandle area)
{
    if (actor.type != ObjectType::creature || !kernel::objects().valid(actor)) { return false; }
    const auto* spatial = kernel::objects().components().find_spatial(actor);
    return spatial && spatial->area == area.id && finite(spatial->position);
}

} // namespace

bool submit_actor_actions(std::span<ActorActionState> states,
    std::span<const ActorActionRequest> requests, std::span<ActorActionResult> results)
{
    std::fill(results.begin(), results.end(), ActorActionResult{.status = ActionStatus::invalid_input});
    if (requests.size() != results.size()) { return false; }
    for (size_t index = 0; index < requests.size(); ++index) {
        const auto& request = requests[index];
        auto& result = results[index];
        result.item = request.target;
        if (request.actor_index >= states.size() || !valid_request(request)) { continue; }
        auto& state = states[request.actor_index];
        state.target = request.target;
        state.destination = request.destination;
        state.slot = request.slot;
        state.elapsed_ticks = 0;
        state.kind = request.kind;
        state.phase = ActionPhase::requested;
        state.status = ActionStatus::accepted;
        result.status = ActionStatus::accepted;
    }
    return true;
}

bool prepare_actor_actions(ObjectHandle area, std::span<ActorActionState> states,
    std::span<ActionMovement> movement, std::span<ActorActionResult> results)
{
    std::fill(movement.begin(), movement.end(), ActionMovement{});
    std::fill(results.begin(), results.end(), ActorActionResult{});
    if (states.size() != movement.size() || states.size() != results.size()) { return false; }
    const bool area_valid = area.type == ObjectType::area && kernel::objects().valid(area);
    for (size_t index = 0; index < states.size(); ++index) {
        auto& state = states[index];
        if (state.phase > ActionPhase::finished || state.kind > ActionKind::cancel
            || ((state.phase == ActionPhase::grabbing || state.phase == ActionPhase::recovering)
                && state.kind != ActionKind::pickup)
            || (state.kind == ActionKind::move_to && !finite(state.destination))) {
            movement[index].kind = ActionMovementKind::stop;
            finish(state, results[index], ActionStatus::invalid_input);
            continue;
        }
        if (!active(state)) { continue; }
        auto& motion = movement[index];
        auto& result = results[index];
        if (!area_valid || !actor_in_area(state.actor, area)) {
            motion.kind = ActionMovementKind::stop;
            finish(state, result, ActionStatus::inactive);
            continue;
        }
        if (state.kind == ActionKind::cancel) {
            motion.kind = ActionMovementKind::stop;
            finish(state, result, ActionStatus::cancelled);
        } else if (state.kind == ActionKind::pickup) {
            if (state.phase == ActionPhase::recovering) {
                motion.kind = ActionMovementKind::stop;
                continue;
            }
            const auto* spatial = state.target.type == ObjectType::item && kernel::objects().valid(state.target)
                ? kernel::objects().components().find_spatial(state.target)
                : nullptr;
            if (!spatial || spatial->area != area.id || !finite(spatial->position)) {
                motion.kind = ActionMovementKind::stop;
                finish(state, result, ActionStatus::rejected);
                result.transfer_status = ItemTransferStatus::invalid_ownership;
                continue;
            }
            if (state.phase == ActionPhase::grabbing) {
                motion.kind = ActionMovementKind::stop;
            } else if (state.phase == ActionPhase::requested || spatial->position != state.destination) {
                state.destination = spatial->position;
                motion = {.destination = state.destination, .kind = ActionMovementKind::move_to};
                state.phase = ActionPhase::moving;
            }
        } else if (state.kind == ActionKind::move_to) {
            if (state.phase == ActionPhase::requested) {
                motion = {.destination = state.destination, .kind = ActionMovementKind::move_to};
                state.phase = ActionPhase::moving;
            }
        } else {
            motion.kind = ActionMovementKind::stop;
        }
    }
    return true;
}

bool complete_actor_actions(ObjectHandle area, const nav::NavWorldState& navigation,
    float pickup_reach, std::span<ActorActionState> states,
    std::span<const ActionMovementStatus> movement, std::span<ActorActionResult> results)
{
    std::fill(results.begin(), results.end(), ActorActionResult{});
    if (states.size() != movement.size() || states.size() != results.size()
        || !std::isfinite(pickup_reach) || pickup_reach <= 0.0f
        || std::ranges::any_of(movement, [](auto value) { return value > ActionMovementStatus::failed; })) { return false; }
    const auto generation = kernel::services().generation();
    for (size_t index = 0; index < states.size(); ++index) {
        auto& state = states[index];
        if (!active(state)) { continue; }
        auto& result = results[index];
        if (kernel::services().generation() != generation || area.type != ObjectType::area
            || !kernel::objects().valid(area) || !actor_in_area(state.actor, area)) {
            finish(state, result, ActionStatus::inactive);
            continue;
        }
        if (state.phase == ActionPhase::recovering) {
            if (state.elapsed_ticks < state.pickup_ticks) { ++state.elapsed_ticks; }
            if (state.elapsed_ticks >= state.pickup_ticks) { state.phase = ActionPhase::finished; }
            continue;
        }
        if (state.kind == ActionKind::move_to || state.kind == ActionKind::pickup) {
            if (movement[index] == ActionMovementStatus::failed) {
                finish(state, result, ActionStatus::movement_failed);
                continue;
            }
            if (state.kind == ActionKind::move_to) {
                if (movement[index] == ActionMovementStatus::arrived) { finish(state, result, ActionStatus::applied); }
                continue;
            }
        }
        auto* actor = kernel::objects().get<Creature>(state.actor);
        auto* item = state.target.type == ObjectType::item ? kernel::objects().get<Item>(state.target) : nullptr;
        if (!item) {
            finish(state, result, ActionStatus::rejected);
            result.transfer_status = ItemTransferStatus::invalid_ownership;
            continue;
        }
        if (state.kind == ActionKind::pickup || state.kind == ActionKind::drop) {
            if (state.kind == ActionKind::pickup && state.phase == ActionPhase::grabbing) {
                if (state.elapsed_ticks < state.pickup_ticks) { ++state.elapsed_ticks; }
                if (state.elapsed_ticks < state.pickup_ticks) { continue; }
            } else if (state.kind == ActionKind::pickup) {
                const auto& components = kernel::objects().components();
                const auto* target = components.find_spatial(state.target);
                const auto* source = components.find_spatial(state.actor);
                const bool within_reach = target && source && glm::distance(source->position, target->position) <= pickup_reach;
                if (!within_reach && movement[index] == ActionMovementStatus::moving) { continue; }
                if (within_reach && state.pickup_ticks != 0) {
                    state.phase = ActionPhase::grabbing;
                    state.elapsed_ticks = 0;
                    continue;
                }
            }
            const std::array transfers{ItemTransfer{state.target,
                state.kind == ActionKind::pickup ? ItemTransferDirection::pickup : ItemTransferDirection::drop}};
            const auto transferred = transfer_area_items(area, state.actor, navigation, pickup_reach, transfers);
            if (state.kind == ActionKind::pickup && state.phase != ActionPhase::grabbing
                && movement[index] == ActionMovementStatus::moving
                && (transferred.status == ItemTransferStatus::out_of_reach || transferred.status == ItemTransferStatus::blocked)) { continue; }
            result.transfer_status = transferred.status;
            result.changed = transferred.transferred != 0;
            finish(state, result, transferred.ok() ? ActionStatus::applied : ActionStatus::rejected);
            if (transferred.ok() && state.kind == ActionKind::pickup && state.pickup_ticks != 0) {
                state.phase = ActionPhase::recovering;
                state.elapsed_ticks = 0;
            }
        } else if (state.kind == ActionKind::equip || state.kind == ActionKind::unequip) {
            const auto slot = static_cast<EquipIndex>(state.slot);
            const bool equip = state.kind == ActionKind::equip;
            if (state.slot < 0 || state.slot >= 18 || (equip && !actor->inventory().has_item(item))
                || (!equip && get_equipped_item(actor, slot) != item)) {
                finish(state, result, ActionStatus::rejected);
                continue;
            }
            const auto before = actor->equipment.equip_version;
            const bool applied = equip ? nwn1::equip_item(actor, item, slot) : nwn1::unequip_item(actor, slot) == item;
            if (kernel::services().generation() != generation || !(actor = kernel::objects().get<Creature>(state.actor))) {
                result.changed = result.equipment_changed = true;
                finish(state, result, ActionStatus::profile_failed);
                continue;
            }
            result.changed = result.equipment_changed = actor->equipment.equip_version != before;
            finish(state, result, applied ? ActionStatus::applied : result.changed ? ActionStatus::profile_failed
                                                                                   : ActionStatus::rejected);
        } else {
            finish(state, result, ActionStatus::invalid_input);
        }
    }
    return true;
}

} // namespace nw::gameplay
