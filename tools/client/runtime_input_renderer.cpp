#include "renderer.hpp"
#include "runtime_input.hpp"

#include <cmath>

namespace nw::toolset {
namespace {

bool valid_pointer(glm::vec2 point, ClientViewportRect viewport) noexcept
{
    return viewport.valid() && std::isfinite(point.x) && std::isfinite(point.y);
}

} // namespace

std::optional<ClientViewportRay> acquire_runtime_viewport_ray(ClientRenderer& renderer,
    glm::vec2 point, ClientViewportRect viewport)
{
    if (!valid_pointer(point, viewport)) { return std::nullopt; }
    return renderer.viewer_viewport_ray(point.x, point.y, viewport);
}

std::optional<ClientAreaDoorHit> acquire_runtime_door_hit(ClientRenderer& renderer,
    glm::vec2 point, ClientViewportRect viewport, std::span<const ObjectHandle> doors)
{
    if (!valid_pointer(point, viewport)) { return std::nullopt; }
    return renderer.viewer_area_door_hit(point.x, point.y, viewport, doors);
}

} // namespace nw::toolset
