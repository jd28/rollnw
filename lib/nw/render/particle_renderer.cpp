#include "particle_renderer.hpp"

#include "shader_provider.hpp"

#include <nw/log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace nw::render {

namespace {

constexpr float kProjectileBodySpriteSizeScale = 0.5f;
constexpr float kProjectileBodySpriteTailSizeFloor = 0.35f;

glm::vec4 unpack_rgba8(uint32_t value)
{
    return glm::vec4{
               float(value & 0xffu),
               float((value >> 8u) & 0xffu),
               float((value >> 16u) & 0xffu),
               float((value >> 24u) & 0xffu),
           }
    * (1.0f / 255.0f);
}

glm::vec3 normalized_transform_direction(const glm::mat4& transform, const glm::vec3& direction)
{
    const glm::vec3 world = glm::vec3(transform * glm::vec4(direction, 0.0f));
    const float length2 = glm::dot(world, world);
    if (length2 <= 1.0e-8f) {
        return direction;
    }
    return world / std::sqrt(length2);
}

float particle_emitter_uniform_scale(const ParticleSystemInstance& system, uint16_t emitter_id)
{
    if (emitter_id >= system.emitters.size()) {
        return 1.0f;
    }

    const glm::mat4& transform = system.emitters[emitter_id].world_transform;
    const float sx = glm::length(glm::vec3(transform[0]));
    const float sy = glm::length(glm::vec3(transform[1]));
    const float sz = glm::length(glm::vec3(transform[2]));
    const float scale = (sx + sy + sz) / 3.0f;
    return scale > 1.0e-6f ? scale : 1.0f;
}

glm::vec3 ambient_tint_hue(const glm::vec3& ambient)
{
    const float max_component = std::max({ambient.x, ambient.y, ambient.z});
    if (max_component <= 1.0e-6f) {
        return glm::vec3{1.0f};
    }
    return glm::clamp(ambient / max_component, glm::vec3{0.0f}, glm::vec3{1.0f});
}

glm::vec4 finalize_particle_color(glm::vec4 color, const ParticleRenderPacket& packet, const RenderContext& ctx)
{
    if (packet.tint_to_scene_ambient) {
        const glm::vec3 tint = ambient_tint_hue(ctx.lighting.ambient);
        color.x *= tint.x;
        color.y *= tint.y;
        color.z *= tint.z;
    }
    if (packet.blend == ParticleBlendMode::additive) {
        color.a *= packet.opacity_scale;
    } else {
        color *= packet.opacity_scale;
    }
    return color;
}

void camera_billboard_axes(const RenderContext& ctx, glm::vec3& right, glm::vec3& up)
{
    const auto forward = render_context_camera_forward(ctx);
    right = glm::cross(forward, glm::vec3{0.0f, 0.0f, 1.0f});
    if (glm::dot(right, right) <= 1.0e-8f) {
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        right = glm::normalize(right);
    }
    up = glm::normalize(glm::cross(right, forward));
}

void axis_billboard_axes(const RenderContext& ctx, const glm::vec3& position, const glm::vec3& locked_up,
    glm::vec3& right, glm::vec3& up)
{
    up = glm::normalize(locked_up);
    const glm::vec3 to_camera = ctx.camera_position - position;
    const glm::vec3 planar = to_camera - up * glm::dot(to_camera, up);
    if (glm::dot(planar, planar) <= 1.0e-8f) {
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        right = glm::normalize(glm::cross(up, glm::normalize(planar)));
    }
}

void plane_aligned_billboard_axes(const RenderContext& ctx, const glm::vec3& position, const glm::vec3& normal,
    const glm::vec3& fallback_right, const glm::vec3& fallback_up, glm::vec3& right, glm::vec3& up)
{
    const float normal_len2 = glm::dot(normal, normal);
    if (normal_len2 <= 1.0e-8f) {
        right = fallback_right;
        up = fallback_up;
        return;
    }

    const glm::vec3 n = normal / std::sqrt(normal_len2);
    const glm::vec3 to_camera = ctx.camera_position - position;
    const float to_camera_len2 = glm::dot(to_camera, to_camera);
    if (to_camera_len2 <= 1.0e-8f) {
        right = fallback_right;
        up = fallback_up;
        return;
    }

    const glm::vec3 view = to_camera / std::sqrt(to_camera_len2);
    glm::vec3 candidate_right = glm::cross(view, n);
    const float right_len2 = glm::dot(candidate_right, candidate_right);
    if (right_len2 <= 1.0e-8f) {
        right = fallback_right;
        up = fallback_up;
        return;
    }

    right = candidate_right / std::sqrt(right_len2);
    const glm::vec3 candidate_up = glm::cross(n, right);
    const float up_len2 = glm::dot(candidate_up, candidate_up);
    if (up_len2 <= 1.0e-8f) {
        right = fallback_right;
        up = fallback_up;
        return;
    }

    up = candidate_up / std::sqrt(up_len2);
}

bool is_white_alpha_mask_texture(const nw::Image& image)
{
    if (!image.valid() || image.width() == 0 || image.height() == 0 || image.channels() < 4 || !image.data()) {
        return false;
    }

    const auto* pixels = image.data();
    const size_t pixel_count = static_cast<size_t>(image.width()) * static_cast<size_t>(image.height());
    const size_t stride = static_cast<size_t>(image.channels());
    const size_t sample_step = std::max<size_t>(1, pixel_count / 256u);

    bool saw_low_alpha = false;
    bool saw_high_alpha = false;
    float min_rgb = 1.0f;
    float max_channel_delta = 0.0f;

    for (size_t i = 0; i < pixel_count; i += sample_step) {
        const size_t idx = i * stride;
        const float r = pixels[idx + 0] * (1.0f / 255.0f);
        const float g = pixels[idx + 1] * (1.0f / 255.0f);
        const float b = pixels[idx + 2] * (1.0f / 255.0f);
        const float a = pixels[idx + 3] * (1.0f / 255.0f);

        min_rgb = std::min(min_rgb, std::min(r, std::min(g, b)));
        max_channel_delta = std::max(max_channel_delta, std::max(std::abs(r - g), std::max(std::abs(g - b), std::abs(b - r))));
        saw_low_alpha |= a <= 0.03f;
        saw_high_alpha |= a >= 0.80f;
    }

    return saw_low_alpha && saw_high_alpha && min_rgb >= 0.96f && max_channel_delta <= 0.02f;
}

glm::vec3 ribbon_right_axis(
    const RenderContext& ctx, const glm::vec3& a, const glm::vec3& b, const glm::vec3& fallback_right)
{
    const glm::vec3 tangent = b - a;
    const float tangent_len2 = glm::dot(tangent, tangent);
    if (tangent_len2 <= 1.0e-8f) {
        return fallback_right;
    }

    const glm::vec3 midpoint = 0.5f * (a + b);
    glm::vec3 to_camera = ctx.camera_position - midpoint;
    const float to_camera_len2 = glm::dot(to_camera, to_camera);
    if (to_camera_len2 <= 1.0e-8f) {
        return fallback_right;
    }

    const glm::vec3 right = glm::cross(glm::normalize(tangent), glm::normalize(to_camera));
    const float right_len2 = glm::dot(right, right);
    if (right_len2 <= 1.0e-8f) {
        return fallback_right;
    }
    return right / std::sqrt(right_len2);
}

float particle_path_noise(uint32_t seed)
{
    const float value = std::sin(static_cast<float>(seed) * 12.9898f) * 43758.5453f;
    return 2.0f * (value - std::floor(value)) - 1.0f;
}

} // namespace

