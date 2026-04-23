#include "renderer.hpp"

#include <nw/gfx/native_vulkan.hpp>
#include <nw/log.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace nw::render {

namespace {

constexpr float kProjectileBodySpriteSizeScale = 0.5f;
constexpr float kProjectileBodySpriteTailSizeFloor = 0.35f;

struct FogCompositeVertex {
    glm::vec2 position;
    glm::vec2 texcoord;
};

glm::vec3 linear_to_display_gamma(const glm::vec3& color)
{
    return glm::pow(glm::clamp(color, glm::vec3{0.0f}, glm::vec3{1.0f}), glm::vec3{1.0f / 2.2f});
}

glm::vec4 unpack_rgba8(uint32_t value)
{
    return glm::vec4{
        float(value & 0xffu),
        float((value >> 8u) & 0xffu),
        float((value >> 16u) & 0xffu),
        float((value >> 24u) & 0xffu),
    } * (1.0f / 255.0f);
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

glm::vec3 camera_forward(const RenderContext& ctx)
{
    const glm::vec3 delta = ctx.camera_target - ctx.camera_position;
    if (glm::dot(delta, delta) <= 1.0e-8f) {
        return glm::vec3{0.0f, 1.0f, 0.0f};
    }
    return glm::normalize(delta);
}

void camera_billboard_axes(const RenderContext& ctx, glm::vec3& right, glm::vec3& up)
{
    const auto forward = camera_forward(ctx);
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

void ground_billboard_axes(const RenderContext& ctx, const glm::vec3& position, glm::vec3& right, glm::vec3& up)
{
    plane_aligned_billboard_axes(
        ctx, position, glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f},
        right, up);
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

size_t align_to(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

Renderer::Renderer(nw::gfx::Context* ctx) noexcept
    : ctx_(ctx)
{
}

Renderer::~Renderer()
{
    for (auto& arena : bone_arenas_) {
        arena.destroy();
    }
    for (auto& arena : particle_instance_arenas_) {
        arena.destroy();
    }
    if (fog_render_target_.valid()) nw::gfx::destroy_render_target(ctx_, fog_render_target_);
    if (particle_billboard_vertices_.valid()) nw::gfx::destroy_buffer(particle_billboard_vertices_);
    if (fog_composite_vertices_.valid()) nw::gfx::destroy_buffer(fog_composite_vertices_);
    if (fog_composite_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, fog_composite_pipeline_);
    if (pbr_skinned_transparent_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, pbr_skinned_transparent_pipeline_);
    if (pbr_skinned_cutout_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, pbr_skinned_cutout_pipeline_);
    if (pbr_skinned_opaque_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, pbr_skinned_opaque_pipeline_);
    if (particle_path_transparent_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, particle_path_transparent_pipeline_);
    if (particle_path_additive_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, particle_path_additive_pipeline_);
    if (particle_path_cutout_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, particle_path_cutout_pipeline_);
    if (particle_transparent_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, particle_transparent_pipeline_);
    if (particle_additive_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, particle_additive_pipeline_);
    if (particle_cutout_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, particle_cutout_pipeline_);
    if (pbr_transparent_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, pbr_transparent_pipeline_);
    if (pbr_cutout_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, pbr_cutout_pipeline_);
    if (pbr_opaque_pipeline_.valid()) nw::gfx::destroy_pipeline(ctx_, pbr_opaque_pipeline_);
}

void Renderer::StorageFrameArena::destroy()
{
    for (auto& block : blocks) {
        if (block.buffer.valid()) nw::gfx::destroy_buffer(block.buffer);
    }
    blocks.clear();
}

bool Renderer::StorageFrameArena::reset(nw::gfx::Context* ctx, uint64_t new_frame_id, size_t min_capacity)
{
    constexpr size_t kBoneArenaInitialSize = 4 * 1024 * 1024;

    for (size_t i = 1; i < blocks.size(); ++i) {
        if (blocks[i].buffer.valid()) nw::gfx::destroy_buffer(blocks[i].buffer);
    }
    if (blocks.size() > 1) blocks.resize(1);

    frame_id = new_frame_id;

    const size_t old_cap = blocks.empty() ? 0 : blocks[0].capacity;
    if (old_cap < min_capacity) {
        if (!blocks.empty() && blocks[0].buffer.valid()) nw::gfx::destroy_buffer(blocks[0].buffer);
        blocks.clear();

        const size_t new_cap = std::max(min_capacity, std::max(kBoneArenaInitialSize, old_cap * 2));
        nw::gfx::BufferDesc desc{};
        desc.size = new_cap;
        desc.usage = nw::gfx::BufferUsage::Storage;
        desc.cpu_visible = true;
        auto buf = nw::gfx::create_buffer(ctx, desc);
        if (!buf.valid()) {
            LOG_F(ERROR, "Failed to create bone arena block");
            return false;
        }
        void* mapped = nw::gfx::map_buffer(buf);
        if (!mapped) {
            nw::gfx::destroy_buffer(buf);
            LOG_F(ERROR, "Failed to map bone arena block");
            return false;
        }
        blocks.push_back({buf, mapped, new_cap, 0});
    } else {
        blocks[0].offset = 0;
    }
    return true;
}

nw::gfx::StorageSpan Renderer::StorageFrameArena::allocate(nw::gfx::Context* ctx, uint32_t size, uint32_t alignment)
{
    auto& back = blocks.back();
    const size_t offset = align_to(back.offset, alignment);
    if (offset + size <= back.capacity) {
        back.offset = offset + size;
        return {back.buffer, static_cast<uint32_t>(offset), size};
    }

    const size_t new_cap = std::max<size_t>(size, back.capacity * 2);
    nw::gfx::BufferDesc desc{};
    desc.size = new_cap;
    desc.usage = nw::gfx::BufferUsage::Storage;
    desc.cpu_visible = true;
    auto buf = nw::gfx::create_buffer(ctx, desc);
    if (!buf.valid()) {
        LOG_F(ERROR, "Bone arena overflow: need {} bytes", size);
        return {};
    }
    void* mapped = nw::gfx::map_buffer(buf);
    if (!mapped) {
        nw::gfx::destroy_buffer(buf);
        return {};
    }
    blocks.push_back({buf, mapped, new_cap, size});
    return {buf, 0, size};
}

nw::gfx::StorageSpan Renderer::StorageFrameArena::write(nw::gfx::Context* ctx, const void* data, uint32_t size, uint32_t alignment)
{
    auto span = allocate(ctx, size, alignment);
    if (!span.buffer.valid()) return {};
    std::memcpy(static_cast<uint8_t*>(blocks.back().mapped) + span.offset, data, size);
    return span;
}

nw::gfx::StorageSpan Renderer::upload_bones(nw::gfx::CommandList* cmd, const glm::mat4* bones, uint32_t count)
{
    if (!bones || count == 0) return {};

    nw::gfx::NativeVulkanFrame frame{};
    if (!nw::gfx::get_native_vulkan_frame(ctx_, cmd, frame)) return {};

    const auto idx = std::min<size_t>(frame.frame_index, bone_arenas_.size() - 1);
    auto& arena = bone_arenas_[idx];
    if (arena.frame_id != frame.frame_id) {
        const uint32_t size = count * sizeof(glm::mat4);
        if (!arena.reset(ctx_, frame.frame_id, size)) return {};
    }
    return arena.write(ctx_, bones, count * sizeof(glm::mat4));
}

nw::gfx::StorageSpan Renderer::upload_particle_instances(
    nw::gfx::CommandList* cmd, std::span<const ParticleBillboardInstance> instances)
{
    if (!cmd || instances.empty()) {
        return {};
    }

    nw::gfx::NativeVulkanFrame frame{};
    if (!nw::gfx::get_native_vulkan_frame(ctx_, cmd, frame)) {
        return {};
    }

    const auto idx = std::min<size_t>(frame.frame_index, particle_instance_arenas_.size() - 1);
    auto& arena = particle_instance_arenas_[idx];
    const uint32_t size = static_cast<uint32_t>(instances.size_bytes());
    if (arena.frame_id != frame.frame_id) {
        if (!arena.reset(ctx_, frame.frame_id, size)) {
            return {};
        }
    }

    return arena.write(ctx_, instances.data(), size);
}

nw::gfx::Context* Renderer::context() const noexcept
{
    return ctx_;
}

bool Renderer::initialize(ShaderProvider& shader_provider)
{
    shader_provider_ = &shader_provider;
    if (!ctx_) {
        return false;
    }

    auto vs_fog = shader_provider_->get_shader("render_fog_composite.vs.hlsl");
    auto ps_fog = shader_provider_->get_shader("render_fog_composite.ps.hlsl");
    if (vs_fog.valid() && ps_fog.valid()) {
        nw::gfx::PipelineDesc fog_desc{};
        fog_desc.vs = vs_fog;
        fog_desc.fs = ps_fog;
        fog_desc.uses_bindless_sampled_textures = true;
        fog_desc.uses_single_texture = false;
        fog_desc.depth_test = false;
        fog_desc.depth_write = false;
        fog_desc.blend_mode = nw::gfx::BlendMode::disabled;
        fog_desc.vertex_stride = sizeof(FogCompositeVertex);
        fog_desc.vertex_attributes = {
            {0, offsetof(FogCompositeVertex, position), nw::gfx::VertexFormat::Float2},
            {1, offsetof(FogCompositeVertex, texcoord), nw::gfx::VertexFormat::Float2},
        };
        fog_composite_pipeline_ = nw::gfx::create_pipeline(ctx_, fog_desc);
        if (!fog_composite_pipeline_.valid()) {
            LOG_F(ERROR, "Failed to create fog composite pipeline");
            return false;
        }

        constexpr std::array<FogCompositeVertex, 3> kFullscreenTriangle{{
            {{-1.0f, -1.0f}, {0.0f, 0.0f}},
            {{-1.0f, 3.0f}, {0.0f, 2.0f}},
            {{3.0f, -1.0f}, {2.0f, 0.0f}},
        }};
        nw::gfx::BufferDesc fog_vb_desc{};
        fog_vb_desc.size = sizeof(kFullscreenTriangle);
        fog_vb_desc.usage = nw::gfx::BufferUsage::Vertex;
        fog_vb_desc.cpu_visible = true;
        fog_composite_vertices_ = nw::gfx::create_buffer(ctx_, fog_vb_desc);
        if (!fog_composite_vertices_.valid()) {
            LOG_F(ERROR, "Failed to create fog composite vertex buffer");
            return false;
        }
        auto* mapped = nw::gfx::map_buffer(fog_composite_vertices_);
        if (!mapped) {
            LOG_F(ERROR, "Failed to map fog composite vertex buffer");
            return false;
        }
        std::memcpy(mapped, kFullscreenTriangle.data(), sizeof(kFullscreenTriangle));
        nw::gfx::unmap_buffer(fog_composite_vertices_);
    }

    auto vs_pbr = shader_provider_->get_shader("render_pbr_static.vs.hlsl");
    auto vs_pbr_skinned = shader_provider_->get_shader("render_pbr_skinned.vs.hlsl");
    auto ps_pbr = shader_provider_->get_shader("render_pbr_static.ps.hlsl");
    if (!vs_pbr.valid() || !ps_pbr.valid()) {
        return true;
    }

    nw::gfx::PipelineDesc pbr_desc{};
    pbr_desc.vs = vs_pbr;
    pbr_desc.fs = ps_pbr;
    pbr_desc.uses_bindless_sampled_textures = true;
    pbr_desc.uses_draw_uniforms = true;
    pbr_desc.uses_draw_uniforms2 = true;
    pbr_desc.uses_storage_buffer = false;
    pbr_desc.uses_single_texture = false;
    pbr_desc.depth_test = true;
    pbr_desc.depth_write = true;
    pbr_desc.blend_mode = nw::gfx::BlendMode::disabled;
    pbr_desc.vertex_stride = sizeof(Vertex);
    pbr_desc.vertex_attributes = {
        {0, offsetof(Vertex, position), nw::gfx::VertexFormat::Float3},
        {1, offsetof(Vertex, normal), nw::gfx::VertexFormat::Float3},
        {2, offsetof(Vertex, texcoord), nw::gfx::VertexFormat::Float2},
        {3, offsetof(Vertex, tangent), nw::gfx::VertexFormat::Float4},
    };
    pbr_opaque_pipeline_ = nw::gfx::create_pipeline(ctx_, pbr_desc);
    if (!pbr_opaque_pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create static glTF PBR opaque pipeline");
        return false;
    }
    pbr_cutout_pipeline_ = nw::gfx::create_pipeline(ctx_, pbr_desc);
    if (!pbr_cutout_pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create static glTF PBR cutout pipeline");
        return false;
    }

    auto pbr_transparent_desc = pbr_desc;
    pbr_transparent_desc.depth_write = false;
    pbr_transparent_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
    pbr_transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, pbr_transparent_desc);
    if (!pbr_transparent_pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create static glTF PBR transparent pipeline");
        return false;
    }

    if (vs_pbr_skinned.valid()) {
        auto pbr_skinned_desc = pbr_desc;
        pbr_skinned_desc.vs = vs_pbr_skinned;
        pbr_skinned_desc.uses_storage_buffer = true;
        pbr_skinned_desc.storage_buffer_count = 1;
        pbr_skinned_desc.vertex_stride = sizeof(SkinnedVertex);
        pbr_skinned_desc.vertex_attributes = {
            {0, offsetof(SkinnedVertex, position), nw::gfx::VertexFormat::Float3},
            {1, offsetof(SkinnedVertex, normal), nw::gfx::VertexFormat::Float3},
            {2, offsetof(SkinnedVertex, texcoord), nw::gfx::VertexFormat::Float2},
            {3, offsetof(SkinnedVertex, tangent), nw::gfx::VertexFormat::Float4},
            {4, offsetof(SkinnedVertex, joint_indices), nw::gfx::VertexFormat::UByte4},
            {5, offsetof(SkinnedVertex, joint_weights), nw::gfx::VertexFormat::UByte4Norm},
        };
        pbr_skinned_opaque_pipeline_ = nw::gfx::create_pipeline(ctx_, pbr_skinned_desc);
        if (!pbr_skinned_opaque_pipeline_.valid()) {
            LOG_F(ERROR, "Failed to create skinned glTF PBR opaque pipeline");
            return false;
        }
        pbr_skinned_cutout_pipeline_ = nw::gfx::create_pipeline(ctx_, pbr_skinned_desc);
        if (!pbr_skinned_cutout_pipeline_.valid()) {
            LOG_F(ERROR, "Failed to create skinned glTF PBR cutout pipeline");
            return false;
        }

        auto pbr_skinned_transparent_desc = pbr_skinned_desc;
        pbr_skinned_transparent_desc.depth_write = false;
        pbr_skinned_transparent_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
        pbr_skinned_transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, pbr_skinned_transparent_desc);
        if (!pbr_skinned_transparent_pipeline_.valid()) {
            LOG_F(ERROR, "Failed to create skinned glTF PBR transparent pipeline");
            return false;
        }
    }

    auto vs_particle = shader_provider_->get_shader("render_particle_billboard.vs.hlsl");
    auto vs_particle_path = shader_provider_->get_shader("render_particle_path.vs.hlsl");
    auto ps_particle = shader_provider_->get_shader("render_particle_billboard.ps.hlsl");
    if (vs_particle.valid() && vs_particle_path.valid() && ps_particle.valid()) {
        constexpr std::array<ParticleBillboardVertex, 6> kParticleBillboardQuad{{
            {{-1.0f, -1.0f}, {0.0f, 0.0f}},
            {{1.0f, -1.0f}, {1.0f, 0.0f}},
            {{1.0f, 1.0f}, {1.0f, 1.0f}},
            {{-1.0f, -1.0f}, {0.0f, 0.0f}},
            {{1.0f, 1.0f}, {1.0f, 1.0f}},
            {{-1.0f, 1.0f}, {0.0f, 1.0f}},
        }};
        nw::gfx::BufferDesc particle_billboard_vb_desc{};
        particle_billboard_vb_desc.size = sizeof(kParticleBillboardQuad);
        particle_billboard_vb_desc.usage = nw::gfx::BufferUsage::Vertex;
        particle_billboard_vb_desc.cpu_visible = true;
        particle_billboard_vertices_ = nw::gfx::create_buffer(ctx_, particle_billboard_vb_desc);
        if (!particle_billboard_vertices_.valid()) {
            LOG_F(WARNING, "Failed to create particle billboard vertex buffer");
        } else {
            auto* mapped = nw::gfx::map_buffer(particle_billboard_vertices_);
            if (!mapped) {
                LOG_F(WARNING, "Failed to map particle billboard vertex buffer");
            } else {
                std::memcpy(mapped, kParticleBillboardQuad.data(), sizeof(kParticleBillboardQuad));
                nw::gfx::unmap_buffer(particle_billboard_vertices_);
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
        particle_transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, particle_desc);
        if (!particle_transparent_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create transparent particle pipeline");
        }

        auto particle_additive_desc = particle_desc;
        particle_additive_desc.blend_mode = nw::gfx::BlendMode::additive;
        particle_additive_pipeline_ = nw::gfx::create_pipeline(ctx_, particle_additive_desc);
        if (!particle_additive_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create additive particle pipeline");
        }

        auto particle_cutout_desc = particle_desc;
        particle_cutout_desc.depth_write = true;
        particle_cutout_desc.blend_mode = nw::gfx::BlendMode::disabled;
        particle_cutout_pipeline_ = nw::gfx::create_pipeline(ctx_, particle_cutout_desc);
        if (!particle_cutout_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create cutout particle pipeline");
        }

        auto particle_path_desc = particle_desc;
        particle_path_desc.vs = vs_particle_path;
        particle_path_desc.uses_storage_buffer = false;
        particle_path_desc.storage_buffer_count = 0;
        particle_path_desc.vertex_stride = sizeof(ParticleVertex);
        particle_path_desc.vertex_attributes = {
            {0, offsetof(ParticleVertex, position), nw::gfx::VertexFormat::Float3},
            {1, offsetof(ParticleVertex, texcoord), nw::gfx::VertexFormat::Float2},
            {2, offsetof(ParticleVertex, color), nw::gfx::VertexFormat::Float4},
        };
        particle_path_transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, particle_path_desc);
        if (!particle_path_transparent_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create transparent path particle pipeline");
        }

        auto particle_path_additive_desc = particle_path_desc;
        particle_path_additive_desc.blend_mode = nw::gfx::BlendMode::additive;
        particle_path_additive_pipeline_ = nw::gfx::create_pipeline(ctx_, particle_path_additive_desc);
        if (!particle_path_additive_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create additive path particle pipeline");
        }

        auto particle_path_cutout_desc = particle_path_desc;
        particle_path_cutout_desc.depth_write = true;
        particle_path_cutout_desc.blend_mode = nw::gfx::BlendMode::disabled;
        particle_path_cutout_pipeline_ = nw::gfx::create_pipeline(ctx_, particle_path_cutout_desc);
        if (!particle_path_cutout_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create cutout path particle pipeline");
        }
    }

    return true;
}

void Renderer::render_static_model(nw::gfx::CommandList* cmd, const RenderModel& model,
    const RenderContext& ctx, RenderPassSelection pass, const std::vector<std::vector<glm::mat4>>* skin_matrices)
{
    if (!cmd || model.primitives.empty()) {
        return;
    }

    Lighting lighting = ctx.lighting;
    if (glm::dot(lighting.ambient, lighting.ambient) < 1.0e-6f
        && lighting.key_intensity < 1.0e-4f
        && lighting.fill_intensity < 1.0e-4f
        && lighting.rim_intensity < 1.0e-4f) {
        lighting.key_direction = glm::normalize(glm::vec3{0.45f, -0.6f, 0.68f});
        lighting.key_color = glm::vec3{1.0f, 0.97f, 0.92f};
        lighting.key_intensity = 2.35f;
        lighting.fill_direction = glm::normalize(glm::vec3{-0.55f, -0.1f, 0.42f});
        lighting.fill_color = glm::vec3{0.42f, 0.48f, 0.58f};
        lighting.fill_intensity = 0.14f;
        lighting.rim_direction = glm::normalize(glm::vec3{0.0f, 0.92f, 0.35f});
        lighting.rim_color = glm::vec3{0.72f, 0.76f, 0.84f};
        lighting.rim_intensity = 0.06f;
        lighting.ambient = glm::vec3{0.008f, 0.010f, 0.012f};
    }

    SceneConstants base_sc{};
    base_sc.view = ctx.view;
    base_sc.projection = ctx.projection;
    base_sc.camera_pos = glm::vec4(ctx.camera_position, 0.0f);

    auto forward = glm::normalize(ctx.camera_target - ctx.camera_position);
    auto right = glm::cross(forward, glm::vec3{0.0f, 0.0f, 1.0f});
    if (glm::length(right) < 0.0001f) {
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    } else {
        right = glm::normalize(right);
    }
    auto up = glm::normalize(glm::cross(right, forward));

    auto camera_relative_light = [&](const glm::vec3& dir) {
        auto world_l = glm::normalize(right * dir.x + (-forward) * dir.y + up * dir.z);
        return -world_l;
    };

    auto world_space_light = [](const glm::vec3& dir) {
        if (glm::dot(dir, dir) < 1.0e-8f) {
            return glm::vec3{0.0f, 0.0f, 1.0f};
        }
        return -glm::normalize(dir);
    };

    const auto encode_light = [&](const glm::vec3& dir) {
        return ctx.lighting_space == LightingSpace::world_space
            ? world_space_light(dir)
            : camera_relative_light(dir);
    };

    base_sc.ambient = glm::vec4(lighting.ambient, 0.0f);
    base_sc.key_dir_intensity = glm::vec4(encode_light(lighting.key_direction), lighting.key_intensity);
    base_sc.key_color = glm::vec4(lighting.key_color, 0.0f);
    base_sc.fill_dir_intensity = glm::vec4(encode_light(lighting.fill_direction), lighting.fill_intensity);
    base_sc.fill_color = glm::vec4(lighting.fill_color, 0.0f);
    base_sc.rim_dir_intensity = glm::vec4(encode_light(lighting.rim_direction), lighting.rim_intensity);
    base_sc.rim_color = glm::vec4(lighting.rim_color, 0.0f);
    base_sc.fog_color = glm::vec4(ctx.fog.color, 0.0f);
    base_sc.fog_range = glm::vec2(ctx.fog.start_distance, ctx.fog.end_distance);
    base_sc.fog_amount = ctx.fog.amount;
    base_sc.fog_enabled = ctx.fog.enabled ? 1u : 0u;
    base_sc.ibl_diffuse_texture_index = ctx.static_pbr_ibl_enabled ? ctx.static_pbr_ibl_diffuse_texture_index : 0u;
    base_sc.ibl_specular_texture_index = ctx.static_pbr_ibl_enabled ? ctx.static_pbr_ibl_specular_texture_index : 0u;
    base_sc.ibl_brdf_lut_texture_index = ctx.static_pbr_ibl_enabled ? ctx.static_pbr_brdf_lut_texture_index : 0u;

    constexpr size_t kMaxBones = 64;
    for (const auto& prim : model.primitives) {
        if (prim.material >= model.materials.size()) {
            continue;
        }
        const auto& material = model.materials[prim.material];
        const bool is_transparent = material.alpha_mode == MaterialMode::transparent;
        if ((pass == RenderPassSelection::opaque_cutout && is_transparent)
            || (pass == RenderPassSelection::transparent && !is_transparent)) {
            continue;
        }

        auto pipeline = is_transparent                        ? pbr_transparent_pipeline_
            : material.alpha_mode == MaterialMode::cutout     ? pbr_cutout_pipeline_
                                                              : pbr_opaque_pipeline_;
        uint32_t vertex_stride = sizeof(Vertex);
        nw::gfx::StorageSpan bone_span{};
        if (prim.skinned) {
            pipeline = is_transparent                            ? pbr_skinned_transparent_pipeline_
                : material.alpha_mode == MaterialMode::cutout    ? pbr_skinned_cutout_pipeline_
                                                                 : pbr_skinned_opaque_pipeline_;
            vertex_stride = sizeof(SkinnedVertex);
            if (!pipeline.valid() || prim.skin >= model.skins.size() || prim.node < 0 || static_cast<size_t>(prim.node) >= model.nodes.size()) {
                continue;
            }

            std::array<glm::mat4, kMaxBones> bones{};
            for (auto& bone : bones) {
                bone = glm::mat4(1.0f);
            }
            const glm::mat4 inverse_mesh = glm::inverse(model.nodes[prim.node].world_transform);
            if (skin_matrices && prim.skin < skin_matrices->size()) {
                const auto& src = (*skin_matrices)[prim.skin];
                for (size_t i = 0; i < src.size() && i < bones.size(); ++i) {
                    bones[i] = inverse_mesh * src[i];
                }
            } else {
                const auto& skin = model.skins[prim.skin];
                for (size_t i = 0; i < skin.joints.size() && i < bones.size(); ++i) {
                    const int32_t joint = skin.joints[i];
                    if (joint < 0 || static_cast<size_t>(joint) >= model.nodes.size() || i >= skin.inverse_bind_matrices.size()) {
                        continue;
                    }
                    bones[i] = inverse_mesh * model.nodes[joint].world_transform * skin.inverse_bind_matrices[i];
                }
            }
            const uint32_t bone_count = skin_matrices && prim.skin < skin_matrices->size()
                ? static_cast<uint32_t>(std::min<size_t>((*skin_matrices)[prim.skin].size(), bones.size()))
                : static_cast<uint32_t>(std::min<size_t>(model.skins[prim.skin].joints.size(), bones.size()));
            bone_span = upload_bones(cmd, bones.data(), bone_count);
            if (!bone_span.buffer.valid()) {
                continue;
            }
        }
        if (!pipeline.valid()) {
            continue;
        }

        SceneConstants sc = base_sc;
        sc.model = prim.transform;
        const glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(prim.transform)));
        sc.normal_matrix = glm::mat4(nm);

        SurfaceConstants surf{};
        surf.albedo = material.albedo;
        surf.roughness = material.roughness;
        surf.metallic = material.metallic;
        surf.normal_scale = material.normal_scale;
        surf.occlusion_strength = material.occlusion_strength;
        surf.ibl_strength = ctx.static_pbr_ibl_strength * material.ibl_strength;
        surf.exposure = ctx.static_pbr_exposure * material.exposure;
        surf.emissive = glm::vec4(material.emissive, 0.0f);
        surf.albedo_index = material.albedo_index;
        surf.normal_index = material.normal_index;
        surf.surface_index = material.surface_index;
        surf.emissive_index = material.emissive_index;
        surf.alpha_mode = static_cast<uint32_t>(material.alpha_mode);
        surf.alpha_cutoff = material.alpha_cutoff;
        surf.double_sided = material.double_sided ? 1u : 0u;

        auto scene_uniforms = nw::gfx::allocate_uniform_span(ctx_, sizeof(SceneConstants));
        auto surface_uniforms = nw::gfx::allocate_uniform_span(ctx_, sizeof(SurfaceConstants));
        if (!scene_uniforms.data || !surface_uniforms.data) {
            continue;
        }

        std::memcpy(scene_uniforms.data, &sc, sizeof(sc));
        std::memcpy(surface_uniforms.data, &surf, sizeof(surf));

        nw::gfx::cmd_bind_pipeline(cmd, pipeline);
        nw::gfx::cmd_bind_vertex_buffer(cmd, prim.vertices, vertex_stride);
        nw::gfx::cmd_bind_index_buffer(cmd, prim.indices, prim.index_stride);
        nw::gfx::cmd_bind_resources(cmd, pipeline, scene_uniforms, bone_span, {}, surface_uniforms);
        nw::gfx::cmd_draw_indexed(cmd, prim.index_count);
    }
}

