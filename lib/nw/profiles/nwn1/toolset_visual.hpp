#pragma once

#include "../../objects/ObjectHandle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace nwn1 {

struct SoundToolsetVisualState {
    float distance_min = 0.0f;
    float distance_max = 0.0f;
    float elevation = 0.0f;
    bool positional = false;
    bool random_position = false;
};

struct SoundToolsetVisualBatchStats {
    size_t input_count = 0;
    size_t output_count = 0;
    size_t rejected_count = 0;
};

// Typed SmallS projection for a borrowed batch of Sound handles. Output and
// validity rows remain parallel to input. Mismatched spans reject the entire
// batch; invalid or malformed Sounds reject only their corresponding row.
// This protocol does not expose SoundState layout to C++ callers.
SoundToolsetVisualBatchStats sound_toolset_visual_states(
    std::span<const nw::ObjectHandle> sounds,
    std::span<SoundToolsetVisualState> states,
    std::span<uint8_t> valid);

// A selected Sound is a genuine UI singleton; it follows the batch path with
// one row.
[[nodiscard]] std::optional<SoundToolsetVisualState>
sound_toolset_visual_state(nw::ObjectHandle sound);

[[nodiscard]] bool replace_sound_toolset_radius(
    nw::ObjectHandle sound, float expected, float replacement);

} // namespace nwn1
