#include "particle_render.hpp"

#include "particle_system.hpp"

#include "../formats/Image.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace nw::render {
namespace {

glm::vec3 normalized_transform_direction(const glm::mat4& transform, const glm::vec3& direction)
{
    const glm::vec3 world = glm::vec3(transform * glm::vec4(direction, 0.0f));
    const float length2 = glm::dot(world, world);
    if (length2 <= 1.0e-8f) {
        return direction;
    }
    return world / std::sqrt(length2);
}

void axis_billboard_axes(const ParticleBillboardView& view, const glm::vec3& position, const glm::vec3& locked_up,
    glm::vec3& right, glm::vec3& up)
{
    up = glm::normalize(locked_up);
    const glm::vec3 to_camera = view.camera_position - position;
    const glm::vec3 planar = to_camera - up * glm::dot(to_camera, up);
    if (glm::dot(planar, planar) <= 1.0e-8f) {
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        right = glm::normalize(glm::cross(up, glm::normalize(planar)));
    }
}

void local_plane_axes(const glm::mat4& transform, glm::vec3& right, glm::vec3& up)
{
    const glm::vec3 normal = normalized_transform_direction(transform, glm::vec3{0.0f, 0.0f, 1.0f});
    glm::vec3 local_right = normalized_transform_direction(transform, glm::vec3{1.0f, 0.0f, 0.0f});
    local_right -= normal * glm::dot(local_right, normal);

    const float right_len2 = glm::dot(local_right, local_right);
    if (right_len2 <= 1.0e-8f) {
        glm::vec3 local_up = normalized_transform_direction(transform, glm::vec3{0.0f, 1.0f, 0.0f});
        local_up -= normal * glm::dot(local_up, normal);
        const float up_len2 = glm::dot(local_up, local_up);
        if (up_len2 <= 1.0e-8f) {
            return;
        }

        up = local_up / std::sqrt(up_len2);
        right = glm::normalize(glm::cross(up, normal));
        return;
    }

    right = local_right / std::sqrt(right_len2);
    up = glm::normalize(glm::cross(normal, right));
}

void world_z_plane_axes(glm::vec3& right, glm::vec3& up) noexcept
{
    right = glm::vec3{1.0f, 0.0f, 0.0f};
    up = glm::vec3{0.0f, 1.0f, 0.0f};
}

} // namespace

ParticleSpriteFrameRect particle_sprite_sheet_frame_rect(
    const ParticleSpriteSheet& sheet, uint16_t frame, const nw::Image* source_image, bool flip_rows) noexcept
{
    const uint16_t columns = std::max<uint16_t>(1, sheet.columns);
    const uint16_t rows = std::max<uint16_t>(1, sheet.rows);
    const uint32_t frame_count = static_cast<uint32_t>(columns) * static_cast<uint32_t>(rows);
    const uint32_t clamped_frame = frame_count > 0
        ? std::min<uint32_t>(frame, frame_count - 1u)
        : 0u;

    const uint16_t column = static_cast<uint16_t>(clamped_frame % columns);
    const uint16_t logical_row = static_cast<uint16_t>(std::min<uint32_t>(clamped_frame / columns, rows - 1u));
    const uint16_t row = flip_rows ? static_cast<uint16_t>(rows - 1u - logical_row) : logical_row;

    ParticleSpriteFrameRect rect{
        .u0 = static_cast<float>(column) / static_cast<float>(columns),
        .v0 = static_cast<float>(row) / static_cast<float>(rows),
        .u1 = static_cast<float>(column + 1u) / static_cast<float>(columns),
        .v1 = static_cast<float>(row + 1u) / static_cast<float>(rows),
        .column = column,
        .row = row,
    };

    if (source_image && source_image->valid() && source_image->width() > 1 && source_image->height() > 1
        && (columns > 1u || rows > 1u)) {
        const float inset_u = 0.5f / static_cast<float>(source_image->width());
        const float inset_v = 0.5f / static_cast<float>(source_image->height());
        if ((rect.u1 - rect.u0) > inset_u * 2.0f && (rect.v1 - rect.v0) > inset_v * 2.0f) {
            rect.u0 += inset_u;
            rect.v0 += inset_v;
            rect.u1 -= inset_u;
            rect.v1 -= inset_v;
        }
    }

    return rect;
}

ParticleBillboardAxes resolve_particle_billboard_axes(const ParticleBillboardView& view,
    const ParticleSystemInstance& system, const ParticleRenderPacket& packet, uint32_t particle_index,
    const ParticleBillboardAxes& fallback) noexcept
{
    ParticleBillboardAxes axes = fallback;
    const auto& core = system.particles.core;
    if (particle_index >= core.position.size()) {
        return axes;
    }

    const glm::vec3 position = core.position[particle_index];
    if (packet.mode == ParticleRenderMode::billboard_world_z) {
        world_z_plane_axes(axes.right, axes.up);
    } else if (packet.mode == ParticleRenderMode::billboard_local_z) {
        if (particle_index >= core.emitter_id.size()) {
            return axes;
        }
        const uint16_t emitter_id = core.emitter_id[particle_index];
        if (emitter_id >= system.emitters.size()) {
            return axes;
        }

        local_plane_axes(system.emitters[emitter_id].world_transform, axes.right, axes.up);
    } else if (packet.mode == ParticleRenderMode::aligned_world_z) {
        axis_billboard_axes(view, position, glm::vec3{0.0f, 0.0f, 1.0f}, axes.right, axes.up);
    }

    return axes;
}

ParticleBillboardAxes particle_billboard_sprite_axes(
    ParticleRenderMode mode, const ParticleBillboardAxes& axes, float rotation) noexcept
{
    if (rotation == 0.0f) {
        return axes;
    }

    const float angle = rotation * 2.0f * 3.14159265358979323846f;
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    if (mode == ParticleRenderMode::aligned_world_z) {
        const float up_len2 = glm::dot(axes.up, axes.up);
        const glm::vec3 up = up_len2 > 1.0e-8f ? axes.up / std::sqrt(up_len2) : glm::vec3{0.0f, 0.0f, 1.0f};
        glm::vec3 right = axes.right - up * glm::dot(axes.right, up);
        const float right_len2 = glm::dot(right, right);
        if (right_len2 <= 1.0e-8f) {
            right = std::abs(up.z) < 0.9f
                ? glm::normalize(glm::cross(up, glm::vec3{0.0f, 0.0f, 1.0f}))
                : glm::vec3{1.0f, 0.0f, 0.0f};
        } else {
            right /= std::sqrt(right_len2);
        }

        return ParticleBillboardAxes{
            .right = right * c + glm::cross(up, right) * s,
            .up = up,
        };
    }

    return ParticleBillboardAxes{
        .right = axes.right * c + axes.up * s,
        .up = axes.right * -s + axes.up * c,
    };
}

bool particle_named_texture_available(std::string_view texture_name, const nw::Image* source_image) noexcept
{
    return texture_name.empty() || (source_image && source_image->valid());
}

} // namespace nw::render