void Renderer::render_particles(nw::gfx::CommandList* cmd, const ParticleSystemInstance& system,
    std::span<const ParticleMaterialDef> source_materials, const RenderContext& ctx,
    ParticleResourceProvider& resources)
{
    if (!cmd || (!particle_transparent_pipeline_.valid() && !particle_cutout_pipeline_.valid())) {
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
        const auto* source_image = texture_name ? resources.get_source_image(texture_name) : nullptr;
        const bool additive_alpha_mask_billboard = packet.blend == ParticleBlendMode::additive
            && source_image
            && is_white_alpha_mask_texture(*source_image);

        const float cols = std::max<uint16_t>(1, packet.sheet.columns);
        const float rows = std::max<uint16_t>(1, packet.sheet.rows);
        const bool uses_path_geometry = packet.mode == ParticleRenderMode::linked_chain
            || packet.mode == ParticleRenderMode::beam;
        particle_vertices_.clear();
        particle_instances_.clear();
        if (uses_path_geometry) {
            particle_vertices_.reserve(std::max<size_t>(static_cast<size_t>(packet.count) * 6, 96));
        } else {
            particle_instances_.reserve(std::max<size_t>(packet.count, 16u));
        }
        particle_order_.clear();
        particle_order_.reserve(packet.count);
        for (uint32_t n = 0; n < packet.count; ++n) {
            const size_t i = packet.begin + n;
            if (i >= core.position.size()) {
                break;
            }
            particle_order_.push_back(static_cast<uint32_t>(i));
        }
        if (packet.sort_back_to_front && packet.mode != ParticleRenderMode::linked_chain) {
            std::sort(particle_order_.begin(), particle_order_.end(),
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
            particle_chain_order_ = particle_order_;
            if (packet.mode == ParticleRenderMode::linked_chain) {
                particle_chain_order_.erase(
                    std::remove_if(particle_chain_order_.begin(), particle_chain_order_.end(),
                        [&](uint32_t idx) { return idx >= core.position.size(); }),
                    particle_chain_order_.end());
            }

            auto emit_ribbon_segment = [&](const glm::vec3& a, const glm::vec3& b, float half_width,
                                           const glm::vec4& color_a, const glm::vec4& color_b,
                                           float u0, float u1, float v0, float v1) {
                const glm::vec3 segment_right = ribbon_right_axis(ctx, a, b, camera_right) * std::max(half_width, 0.001f);
                const glm::vec3 p0 = a - segment_right;
                const glm::vec3 p1 = a + segment_right;
                const glm::vec3 p2 = b + segment_right;
                const glm::vec3 p3 = b - segment_right;
                particle_vertices_.push_back(ParticleVertex{.position = p0, .texcoord = {u0, v0}, .color = color_a});
                particle_vertices_.push_back(ParticleVertex{.position = p1, .texcoord = {u0, v1}, .color = color_a});
                particle_vertices_.push_back(ParticleVertex{.position = p2, .texcoord = {u1, v1}, .color = color_b});
                particle_vertices_.push_back(ParticleVertex{.position = p0, .texcoord = {u0, v0}, .color = color_a});
                particle_vertices_.push_back(ParticleVertex{.position = p2, .texcoord = {u1, v1}, .color = color_b});
                particle_vertices_.push_back(ParticleVertex{.position = p3, .texcoord = {u1, v0}, .color = color_b});
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
                particle_vertices_.push_back(ParticleVertex{.position = p0, .texcoord = {u0, v0}, .color = color_a});
                particle_vertices_.push_back(ParticleVertex{.position = p1, .texcoord = {u0, v1}, .color = color_b});
                particle_vertices_.push_back(ParticleVertex{.position = p2, .texcoord = {u1, v1}, .color = color_b});
                particle_vertices_.push_back(ParticleVertex{.position = p0, .texcoord = {u0, v0}, .color = color_a});
                particle_vertices_.push_back(ParticleVertex{.position = p2, .texcoord = {u1, v1}, .color = color_b});
                particle_vertices_.push_back(ParticleVertex{.position = p3, .texcoord = {u1, v0}, .color = color_a});
            };

            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 1.0f / cols;
            float v1 = 1.0f / rows;
            if (!particle_chain_order_.empty()) {
                const size_t i = particle_chain_order_.front();
                const uint16_t frame = core.frame[i];
                const uint16_t frame_begin = packet.sheet.frame_begin;
                const uint16_t frame_index = frame >= frame_begin ? static_cast<uint16_t>(frame - frame_begin) : 0;
                const float frame_col = std::fmod(static_cast<float>(frame_index), cols);
                const float frame_row = std::floor(static_cast<float>(frame_index) / cols);
                u0 = frame_col / cols;
                v0 = frame_row / rows;
                u1 = (frame_col + 1.0f) / cols;
                v1 = (frame_row + 1.0f) / rows;
                if (source_image && source_image->valid() && source_image->width() > 1 && source_image->height() > 1
                    && (cols > 1.0f || rows > 1.0f)) {
                    const float inset_u = 0.5f / static_cast<float>(source_image->width());
                    const float inset_v = 0.5f / static_cast<float>(source_image->height());
                    if ((u1 - u0) > inset_u * 2.0f && (v1 - v0) > inset_v * 2.0f) {
                        u0 += inset_u;
                        v0 += inset_v;
                        u1 -= inset_u;
                        v1 -= inset_v;
                    }
                }
            }

            if (packet.path.kind == ParticleRenderPathKind::beam) {
                const uint32_t segment_count = std::max<uint32_t>(1u, packet.path.subdivisions);
                const glm::vec4 color = !particle_order_.empty()
                    ? unpack_rgba8(core.color_rgba8[particle_order_.front()])
                    : glm::vec4{1.0f};
                const float half_width = !particle_order_.empty()
                    ? 0.22f * std::max(core.size_x[particle_order_.front()], 0.001f)
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
                for (uint32_t idx : particle_chain_order_) {
                    if (idx >= core.position.size()
                        || idx >= system.particles.bezier.source_position.size()
                        || idx >= system.particles.bezier.target_position.size()) {
                        continue;
                    }

                    const glm::vec3 source = system.particles.bezier.source_position[idx];
                    const glm::vec3 middle = core.position[idx];
                    const glm::vec3 target = system.particles.bezier.target_position[idx];
                    const glm::vec4 color = unpack_rgba8(core.color_rgba8[idx]);
                    const float half_width = 0.5f * std::max(core.size_x[idx], 0.001f);
                    const glm::vec3 rv = ribbon_right_axis(ctx, source, target, camera_right)
                        * std::max(half_width, 0.001f);
                    const float len0 = glm::distance(source, middle);
                    const float len1 = glm::distance(middle, target);
                    const float total_len = std::max(1.0e-5f, len0 + len1);
                    const float vm = v0 + (v1 - v0) * (len0 / total_len);

                    if (len0 > 1.0e-5f) {
                        particle_vertices_.push_back(ParticleVertex{.position = source - rv, .texcoord = {u0, v0}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = source + rv, .texcoord = {u1, v0}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = middle + rv, .texcoord = {u1, vm}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = source - rv, .texcoord = {u0, v0}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = middle + rv, .texcoord = {u1, vm}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = middle - rv, .texcoord = {u0, vm}, .color = color});
                    }

                    if (len1 > 1.0e-5f) {
                        particle_vertices_.push_back(ParticleVertex{.position = middle - rv, .texcoord = {u0, vm}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = middle + rv, .texcoord = {u1, vm}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = target + rv, .texcoord = {u1, v1}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = middle - rv, .texcoord = {u0, vm}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = target + rv, .texcoord = {u1, v1}, .color = color});
                        particle_vertices_.push_back(ParticleVertex{.position = target - rv, .texcoord = {u0, v1}, .color = color});
                    }
                }
            } else if (particle_chain_order_.size() >= 2) {
                for (size_t i = 1; i < particle_chain_order_.size(); ++i) {
                    const size_t a_idx = particle_chain_order_[i - 1];
                    const size_t b_idx = particle_chain_order_[i];
                    const glm::vec4 color_a = unpack_rgba8(core.color_rgba8[a_idx]);
                    const glm::vec4 color_b = unpack_rgba8(core.color_rgba8[b_idx]);
                    const float hw = 0.5f * std::max(0.5f * (core.size_x[a_idx] + core.size_x[b_idx]), 0.001f);
                    const float seg_u0 = std::lerp(u0, u1, static_cast<float>(i - 1) / static_cast<float>(particle_chain_order_.size() - 1));
                    const float seg_u1 = std::lerp(u0, u1, static_cast<float>(i) / static_cast<float>(particle_chain_order_.size() - 1));
                    emit_ribbon_segment(core.position[a_idx], core.position[b_idx], hw, color_a, color_b, seg_u0, seg_u1, v0, v1);
                }
            }
        }

        if (packet.mode != ParticleRenderMode::linked_chain
            && packet.mode != ParticleRenderMode::beam) {
            glm::vec4 uniform_color{1.0f};
            if (packet.uniform_color && !particle_order_.empty()) {
                uniform_color = unpack_rgba8(core.color_rgba8[particle_order_.front()]);
            }
            for (uint32_t ordered_index = 0; ordered_index < particle_order_.size(); ++ordered_index) {
                const size_t i = particle_order_[ordered_index];

                const glm::vec3 position = core.position[i];
                const float emitter_scale = particle_emitter_uniform_scale(system, core.emitter_id[i]);
                const bool projectile_body_sprite = packet.semantic == ParticleRenderSemantic::projectile_body_sprite;
                glm::vec3 quad_right = camera_right;
                glm::vec3 quad_up = camera_up;
                if (packet.mode == ParticleRenderMode::billboard_world_z) {
                    ground_billboard_axes(ctx, position, quad_right, quad_up);
                } else if (packet.mode == ParticleRenderMode::aligned_world_z) {
                    axis_billboard_axes(ctx, position, glm::vec3{0.0f, 0.0f, 1.0f}, quad_right, quad_up);
                }

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

                const float rotation = packet.uniform_rotation && !particle_order_.empty()
                    ? core.rotation[particle_order_.front()]
                    : core.rotation[i];
                const float angle = rotation * 2.0f * 3.14159265358979323846f;
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                const auto rotate = [&](float x, float y) {
                    return quad_right * (x * c - y * s) + quad_up * (x * s + y * c);
                };

                const glm::vec3 rotated_right = rotate(sx, 0.0f);
                const glm::vec3 rotated_up = rotate(0.0f, sy);

                const uint16_t frame = packet.uniform_frame && !particle_order_.empty()
                    ? core.frame[particle_order_.front()]
                    : core.frame[i];
                const uint16_t frame_begin = packet.sheet.frame_begin;
                const uint16_t frame_index = frame >= frame_begin ? static_cast<uint16_t>(frame - frame_begin) : 0;
                float u0 = std::fmod(static_cast<float>(frame_index), cols) / cols;
                float v0 = std::floor(static_cast<float>(frame_index) / cols) / rows;
                float u1 = u0 + 1.0f / cols;
                float v1 = v0 + 1.0f / rows;
                if (source_image && source_image->valid() && source_image->width() > 1 && source_image->height() > 1
                    && (cols > 1.0f || rows > 1.0f)) {
                    const float inset_u = 0.5f / static_cast<float>(source_image->width());
                    const float inset_v = 0.5f / static_cast<float>(source_image->height());
                    if ((u1 - u0) > inset_u * 2.0f && (v1 - v0) > inset_v * 2.0f) {
                        u0 += inset_u;
                        v0 += inset_v;
                        u1 -= inset_u;
                        v1 -= inset_v;
                    }
                }
                glm::vec4 color = packet.uniform_color
                    ? uniform_color
                    : unpack_rgba8(core.color_rgba8[i]);
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

                particle_instances_.push_back(ParticleBillboardInstance{
                    .center = glm::vec4(position, 1.0f),
                    .right = glm::vec4(rotated_right, 0.0f),
                    .up = glm::vec4(rotated_up, 0.0f),
                    .texcoord_rect = glm::vec4(u0, v0, u1, v1),
                    .color = color,
                });
            }
        }
        if (uses_path_geometry ? particle_vertices_.empty() : particle_instances_.empty()) {
            continue;
        }

        auto pipeline = uses_path_geometry ? particle_path_transparent_pipeline_ : particle_transparent_pipeline_;
        if (packet.blend == ParticleBlendMode::cutout) {
            pipeline = uses_path_geometry ? particle_path_cutout_pipeline_ : particle_cutout_pipeline_;
        } else if (packet.blend == ParticleBlendMode::additive) {
            pipeline = uses_path_geometry ? particle_path_additive_pipeline_ : particle_additive_pipeline_;
        }
        if (!pipeline.valid()) {
            LOG_F(WARNING, "particle pipeline invalid: blend={} mode={} — packet skipped",
                static_cast<uint32_t>(packet.blend), static_cast<uint32_t>(packet.mode));
            continue;
        }

        ParticleDrawConstants constants{
            .view = ctx.view,
            .projection = ctx.projection,
            .texture_index = nw::gfx::get_bindless_texture_index(ctx_, texture),
            .alpha_cutout = packet.blend == ParticleBlendMode::cutout ? 1u : 0u,
            .additive_like = packet.blend == ParticleBlendMode::additive ? 1u : 0u,
            .additive_alpha_gated = packet.blend == ParticleBlendMode::additive
                && (additive_alpha_mask_billboard
                    || (packet.mode != ParticleRenderMode::billboard
                        && packet.mode != ParticleRenderMode::billboard_local_z
                        && packet.mode != ParticleRenderMode::billboard_world_z
                        && packet.mode != ParticleRenderMode::aligned_world_z)) ? 1u : 0u,
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
            const size_t vertex_bytes = particle_vertices_.size() * sizeof(ParticleVertex);
            const auto vertex_span = nw::gfx::allocate_vertex_span(ctx_, static_cast<uint32_t>(vertex_bytes), 16);
            if (!vertex_span.buffer.valid() || !vertex_span.data) {
                continue;
            }
            std::memcpy(vertex_span.data, particle_vertices_.data(), vertex_bytes);
            nw::gfx::cmd_bind_vertex_buffer(cmd, vertex_span.buffer, sizeof(ParticleVertex), vertex_span.offset);
            nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms);
            nw::gfx::cmd_draw(cmd, static_cast<uint32_t>(particle_vertices_.size()), 1);
        } else {
            if (!particle_billboard_vertices_.valid()) {
                continue;
            }
            const auto instance_span = upload_particle_instances(cmd, particle_instances_);
            if (!instance_span.buffer.valid()) {
                continue;
            }
            nw::gfx::cmd_bind_vertex_buffer(cmd, particle_billboard_vertices_, sizeof(ParticleBillboardVertex));
            nw::gfx::cmd_bind_resources(cmd, pipeline, uniforms, instance_span);
            nw::gfx::cmd_draw(cmd, 6, static_cast<uint32_t>(particle_instances_.size()));
        }
    }
}

