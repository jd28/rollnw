#pragma once

#include "../../objects/ObjectHandle.hpp"
#include "../../rules/attributes.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nwn1 {

// Cold resource lookup shared by visual loading and headless action admission.
// This query returns one rig for one appearance; it does not update actors.
std::optional<std::string> resolve_creature_base_rig(const nw::AppearanceInfo& appearance,
    std::string_view race, char sex);

struct EquipmentAnimationTiming {
    float duration = 0.0f;
    float draw_marker = -1.0f; // no valid draw_weapon event; commit at clip end
};

struct CreatureActionAnimationTimings {
    float pickup_duration = 0.0f;
    std::array<EquipmentAnimationTiming, 2> hands; // right, left
};

// Resolve getlow, drwright and drwleft from each actor's visual appearance and
// supermodel chain. Outputs parallel actors; no pointers or model leases escape.
// Call on spawn/appearance change, never per tick. Missing/stale actors, missing
// clips and invalid durations produce zero (immediate action fallback). A shape
// mismatch returns false and clears outputs. No renderer or GPU is required.
bool resolve_creature_action_animation_timings(std::span<const nw::ObjectHandle> actors,
    std::span<CreatureActionAnimationTimings> timings);

} // namespace nwn1