ParticleRenderer::ParticleRenderer(nw::gfx::Context* ctx) noexcept
    : ctx_(ctx)
{
}

ParticleRenderer::~ParticleRenderer()
{
    for (auto& arena : instance_arenas_) {
        arena.destroy();
    }
    if (billboard_vertices_.valid()) nw::gfx::destroy_buffer(billboard_vertices_);
    if (path_transparent_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, path_transparent_pipeline_);
    if (path_additive_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, path_additive_pipeline_);
    if (path_cutout_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, path_cutout_pipeline_);
    if (transparent_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, transparent_pipeline_);
    if (additive_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, additive_pipeline_);
    if (cutout_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, cutout_pipeline_);
}

bool ParticleRenderer::initialize(ShaderProvider& shader_provider)
{
    if (!ctx_) {
        return false;
    }

    auto vs_particle = shader_provider.get_shader("render_particle_billboard.vs.hlsl");
    auto vs_particle_path = shader_provider.get_shader("render_particle_path.vs.hlsl");
    auto ps_particle = shader_provider.get_shader("render_particle_billboard.ps.hlsl");
    if (!vs_particle.valid() || !vs_particle_path.valid() || !ps_particle.valid()) {
        return true;
    }

    constexpr std::array<ParticleBillboardVertex, 6> kParticleBillboardQuad{{
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{1.0f, -1.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f}, {0.0f, 0.0f}},
        {{1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-1.0f, 1.0f}, {0.0f, 1.0f}},
    }};
    nw::gfx::BufferDesc vertex_desc{};
    vertex_desc.size = sizeof(kParticleBillboardQuad);
    vertex_desc.usage = nw::gfx::BufferUsage::Vertex;
    vertex_desc.cpu_visible = true;
    billboard_vertices_ = nw::gfx::create_buffer(ctx_, vertex_desc);
    if (!billboard_vertices_.valid()) {
        LOG_F(WARNING, "Failed to create particle billboard vertex buffer");
    } else {
        auto* mapped = nw::gfx::map_buffer(billboard_vertices_);
        if (!mapped) {
            LOG_F(WARNING, "Failed to map particle billboard vertex buffer");
        } else {
            std::memcpy(mapped, kParticleBillboardQuad.data(), sizeof(kParticleBillboardQuad));
            nw::gfx::unmap_buffer(billboard_vertices_);
        }
    }

    nw::gfx::PipelineDesc particle_desc{};
    particle_desc.vs = vs_particle;
    particle_desc.fs = ps_particle;
    particle_desc.uses_bindless_sampled_textures = true;
    particle_desc.uses_draw_uniforms = true;
    particle_desc.uses_storage_buffer = true;
    particle_desc.storage_buffer_count = 1;
    particle_desc.uses_single_texture = false;
    particle_desc.depth_test = true;
    particle_desc.depth_write = false;
    particle_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
    particle_desc.vertex_stride = sizeof(ParticleBillboardVertex);
    particle_desc.vertex_attributes = {
        {0, offsetof(ParticleBillboardVertex, corner), nw::gfx::VertexFormat::Float2},
        {1, offsetof(ParticleBillboardVertex, texcoord), nw::gfx::VertexFormat::Float2},
    };
    transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, particle_desc);
    if (!transparent_pipeline_.valid()) {
        LOG_F(WARNING, "Failed to create transparent particle pipeline");
    }

    auto additive_desc = particle_desc;
    additive_desc.blend_mode = nw::gfx::BlendMode::additive;
    additive_pipeline_ = nw::gfx::create_pipeline(ctx_, additive_desc);
    if (!additive_pipeline_.valid()) {
        LOG_F(WARNING, "Failed to create additive particle pipeline");
    }

    auto cutout_desc = particle_desc;
    cutout_desc.depth_write = true;
    cutout_desc.blend_mode = nw::gfx::BlendMode::disabled;
    cutout_pipeline_ = nw::gfx::create_pipeline(ctx_, cutout_desc);
    if (!cutout_pipeline_.valid()) {
        LOG_F(WARNING, "Failed to create cutout particle pipeline");
    }

    auto path_desc = particle_desc;
    path_desc.vs = vs_particle_path;
    path_desc.uses_storage_buffer = false;
    path_desc.storage_buffer_count = 0;
    path_desc.vertex_stride = sizeof(ParticleVertex);
    path_desc.vertex_attributes = {
        {0, offsetof(ParticleVertex, position), nw::gfx::VertexFormat::Float3},
        {1, offsetof(ParticleVertex, texcoord), nw::gfx::VertexFormat::Float2},
        {2, offsetof(ParticleVertex, color), nw::gfx::VertexFormat::Float4},
    };
    path_transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, path_desc);
    if (!path_transparent_pipeline_.valid()) {
        LOG_F(WARNING, "Failed to create transparent path particle pipeline");
    }

    auto path_additive_desc = path_desc;
    path_additive_desc.blend_mode = nw::gfx::BlendMode::additive;
    path_additive_pipeline_ = nw::gfx::create_pipeline(ctx_, path_additive_desc);
    if (!path_additive_pipeline_.valid()) {
        LOG_F(WARNING, "Failed to create additive path particle pipeline");
    }

    auto path_cutout_desc = path_desc;
    path_cutout_desc.depth_write = true;
    path_cutout_desc.blend_mode = nw::gfx::BlendMode::disabled;
    path_cutout_pipeline_ = nw::gfx::create_pipeline(ctx_, path_cutout_desc);
    if (!path_cutout_pipeline_.valid()) {
        LOG_F(WARNING, "Failed to create cutout path particle pipeline");
    }

    return true;
}