bool Renderer::ensure_fog_resources(uint32_t width, uint32_t height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (fog_render_target_.valid() && fog_target_width_ == width && fog_target_height_ == height) {
        return true;
    }

    if (fog_render_target_.valid()) {
        nw::gfx::destroy_render_target(ctx_, fog_render_target_);
        fog_render_target_ = {};
        fog_color_texture_ = {};
        fog_depth_texture_ = {};
    }

    nw::gfx::TextureDesc color_desc{};
    color_desc.width = width;
    color_desc.height = height;
    color_desc.format = nw::gfx::Fmt::RGBA16F;
    color_desc.sampled = true;
    color_desc.render_target = true;
    fog_color_texture_ = nw::gfx::create_texture(ctx_, color_desc);
    if (!fog_color_texture_.valid()) {
        LOG_F(ERROR, "Failed to create fog offscreen color target");
        return false;
    }

    nw::gfx::TextureDesc depth_desc{};
    depth_desc.width = width;
    depth_desc.height = height;
    depth_desc.format = nw::gfx::Fmt::D32F;
    depth_desc.sampled = true;
    depth_desc.render_target = true;
    fog_depth_texture_ = nw::gfx::create_texture(ctx_, depth_desc);
    if (!fog_depth_texture_.valid()) {
        LOG_F(ERROR, "Failed to create fog offscreen depth target");
        nw::gfx::destroy_texture(ctx_, fog_color_texture_);
        fog_color_texture_ = {};
        return false;
    }

    nw::gfx::RenderTargetDesc rt_desc{};
    rt_desc.color[0].texture = fog_color_texture_;
    rt_desc.depth.texture = fog_depth_texture_;
    fog_render_target_ = nw::gfx::create_render_target(ctx_, rt_desc);
    if (!fog_render_target_.valid()) {
        LOG_F(ERROR, "Failed to create fog offscreen render target");
        nw::gfx::destroy_texture(ctx_, fog_depth_texture_);
        nw::gfx::destroy_texture(ctx_, fog_color_texture_);
        fog_depth_texture_ = {};
        fog_color_texture_ = {};
        return false;
    }

    fog_target_width_ = width;
    fog_target_height_ = height;
    return true;
}

