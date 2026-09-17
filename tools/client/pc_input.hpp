#pragma once

#include "preview_session.hpp"

#include <array>
#include <span>

namespace nw::toolset {

// Physical keys, with their meaning defined only in the PC map below.
enum class PcKey : uint8_t { w,
    s,
    q,
    e,
    a,
    d,
    left,
    right,
    up,
    down,
    count };

struct PcSourceEligibility {
    bool keyboard = true;
    bool controller = true;
    bool pointer = true;
};

struct PcDeviceSample {
    std::array<bool, static_cast<size_t>(PcKey::count)> keys{};
    glm::vec2 left_stick{0.0f};
    glm::vec2 right_stick{0.0f};
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
    bool left_shoulder = false;
    bool right_shoulder = false;
    bool controller_connected = false;
    PreviewInputSample pending;
    glm::vec2 mouse_look_pixels{0.0f};
    double mouse_sample_seconds = 0.0;
    float wheel_zoom = 0.0f;
    PcSourceEligibility eligibility;
};

// Caller-owned, equal-length contiguous buffers, consumed before reacquisition.
// Connected controller axes are finite and in [-1,1]; elapsed time is finite
// and nonnegative. Invalid rows or mismatched sizes clear all outputs and
// return invalid_input. Inactive pointer payload fields retain the existing
// unspecified values; flags alone determine whether they are consumed.
// Missing/claimed sources contribute zero; movement
// clamps to [-1,1], combined look/zoom retain the existing unclamped values.
// This map has no role, SDL/DOM pointers, actor ownership or preview lifecycle.
PreviewStatus translate_pc_input_samples(std::span<const PcDeviceSample> inputs,
    std::span<PreviewInputSample> outputs) noexcept;

// One row per ordered world click. The current consumer supplies dense door
// interaction facts or a projected navigation point from its own world/session.
// No lifecycle or role policy enters this shared pointer map.
struct PcPointerAction {
    glm::vec3 door_bounds_min{0.0f};
    glm::vec3 door_bounds_max{0.0f};
    glm::vec3 navigation_position{0.0f};
    uint32_t door_index = UINT32_MAX;
    bool door_interactable = false;
    bool navigation_projected = false;
};

// Replaces pending pointer actions while preserving other sample fields.
// Missing projection drops the action. Invalid rows/sizes drop all output
// pointer actions and return invalid_input, following the existing setters.
PreviewStatus apply_pc_pointer_actions(std::span<const PcPointerAction> inputs,
    std::span<PreviewInputSample> pending) noexcept;

} // namespace nw::toolset