nw::gfx::StorageSpan ParticleRenderer::upload_instances(
    nw::gfx::CommandList* cmd, std::span<const ParticleBillboardInstance> instances)
{
    if (!cmd || instances.empty()) {
        return {};
    }

    nw::gfx::FrameInfo frame{};
    if (!nw::gfx::get_frame_info(ctx_, frame)) {
        return {};
    }

    const auto idx = std::min<size_t>(frame.frame_index, instance_arenas_.size() - 1);
    auto& arena = instance_arenas_[idx];
    const uint32_t size = static_cast<uint32_t>(instances.size_bytes());
    if (arena.frame_id() != frame.frame_id) {
        if (!arena.reset(ctx_, frame.frame_id, size)) {
            return {};
        }
    }

    return arena.write(ctx_, instances.data(), size);
}

void ParticleRenderer::render(nw::gfx::CommandList* cmd, const ParticleSystemInstance& system,
    std::span<const ParticleMaterialDef> source_materials, const RenderContext& ctx,
    ParticleResourceProvider& resources)
{
    if (!cmd || (!transparent_pipeline_.valid() && !cutout_pipeline_.valid())) {
        return;
    }
    const auto* effect = system.effect;
    if (!effect) {
        return;
    }

    glm::vec3 camera_right{1.0f, 0.0f, 0.0f};
    glm::vec3 camera_up{0.0f, 0.0f, 1.0f};
    camera_billboard_axes(ctx, camera_right, camera_up);

    const auto packets = system.render_packets.span();
    const auto& core = system.particles.core;
    const ParticleBillboardView particle_view{.camera_position = ctx.camera_position};
    for (const auto& packet : packets) {
        if (packet.material >= effect->materials.size() || packet.mode == ParticleRenderMode::mesh) {
            continue;
        }

        const auto& source_material = packet.material < source_materials.size()
            ? source_materials[packet.material]
            : ParticleMaterialDef{};
        const auto* texture_name = source_material.texture.empty() ? nullptr : source_material.texture.c_str();
        const bool premultiply_alpha = packet.blend != ParticleBlendMode::additive;
        const auto texture = texture_name ? resources.get_texture(texture_name, premultiply_alpha) : resources.fallback_texture();
        if (!texture.valid()) {
            continue;
        }
        const auto texture_index = nw::gfx::get_bindless_texture_index(ctx_, texture);
        if (!nw::gfx::bindless_texture_index_valid(texture_index)) {
            continue;
        }
        const auto* source_image = texture_name ? resources.get_source_image(texture_name) : nullptr;
        const bool texture_rows_flipped = texture_name && resources.texture_rows_flipped(texture_name, premultiply_alpha);
        const bool additive_alpha_mask_billboard = packet.blend == ParticleBlendMode::additive
            && source_image
            && is_white_alpha_mask_texture(*source_image);

        const float cols = std::max<uint16_t>(1, packet.sheet.columns);
        const float rows = std::max<uint16_t>(1, packet.sheet.rows);
        const bool uses_path_geometry = packet.mode == ParticleRenderMode::linked_chain
            || packet.mode == ParticleRenderMode::beam;
        vertices_.clear();
        instances_.clear();
        if (uses_path_geometry) {
            vertices_.reserve(std::max<size_t>(static_cast<size_t>(packet.count) * 6, 96));
        } else {
            instances_.reserve(std::max<size_t>(packet.count, 16u));
        }
        order_.clear();
        order_.reserve(packet.count);
        for (const uint32_t particle_index : particle_render_packet_indices(system, packet)) {
            if (particle_index >= core.position.size()) {
                continue;
            }
            order_.push_back(particle_index);
        }
        if (packet.sort_back_to_front && packet.mode != ParticleRenderMode::linked_chain) {
            std::sort(order_.begin(), order_.end(),
                [&](uint32_t lhs, uint32_t rhs) {
                    const float lhs_dist2 = glm::dot(core.position[lhs] - ctx.camera_position, core.position[lhs] - ctx.camera_position);
                    const float rhs_dist2 = glm::dot(core.position[rhs] - ctx.camera_position, core.position[rhs] - ctx.camera_position);
                    if (lhs_dist2 != rhs_dist2) {
                        return lhs_dist2 > rhs_dist2;
                    }
                    return lhs < rhs;
                });
        }

        if (packet.mode == ParticleRenderMode::linked_chain || packet.mode == ParticleRenderMode::beam) {
            chain_order_ = order_;
            if (packet.mode == ParticleRenderMode::linked_chain) {
                chain_order_.erase(
                    std::remove_if(chain_order_.begin(), chain_order_.end(),
                        [&](uint32_t idx) { return idx >= core.position.size(); }),
                    chain_order_.end());
            }

            auto emit_ribbon_segment = [&](const glm::vec3& a, const glm::vec3& b, float half_width,
                                           const glm::vec4& color_a, const glm::vec4& color_b,
                                           float u0, float u1, float v0, float v1) {
                const glm::vec3 segment_right = ribbon_right_axis(ctx, a, b, camera_right) * std::max(half_width, 0.001f);
                const glm::vec3 p0 = a - segment_right;
                const glm::vec3 p1 = a + segment_right;
                const glm::vec3 p2 = b + segment_right;
                const glm::vec3 p3 = b - segment_right;
                vertices_.push_back(ParticleVertex{.position = p0, .texcoord = {u0, v0}, .color = color_a});
                vertices_.push_back(ParticleVertex{.position = p1, .texcoord = {u0, v1}, .color = color_a});
                vertices_.push_back(ParticleVertex{.position = p2, .texcoord = {u1, v1}, .color = color_b});
                vertices_.push_back(ParticleVertex{.position = p0, .texcoord = {u0, v0}, .color = color_a});
                vertices_.push_back(ParticleVertex{.position = p2, .texcoord = {u1, v1}, .color = color_b});
                vertices_.push_back(ParticleVertex{.position = p3, .texcoord = {u1, v0}, .color = color_b});
            };
            auto emit_segment_card = [&](const glm::vec3& a, const glm::vec3& b, float half_width,
                                         const glm::vec4& color_a, const glm::vec4& color_b,
                                         float u0, float u1, float v0, float v1) {
                const glm::vec3 tangent = b - a;
                const float tangent_len2 = glm::dot(tangent, tangent);
                if (tangent_len2 <= 1.0e-8f) {
                    return;
                }

                const float half_length = 0.5f * std::sqrt(tangent_len2);
                const glm::vec3 segment_forward = tangent / (2.0f * half_length);
                const glm::vec3 center = 0.5f * (a + b);
                const glm::vec3 segment_right = ribbon_right_axis(ctx, a, b, camera_right) * std::max(half_width, 0.001f);
                const glm::vec3 along = segment_forward * half_length;
                const glm::vec3 p0 = center - along - segment_right;
                const glm::vec3 p1 = center + along - segment_right;
                const glm::vec3 p2 = center + along + segment_right;
                const glm::vec3 p3 = center - along + segment_right;
                vertices_.push_back(ParticleVertex{.position = p0, .texcoord = {u0, v0}, .color = color_a});
                vertices_.push_back(ParticleVertex{.position = p1, .texcoord = {u0, v1}, .color = color_b});
                vertices_.push_back(ParticleVertex{.position = p2, .texcoord = {u1, v1}, .color = color_b});
                vertices_.push_back(ParticleVertex{.position = p0, .texcoord = {u0, v0}, .color = color_a});
                vertices_.push_back(ParticleVertex{.position = p2, .texcoord = {u1, v1}, .color = color_b});
                vertices_.push_back(ParticleVertex{.position = p3, .texcoord = {u1, v0}, .color = color_a});
            };

            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 1.0f / cols;
            float v1 = 1.0f / rows;
            if (!chain_order_.empty()) {
                const size_t i = chain_order_.front();
                const ParticleSpriteFrameRect rect = particle_sprite_sheet_frame_rect(
                    packet.sheet, core.frame[i], source_image, texture_rows_flipped);
                u0 = rect.u0;
                v0 = rect.v0;
                u1 = rect.u1;
                v1 = rect.v1;
            }

            if (packet.path.kind == ParticleRenderPathKind::beam) {
                const uint32_t segment_count = std::max<uint32_t>(1u, packet.path.subdivisions);
                const glm::vec4 color = !order_.empty()
                    ? finalize_particle_color(unpack_rgba8(core.color_rgba8[order_.front()]), packet, ctx)
                    : glm::vec4{1.0f};
                const float half_width = !order_.empty()
                    ? 0.22f * std::max(core.size_x[order_.front()], 0.001f)
                    : 0.1f;
                constexpr float kPi = 3.14159265358979323846f;
                glm::vec3 prev = packet.path.source;
                for (uint32_t segment = 1; segment <= segment_count; ++segment) {
                    const float t = static_cast<float>(segment) / static_cast<float>(segment_count);
                    const glm::vec3 next = glm::mix(packet.path.source, packet.path.target, t);
                    const float seg_v0 = std::lerp(v0, v1, static_cast<float>(segment - 1) / static_cast<float>(segment_count));
                    const float seg_v1 = std::lerp(v0, v1, t);
                    const glm::vec3 jitter_axis = ribbon_right_axis(ctx, prev, next, camera_right);
                    const float jitter_prev = particle_path_noise(1000u + segment * 2u - 1u);
                    const float jitter_next = particle_path_noise(1000u + segment * 2u);
                    const float end_fade_prev = std::sin(kPi * (static_cast<float>(segment - 1) / static_cast<float>(segment_count)));
                    const float end_fade_next = std::sin(kPi * t);
                    const glm::vec3 prev_jittered = prev + jitter_axis * (0.14f * half_width * jitter_prev * end_fade_prev);
                    const glm::vec3 next_jittered = next + jitter_axis * (0.14f * half_width * jitter_next * end_fade_next);
                    emit_segment_card(prev_jittered, next_jittered, half_width, color, color, u0, u1, seg_v0, seg_v1);
                    prev = next;
                }
            } else if (packet.mode == ParticleRenderMode::linked_chain
                && packet.path.kind == ParticleRenderPathKind::bezier) {
                for (uint32_t idx : chain_order_) {
                    if (idx >= core.position.size()
                        || idx >= system.particles.bezier.source_position.size()
                        || idx >= system.particles.bezier.target_position.size()) {
                        continue;
                    }

                    const glm::vec3 source = system.particles.bezier.source_position[idx];
                    const glm::vec3 middle = core.position[idx];
                    const glm::vec3 target = system.particles.bezier.target_position[idx];
                    const glm::vec4 color = finalize_particle_color(unpack_rgba8(core.color_rgba8[idx]), packet, ctx);
                    const float half_width = 0.5f * std::max(core.size_x[idx], 0.001f);
                    const glm::vec3 rv = ribbon_right_axis(ctx, source, target, camera_right)
                        * std::max(half_width, 0.001f);
                    const float len0 = glm::distance(source, middle);
                    const float len1 = glm::distance(middle, target);
                    const float total_len = std::max(1.0e-5f, len0 + len1);
                    const float vm = v0 + (v1 - v0) * (len0 / total_len);

                    if (len0 > 1.0e-5f) {
                        vertices_.push_back(ParticleVertex{.position = source - rv, .texcoord = {u0, v0}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = source + rv, .texcoord = {u1, v0}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = middle + rv, .texcoord = {u1, vm}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = source - rv, .texcoord = {u0, v0}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = middle + rv, .texcoord = {u1, vm}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = middle - rv, .texcoord = {u0, vm}, .color = color});
                    }

                    if (len1 > 1.0e-5f) {
                        vertices_.push_back(ParticleVertex{.position = middle - rv, .texcoord = {u0, vm}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = middle + rv, .texcoord = {u1, vm}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = target + rv, .texcoord = {u1, v1}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = middle - rv, .texcoord = {u0, vm}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = target + rv, .texcoord = {u1, v1}, .color = color});
                        vertices_.push_back(ParticleVertex{.position = target - rv, .texcoord = {u0, v1}, .color = color});
                    }
                }
            } else if (chain_order_.size() >= 2) {
                for (size_t i = 1; i < chain_order_.size(); ++i) {
                    const size_t a_idx = chain_order_[i - 1];
                    const size_t b_idx = chain_order_[i];
                    const glm::vec4 color_a = finalize_particle_color(unpack_rgba8(core.color_rgba8[a_idx]), packet, ctx);
                    const glm::vec4 color_b = finalize_particle_color(unpack_rgba8(core.color_rgba8[b_idx]), packet, ctx);
                    const float hw = 0.5f * std::max(0.5f * (core.size_x[a_idx] + core.size_x[b_idx]), 0.001f);
                    const float seg_u0 = std::lerp(u0, u1, static_cast<float>(i - 1) / static_cast<float>(chain_order_.size() - 1));
                    const float seg_u1 = std::lerp(u0, u1, static_cast<float>(i) / static_cast<float>(chain_order_.size() - 1));
                    emit_ribbon_segment(core.position[a_idx], core.position[b_idx], hw, color_a, color_b, seg_u0, seg_u1, v0, v1);
                }
            }
        }

        if (packet.mode != ParticleRenderMode::linked_chain
            && packet.mode != ParticleRenderMode::beam) {
            glm::vec4 uniform_color{1.0f};
            if (packet.uniform_color && !order_.empty()) {
                uniform_color = unpack_rgba8(core.color_rgba8[order_.front()]);
            }
            for (uint32_t ordered_index = 0; ordered_index < order_.size(); ++ordered_index) {
                const size_t i = order_[ordered_index];

                const glm::vec3 position = core.position[i];
                const float emitter_scale = particle_emitter_uniform_scale(system, core.emitter_id[i]);
                const bool projectile_body_sprite = packet.semantic == ParticleRenderSemantic::projectile_body_sprite;
                const ParticleBillboardAxes packet_axes = resolve_particle_billboard_axes(particle_view, system, packet,
                    static_cast<uint32_t>(i), ParticleBillboardAxes{.right = camera_right, .up = camera_up});
                glm::vec3 quad_right = packet_axes.right;
                glm::vec3 quad_up = packet_axes.up;

                float sx = 0.5f * std::max(core.size_x[i] * emitter_scale, 0.001f);
                float sy = 0.5f * std::max(core.size_y[i] * emitter_scale, 0.001f);
                if (packet.mode == ParticleRenderMode::velocity_aligned
                    || packet.mode == ParticleRenderMode::stretched) {
                    const glm::vec3 velocity = core.velocity[i];
                    const float velocity_len = glm::length(velocity);
                    glm::vec3 alignment_dir{0.0f};
                    if (velocity_len > 1.0e-5f) {
                        alignment_dir = velocity / velocity_len;
                    } else if (packet.mode == ParticleRenderMode::velocity_aligned
                        && i < core.render_direction.size()
                        && glm::dot(core.render_direction[i], core.render_direction[i]) > 1.0e-6f) {
                        alignment_dir = glm::normalize(core.render_direction[i]);
                    } else if ((packet.mode == ParticleRenderMode::stretched
                                   || (packet.mode == ParticleRenderMode::velocity_aligned
                                       && core.emitter_id[i] < system.emitters.size()
                                       && core.size_y[i] * emitter_scale <= 1.0e-3f))
                        && core.emitter_id[i] < system.emitters.size()) {
                        const auto& emitter_state = system.emitters[core.emitter_id[i]];
                        const float emitter_speed = glm::length(emitter_state.linear_velocity);
                        if (emitter_speed > 1.0e-5f) {
                            alignment_dir = emitter_state.linear_velocity / emitter_speed;
                        } else {
                            alignment_dir = normalized_transform_direction(
                                emitter_state.world_transform, glm::vec3{0.0f, 0.0f, 1.0f});
                        }
                    }

                    if (glm::dot(alignment_dir, alignment_dir) > 1.0e-6f) {
                        if (packet.mode == ParticleRenderMode::velocity_aligned
                            && packet.deadspace_radians > 1.0e-6f) {
                            const glm::vec3 to_camera = glm::normalize(ctx.camera_position - position);
                            const float view_alignment = std::abs(glm::dot(alignment_dir, to_camera));
                            const float deadspace_cos = std::cos(packet.deadspace_radians);
                            if (view_alignment >= deadspace_cos) {
                                continue;
                            }
                        }

                        if (packet.mode == ParticleRenderMode::velocity_aligned) {
                            if (projectile_body_sprite) {
                                axis_billboard_axes(ctx, position, alignment_dir, quad_right, quad_up);
                                const float head_t = 1.0f - std::clamp(core.normalized_age[i], 0.0f, 1.0f);
                                const float body_size = std::max(core.size_x_end[i], core.size_x_begin[i])
                                    * kProjectileBodySpriteSizeScale
                                    * (kProjectileBodySpriteTailSizeFloor
                                        + (1.0f - kProjectileBodySpriteTailSizeFloor) * head_t);
                                sx = 0.5f * std::max(body_size * emitter_scale, 0.001f);
                                sy = sx;
                            } else {
                                plane_aligned_billboard_axes(
                                    ctx, position, alignment_dir, camera_right, camera_up, quad_right, quad_up);
                            }
                        } else {
                            quad_right = ribbon_right_axis(ctx, position, position + alignment_dir, camera_right);
                            quad_up = alignment_dir;
                        }
                        if (packet.mode == ParticleRenderMode::stretched) {
                            sy *= std::max(packet.blur_length, 1.0f);
                        }
                    }
                }

                const float rotation = packet.uniform_rotation && !order_.empty()
                    ? core.rotation[order_.front()]
                    : core.rotation[i];
                const ParticleBillboardAxes rotated_axes = particle_billboard_sprite_axes(
                    packet.mode, ParticleBillboardAxes{.right = quad_right, .up = quad_up}, rotation);
                const glm::vec3 rotated_right = rotated_axes.right * sx;
                const glm::vec3 rotated_up = rotated_axes.up * sy;

                const uint16_t frame = packet.uniform_frame && !order_.empty()
                    ? core.frame[order_.front()]
                    : core.frame[i];
                const ParticleSpriteFrameRect rect = particle_sprite_sheet_frame_rect(
                    packet.sheet, frame, source_image, texture_rows_flipped);
                const float u0 = rect.u0;
                const float v0 = rect.v0;
                const float u1 = rect.u1;
                const float v1 = rect.v1;
                glm::vec4 color = packet.uniform_color
                    ? uniform_color
                    : unpack_rgba8(core.color_rgba8[i]);
                color = finalize_particle_color(color, packet, ctx);

                instances_.push_back(ParticleBillboardInstance{
                    .center = glm::vec4(position, 1.0f),
                    .right = glm::vec4(rotated_right, 0.0f),
                    .up = glm::vec4(rotated_up, 0.0f),
                    .texcoord_rect = glm::vec4(u0, v0, u1, v1),
                    .color = color,
                });
            }
        }
        if (uses_path_geometry ? vertices_.empty() : instances_.empty()) {
            continue;
        }

        auto pipeline = uses_path_geometry ? path_transparent_pipeline_ : transparent_pipeline_;
        if (packet.blend == ParticleBlendMode::cutout) {
            pipeline = uses_path_geometry ? path_cutout_pipeline_ : cutout_pipeline_;
        } else if (packet.blend == ParticleBlendMode::additive) {
            pipeline = uses_path_geometry ? path_additive_pipeline_ : additive_pipeline_;
        }
        if (!pipeline.valid()) {
            LOG_F(WARNING, "particle pipeline invalid: blend={} mode={} - packet skipped",
                static_cast<uint32_t>(packet.blend), static_cast<uint32_t>(packet.mode));
            continue;
        }

        ParticleDrawConstants constants{
            .view = ctx.view,
            .projection = ctx.projection,
            .texture_index = texture_index,
            .alpha_cutout = packet.blend == ParticleBlendMode::cutout ? 1u : 0u,
            .additive_like = packet.blend == ParticleBlendMode::additive ? 1u : 0u,
            .additive_alpha_gated = packet.blend == ParticleBlendMode::additive
                    && (additive_alpha_mask_billboard
                        || (packet.mode != ParticleRenderMode::billboard
                            && packet.mode != ParticleRenderMode::billboard_local_z
                            && packet.mode != ParticleRenderMode::billboard_world_z
                            && packet.mode != ParticleRenderMode::aligned_world_z))
                ? 1u
                : 0u,
            .alpha_cutout_threshold = 0.5f,
            .additive_intensity = packet.blend == ParticleBlendMode::additive
                ? ((packet.mode == ParticleRenderMode::linked_chain
                       || packet.mode == ParticleRenderMode::beam)
                          ? 0.95f
                          : 1.0f)
                : 1.0f,
            .alpha_intensity = 1.0f,
            .fog_color = glm::vec4(ctx.fog.color, 0.0f),
            .fog_range = glm::vec2(ctx.fog.start_distance, ctx.fog.end_distance),
            .fog_amount = ctx.fog.amount,
            .fog_enabled = ctx.fog.enabled ? 1u : 0u,
        };
        auto uniforms = nw::gfx::allocate_uniform_span(ctx_, sizeof(constants));
        if (!uniforms.data) {
            continue;
        }
        std::memcpy(uniforms.data, &constants, sizeof(constants));

        nw::gfx::cmd_bind_pipeline(cmd, pipeline);
        if (uses_path_geometry) {
            const size_t vertex_bytes = vertices_.size() * sizeof(ParticleVertex);
            const auto vertex_span = nw::gfx::allocate_vertex_span(ctx_, static_cast<uint32_t>(vertex_bytes), 16);
            if (!vertex_span.buffer.valid() || !vertex_span.data) {
                continue;
            }
            std::memcpy(vertex_span.data, vertices_.data(), vertex_bytes);
            nw::gfx::cmd_bind_vertex_buffer(cmd, vertex_span.buffer, sizeof(ParticleVertex), vertex_span.offset);
            nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms);
            nw::gfx::cmd_draw(cmd, static_cast<uint32_t>(vertices_.size()), 1);
        } else {
            if (!billboard_vertices_.valid()) {
                continue;
            }
            const auto instance_span = upload_instances(cmd, instances_);
            if (!instance_span.buffer.valid()) {
                continue;
            }
            nw::gfx::cmd_bind_vertex_buffer(cmd, billboard_vertices_, sizeof(ParticleBillboardVertex));
            nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms, instance_span);
            nw::gfx::cmd_draw(cmd, 6, static_cast<uint32_t>(instances_.size()));
        }
    }
}

} // namespace nw::render
