#include "area_object_editor.hpp"
#include "editor_input.hpp"
#include "toolset_backend.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/toolset_visual.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace nw::toolset {

bool editable_area_object(nw::ObjectHandle object) noexcept
{
    switch (object.type) {
    case nw::ObjectType::creature:
    case nw::ObjectType::door:
    case nw::ObjectType::encounter:
    case nw::ObjectType::item:
    case nw::ObjectType::placeable:
    case nw::ObjectType::sound:
    case nw::ObjectType::store:
    case nw::ObjectType::trigger:
    case nw::ObjectType::waypoint:
        return true;
    default:
        return false;
    }
}

bool snap_area_door_preview(
    nw::ObjectHandle area,
    nw::ObjectHandle door,
    glm::vec3 requested_position,
    std::unique_ptr<nw::toolset::AreaDoorHookSnapshot>& hooks,
    int32_t& hook_type,
    nw::ObjectSpatialState& preview,
    std::string& diagnostic)
{
    constexpr int32_t k_unresolved_hook_type = -2;
    constexpr int32_t k_invalid_hook_type = -3;
    if (door.type != nw::ObjectType::door) {
        return true;
    }
    if (hook_type == k_unresolved_hook_type) {
        hook_type = nw::toolset::area_door_hook_type(door)
                        .value_or(k_invalid_hook_type);
    }
    if (hook_type == k_invalid_hook_type) {
        diagnostic = "Door hook policy is invalid or unavailable";
        return false;
    }
    if (!hooks) {
        const auto* live_area = nw::kernel::objects().get<nw::Area>(area);
        if (!live_area) {
            diagnostic = "Active area is invalid or stale";
            return false;
        }
        try {
            hooks = std::make_unique<nw::toolset::AreaDoorHookSnapshot>();
        } catch (const std::bad_alloc&) {
            diagnostic = "Door-hook snapshot allocation failed";
            return false;
        }
        if (!nw::toolset::build_area_door_hooks(*live_area, *hooks, diagnostic)) {
            hooks.reset();
            return false;
        }
    }
    const auto hook = nw::toolset::nearest_area_door_hook(
        *hooks, requested_position, hook_type, door);
    if (!hook) {
        diagnostic = "No compatible unoccupied door hook is available";
        return false;
    }
    preview.position = hook->position;
    preview.orientation = hook->orientation;
    diagnostic.clear();
    return true;
}

bool placement_blueprint_resource(const nw::Resource& resource) noexcept
{
    return resource.valid()
        && (resource.type == nw::ResourceType::utc
            || resource.type == nw::ResourceType::utd
            || resource.type == nw::ResourceType::ute
            || resource.type == nw::ResourceType::utp
            || resource.type == nw::ResourceType::uti
            || resource.type == nw::ResourceType::utm
            || resource.type == nw::ResourceType::uts
            || resource.type == nw::ResourceType::utt
            || resource.type == nw::ResourceType::utw);
}

bool region_blueprint_resource(const nw::Resource& resource) noexcept
{
    return resource.type == nw::ResourceType::ute
        || resource.type == nw::ResourceType::utt;
}

bool area_object_placement_position_valid(nw::ObjectHandle area, glm::vec3 position)
{
    constexpr float k_tile_size = 10.0f;
    if (area.type != nw::ObjectType::area) {
        return false;
    }
    const auto* live_area = nw::kernel::objects().get<nw::Area>(area);
    return live_area && live_area->width > 0 && live_area->height > 0
        && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)
        && position.x >= 0.0f
        && position.x <= static_cast<float>(live_area->width) * k_tile_size
        && position.y >= 0.0f
        && position.y <= static_cast<float>(live_area->height) * k_tile_size;
}

std::optional<CommandResult> apply_area_object_wheel_action(const EditorWheelAction& action,
    ToolsetBackend& backend, const CommandContext& context, ObjectHandle target)
{
    if (!std::isfinite(action.amount) || action.amount == 0) { return std::nullopt; }
    if (action.kind == EditorWheelActionKind::sound_radius) {
        if (const auto sound = nwn1::sound_toolset_visual_state(target)) {
            const float radius = std::max(sound->distance_min, sound->distance_max * std::pow(1.1f, action.amount));
            if (std::isfinite(radius) && radius != sound->distance_max) {
                return backend.resize_sound_radius({.sound = target, .before = sound->distance_max, .after = radius}, context);
            }
        }
        return std::nullopt;
    }
    if (action.kind != EditorWheelActionKind::object_scale && action.kind != EditorWheelActionKind::object_rotate) { return std::nullopt; }
    const bool rotate = action.kind == EditorWheelActionKind::object_rotate;
    const float value = rotate ? action.amount * 15.0f : std::pow(1.1f, action.amount);
    std::array<char, 64> buffer{};
    const auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general, std::numeric_limits<float>::max_digits10);
    if (formatted.ec != std::errc{}) { return std::nullopt; }
    const std::string argument{buffer.data(), formatted.ptr};
    return backend.execute_command(rotate ? "object.transform.rotate" : "object.transform.scale", {argument}, context);
}

} // namespace nw::toolset
