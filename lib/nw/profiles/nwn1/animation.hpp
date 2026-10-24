#pragma once

#include "../../objects/ObjectHandle.hpp"
#include "../../rules/attributes.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nwn1 {

// Cold resource lookup shared by visual loading and headless action admission.
// This query returns one rig for one appearance; it does not update actors.
std::optional<std::string> resolve_creature_base_rig(const nw::AppearanceInfo& appearance,
    std::string_view race, char sex);

// Resolve the NWN pickup clip from each actor's existing visual appearance and
// supermodel chain. Outputs parallel actors; no pointers or model leases escape.
// Call on spawn/appearance change, never per tick. Missing/stale actors, missing
// clips and invalid durations produce zero (immediate pickup fallback). A shape
// mismatch returns false and clears outputs. No renderer or GPU is required.
bool resolve_pickup_animation_durations(std::span<const nw::ObjectHandle> actors,
    std::span<float> durations);

} // namespace nwn1
