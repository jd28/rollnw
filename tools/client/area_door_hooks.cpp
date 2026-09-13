#include "area_door_hooks.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/AreaTransforms.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nw::toolset {
namespace {

bool finite(glm::vec3 value) noexcept
{
    return std::isfinite(value.x)
        && std::isfinite(value.y)
        && std::isfinite(value.z);
}

ObjectHandle hook_occupant(const Area& area, glm::vec3 position) noexcept
{
    constexpr float occupied_distance_squared = 0.05f * 0.05f;
    for (const auto* door : area.doors) {
        if (!door) {
            continue;
        }
        const auto* spatial = kernel::objects().components().find_spatial(
            door->handle());
        if (spatial && finite(spatial->position)
            && glm::dot(spatial->position - position, spatial->position - position)
                <= occupied_distance_squared) {
            return door->handle();
        }
    }
    return ObjectHandle{};
}

} // namespace

bool build_area_door_hooks(
    const Area& area, AreaDoorHookSnapshot& output, std::string& diagnostic)
{
    output = {};
    diagnostic.clear();
    if (area.width <= 0 || area.height <= 0 || !area.tileset
        || !std::isfinite(area.tileset->tile_height)
        || area.tileset->tile_height <= 0.0f) {
        diagnostic = "Area or tileset does not define valid door-hook data";
        return false;
    }

    const uint64_t tile_count_64 = static_cast<uint64_t>(area.width)
        * static_cast<uint64_t>(area.height);
    if (tile_count_64 > UINT32_MAX || tile_count_64 > area.tiles.size()) {
        diagnostic = "Area tile rows are incomplete or exceed the door-hook index range";
        return false;
    }

    try {
        output.area = area.handle();
        output.width = area.width;
        output.height = area.height;
        output.tile_offsets.reserve(static_cast<size_t>(tile_count_64) + 1u);
        output.tile_offsets.push_back(0u);

        for (uint32_t tile_index = 0;
            tile_index < static_cast<uint32_t>(tile_count_64);
            ++tile_index) {
            const auto& area_tile = area.tiles[tile_index];
            if (area_tile.id >= 0
                && static_cast<size_t>(area_tile.id) < area.tileset->tiles.size()) {
                const auto& tile = area.tileset->tiles[static_cast<size_t>(area_tile.id)];
                glm::mat4 transform{1.0f};
                const int32_t x = static_cast<int32_t>(tile_index
                    % static_cast<uint32_t>(area.width));
                const int32_t y = static_cast<int32_t>(tile_index
                    / static_cast<uint32_t>(area.width));
                if (build_area_tile_world_transform(
                        area.tileset->tile_height,
                        {x, y, area_tile.height, area_tile.orientation},
                        transform)) {
                    for (uint32_t slot_index = 0;
                        slot_index < tile.door_slots.size();
                        ++slot_index) {
                        const auto& slot = tile.door_slots[slot_index];
                        if (slot.type < 0 || !finite(slot.position)
                            || !std::isfinite(slot.orientation)) {
                            continue;
                        }
                        const glm::vec3 position{
                            transform * glm::vec4{slot.position, 1.0f}};
                        const float degrees = slot.orientation
                            + 90.0f * static_cast<float>(area_tile.orientation);
                        const float radians = glm::radians(degrees);
                        const glm::vec3 orientation{
                            std::cos(radians), std::sin(radians), 0.0f};
                        if (!finite(position) || !finite(orientation)
                            || output.hooks.size() >= UINT32_MAX) {
                            continue;
                        }
                        output.hooks.push_back({
                            .position = position,
                            .orientation = orientation,
                            .occupant = hook_occupant(area, position),
                            .type = slot.type,
                            .tile_index = tile_index,
                            .slot_index = slot_index,
                        });
                    }
                }
            }
            output.tile_offsets.push_back(
                static_cast<uint32_t>(output.hooks.size()));
        }
    } catch (const std::bad_alloc&) {
        output = {};
        diagnostic = "Area door-hook snapshot allocation failed";
        return false;
    } catch (const std::length_error&) {
        output = {};
        diagnostic = "Area door-hook snapshot exceeds container capacity";
        return false;
    }
    return true;
}

std::optional<AreaDoorHook> nearest_area_door_hook(
    const AreaDoorHookSnapshot& snapshot,
    glm::vec3 position,
    int32_t type,
    ObjectHandle exclude)
{
    constexpr float tile_size = 10.0f;
    if (snapshot.area.type != ObjectType::area
        || snapshot.width <= 0 || snapshot.height <= 0
        || snapshot.tile_offsets.size()
            != static_cast<size_t>(snapshot.width)
                    * static_cast<size_t>(snapshot.height)
                + 1u
        || type < k_area_door_any_hook_type || !finite(position)) {
        return std::nullopt;
    }

    const int32_t tile_x = std::clamp(
        static_cast<int32_t>(std::floor(position.x / tile_size)),
        0,
        snapshot.width - 1);
    const int32_t tile_y = std::clamp(
        static_cast<int32_t>(std::floor(position.y / tile_size)),
        0,
        snapshot.height - 1);
    float nearest_distance_squared = std::numeric_limits<float>::max();
    const AreaDoorHook* nearest = nullptr;
    for (int32_t y = std::max(0, tile_y - 1);
        y <= std::min(snapshot.height - 1, tile_y + 1);
        ++y) {
        for (int32_t x = std::max(0, tile_x - 1);
            x <= std::min(snapshot.width - 1, tile_x + 1);
            ++x) {
            const size_t tile_index = static_cast<size_t>(y * snapshot.width + x);
            const uint32_t first = snapshot.tile_offsets[tile_index];
            const uint32_t last = snapshot.tile_offsets[tile_index + 1u];
            if (first > last || last > snapshot.hooks.size()) {
                return std::nullopt;
            }
            for (uint32_t hook_index = first; hook_index < last; ++hook_index) {
                const auto& hook = snapshot.hooks[hook_index];
                if ((type != k_area_door_any_hook_type && hook.type != type)
                    || (hook.occupant.type != ObjectType::invalid
                        && hook.occupant != exclude)) {
                    continue;
                }
                const glm::vec2 delta{hook.position.x - position.x,
                    hook.position.y - position.y};
                const float distance_squared = glm::dot(delta, delta);
                if (distance_squared < nearest_distance_squared) {
                    nearest_distance_squared = distance_squared;
                    nearest = &hook;
                }
            }
        }
    }
    return nearest ? std::optional<AreaDoorHook>{*nearest} : std::nullopt;
}

std::optional<int32_t> area_door_hook_type(ObjectHandle door)
{
    if (door.type != ObjectType::door || !kernel::objects().valid(door)) {
        return std::nullopt;
    }
    const Vector<smalls::Value> args{nwn1::bridge::make_object_arg(door)};
    const auto type = nwn1::bridge::call_nwn1_module_int(
        "nwn1.doors", "toolset_hook_type", args);
    return type && *type >= k_area_door_any_hook_type
        ? type
        : std::nullopt;
}

} // namespace nw::toolset
