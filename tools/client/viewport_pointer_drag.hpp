#pragma once

#include "viewport_rect.hpp"

#include <glm/vec2.hpp>

#include <cmath>

enum class ClientViewportPointerDragStatus {
    pending,
    dragging,
    cancelled,
};

struct ClientViewportPointerDrag {
    ClientViewportRect viewport;
    glm::vec2 press_position{0.0f};
    bool dragging = false;
};

// A primary pointer has one active gesture. This singleton gate owns no object
// state; callers retain their existing batch spatial update/commit protocols.
// Invalid inputs or a changed coordinate frame cancel instead of rebasing.
[[nodiscard]] inline ClientViewportPointerDragStatus update_viewport_pointer_drag(
    ClientViewportPointerDrag& drag, glm::vec2 position, ClientViewportRect viewport)
{
    const auto& initial = drag.viewport;
    const float left = static_cast<float>(viewport.x);
    const float top = static_cast<float>(viewport.y);
    if (!viewport.valid()
        || viewport.x != initial.x || viewport.y != initial.y
        || viewport.width != initial.width || viewport.height != initial.height
        || !std::isfinite(position.x) || !std::isfinite(position.y)
        || !std::isfinite(drag.press_position.x) || !std::isfinite(drag.press_position.y)
        || drag.press_position.x < left || drag.press_position.y < top
        || drag.press_position.x >= left + static_cast<float>(viewport.width)
        || drag.press_position.y >= top + static_cast<float>(viewport.height)
        || position.x < left || position.y < top
        || position.x >= left + static_cast<float>(viewport.width)
        || position.y >= top + static_cast<float>(viewport.height)) {
        return ClientViewportPointerDragStatus::cancelled;
    }
    constexpr float threshold_pixels = 5.0f;
    const auto delta = position - drag.press_position;
    drag.dragging = drag.dragging
        || std::abs(delta.x) >= threshold_pixels || std::abs(delta.y) >= threshold_pixels;
    return drag.dragging ? ClientViewportPointerDragStatus::dragging
                         : ClientViewportPointerDragStatus::pending;
}
