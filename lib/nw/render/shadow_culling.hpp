#pragma once

#include "model.hpp"

#include <cstdint>
#include <span>

namespace nw::render {

// Frame-local batch protocol: bounds and matrices are borrowed for this call;
// caller-owned masks retain source order, with bit i denoting shadow view i.
// Up to eight views; a size mismatch rejects the entire batch without writes.
// Empty batches/views are valid. Orthographic views use interval projection;
// other views and nonfinite inputs retain the eight-corner projection policy.
[[nodiscard]] bool collect_shadow_visibility_masks(
    std::span<const Bounds> bounds,
    std::span<const glm::mat4> world_to_shadow,
    std::span<uint8_t> masks);

} // namespace nw::render