void Renderer::render_fog_composite(nw::gfx::CommandList* cmd, const RenderContext& ctx, const SceneFog& fog)
{
    if (!cmd || !fog.enabled || !fog_composite_pipeline_.valid() || !fog_composite_vertices_.valid()
        || !fog_color_texture_.valid() || !fog_depth_texture_.valid()) {
        return;
    }

    const auto color_index = nw::gfx::get_bindless_texture_index(ctx_, fog_color_texture_);
    const auto depth_index = nw::gfx::get_bindless_texture_index(ctx_, fog_depth_texture_);
    if (color_index == 0 || depth_index == 0) {
        return;
    }

    auto uniforms = nw::gfx::allocate_uniform_span(ctx_, sizeof(FogCompositeConstants));
    if (!uniforms.data) {
        return;
    }

    FogCompositeConstants constants{};
    constants.color_texture_index = color_index;
    constants.depth_texture_index = depth_index;
    constants.fog_amount = fog.amount;
    constants.fog_color = glm::vec4(linear_to_display_gamma(fog.color), 0.0f);
    constants.fog_range = glm::vec2(fog.start_distance, fog.end_distance);
    constants.camera_clip_planes = glm::vec2(ctx.camera_near_plane, ctx.camera_far_plane);
    std::memcpy(uniforms.data, &constants, sizeof(constants));

    nw::gfx::cmd_bind_pipeline(cmd, fog_composite_pipeline_);
    nw::gfx::cmd_bind_vertex_buffer(cmd, fog_composite_vertices_, sizeof(FogCompositeVertex));
    nw::gfx::cmd_bind_resources(cmd, fog_composite_pipeline_, uniforms);
    nw::gfx::cmd_draw(cmd, 3);
}

} // namespace nw::render
