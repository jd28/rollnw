#include "model_gpu_backend.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/gfx/native_vulkan.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <algorithm>
#include <array>
#include <cstring>

namespace nw::render::nwn {

namespace {

constexpr size_t kBoneArenaInitialSize = 4 * 1024 * 1024;

size_t align_to(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

nw::gfx::Handle<nw::gfx::Texture> create_solid_texture(nw::gfx::Context* ctx, nw::gfx::Fmt format,
    std::array<uint8_t, 4> pixel)
{
    nw::gfx::TextureDesc desc{};
    desc.width = 1;
    desc.height = 1;
    desc.format = format;
    auto texture = nw::gfx::create_texture(ctx, desc);
    if (texture.valid()) {
        nw::gfx::upload_texture_rgba8(ctx, texture, pixel.data(), pixel.size());
    }
    return texture;
}

uint32_t pack_rgba8(const uint8_t* src, uint32_t channels)
{
    const uint32_t r = channels > 0 ? src[0] : 0;
    const uint32_t g = channels > 1 ? src[1] : 0;
    const uint32_t b = channels > 2 ? src[2] : 0;
    const uint32_t a = channels > 3 ? src[3] : 255;
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

bool create_plt_palette_buffer(nw::gfx::Context* ctx, nw::gfx::Handle<nw::gfx::Buffer>& out)
{
    struct LayerHeader {
        uint32_t offset = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t _pad = 0;
    };

    std::array<LayerHeader, nw::plt_layer_size> headers{};
    std::vector<uint32_t> pixels;

    for (uint32_t layer = 0; layer < nw::plt_layer_size; ++layer) {
        auto* img = nw::kernel::resman().palette_texture(static_cast<nw::PltLayer>(layer));
        if (!img || !img->valid() || img->channels() < 3) {
            LOG_F(ERROR, "Failed to load palette texture for PLT layer {}", layer);
            return false;
        }

        headers[layer].offset = static_cast<uint32_t>(pixels.size());
        headers[layer].width = img->width();
        headers[layer].height = img->height();

        const uint8_t* src = img->data();
        const uint32_t channels = img->channels();
        const size_t pixel_count = static_cast<size_t>(img->width()) * img->height();
        pixels.reserve(pixels.size() + pixel_count);
        for (size_t i = 0; i < pixel_count; ++i) {
            pixels.push_back(pack_rgba8(src + (i * channels), channels));
        }
    }

    std::vector<uint32_t> buffer_data;
    buffer_data.reserve(headers.size() * 4 + pixels.size());
    for (const auto& header : headers) {
        buffer_data.push_back(header.offset);
        buffer_data.push_back(header.width);
        buffer_data.push_back(header.height);
        buffer_data.push_back(header._pad);
    }
    buffer_data.insert(buffer_data.end(), pixels.begin(), pixels.end());

    nw::gfx::BufferDesc desc{};
    desc.size = buffer_data.size() * sizeof(uint32_t);
    desc.usage = nw::gfx::BufferUsage::Storage;
    desc.cpu_visible = true;
    out = nw::gfx::create_buffer(ctx, desc);
    if (!out.valid()) {
        LOG_F(ERROR, "Failed to create PLT palette storage buffer");
        return false;
    }

    void* mapped = nw::gfx::map_buffer(out);
    if (!mapped) {
        LOG_F(ERROR, "Failed to map PLT palette storage buffer");
        nw::gfx::destroy_buffer(out);
        out = {};
        return false;
    }

    std::memcpy(mapped, buffer_data.data(), desc.size);
    nw::gfx::unmap_buffer(out);
    return true;
}

} // namespace

ModelGpuBackend::~ModelGpuBackend()
{
    for (auto& shadow_rt : shadow_render_targets_) {
        if (shadow_rt.valid()) { nw::gfx::destroy_render_target(ctx_, shadow_rt); }
    }
    for (auto& arena : bone_arenas_) {
        arena.destroy();
    }
    if (plt_palette_buffer_.valid()) { nw::gfx::destroy_buffer(plt_palette_buffer_); }
    if (skinned_shadow_cutout_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, skinned_shadow_cutout_pipeline_); }
    if (skinned_shadow_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, skinned_shadow_pipeline_); }
    if (skinned_offscreen_transparent_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, skinned_offscreen_transparent_pipeline_); }
    if (skinned_transparent_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, skinned_transparent_pipeline_); }
    if (skinned_offscreen_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, skinned_offscreen_pipeline_); }
    if (skinned_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, skinned_pipeline_); }
    if (static_shadow_cutout_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, static_shadow_cutout_pipeline_); }
    if (static_shadow_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, static_shadow_pipeline_); }
    if (static_offscreen_transparent_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, static_offscreen_transparent_pipeline_); }
    if (static_transparent_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, static_transparent_pipeline_); }
    if (static_offscreen_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, static_offscreen_pipeline_); }
    if (static_pipeline_.valid()) { nw::gfx::destroy_pipeline(ctx_, static_pipeline_); }
    if (fallback_texture_.valid()) { nw::gfx::destroy_texture(ctx_, fallback_texture_); }
    if (fallback_normal_.valid()) { nw::gfx::destroy_texture(ctx_, fallback_normal_); }
    if (fallback_surface_.valid()) { nw::gfx::destroy_texture(ctx_, fallback_surface_); }
    if (fallback_emissive_.valid()) { nw::gfx::destroy_texture(ctx_, fallback_emissive_); }
}

void ModelGpuBackend::BoneFrameArena::destroy()
{
    for (auto& block : blocks) {
        if (block.buffer.valid()) nw::gfx::destroy_buffer(block.buffer);
    }
    blocks.clear();
}

bool ModelGpuBackend::BoneFrameArena::reset(nw::gfx::Context* ctx, uint64_t new_frame_id, size_t min_capacity)
{
    frame_id = new_frame_id;
    for (auto& block : blocks) {
        block.offset = 0;
    }

    auto largest = std::max_element(blocks.begin(), blocks.end(),
        [](const BoneBlock& lhs, const BoneBlock& rhs) {
            return lhs.capacity < rhs.capacity;
        });
    if (largest != blocks.end()) {
        std::iter_swap(largest, blocks.end() - 1);
    }

    const size_t old_cap = blocks.empty() ? 0 : blocks.back().capacity;
    if (old_cap < min_capacity) {
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
    }
    return true;
}

nw::gfx::StorageSpan ModelGpuBackend::BoneFrameArena::allocate(nw::gfx::Context* ctx, uint32_t size, uint32_t alignment)
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

nw::gfx::StorageSpan ModelGpuBackend::BoneFrameArena::write(nw::gfx::Context* ctx, const void* data, uint32_t size, uint32_t alignment)
{
    auto span = allocate(ctx, size, alignment);
    if (!span.buffer.valid()) return {};
    std::memcpy(static_cast<uint8_t*>(blocks.back().mapped) + span.offset, data, size);
    return span;
}

bool ModelGpuBackend::initialize(nw::render::ShaderProvider& shader_provider)
{
    if (!create_plt_palette_buffer(ctx_, plt_palette_buffer_)) {
        return false;
    }

    auto vs_static = shader_provider.get_shader("render_static_mesh.vs.hlsl");
    auto vs_static_shadow = shader_provider.get_shader("render_shadow_static.vs.hlsl");
    auto ps_shadow = shader_provider.get_shader("render_shadow.ps.hlsl");
    auto ps = shader_provider.get_shader("render_pbr.ps.hlsl");
    if (!vs_static.valid() || !ps.valid() || !vs_static_shadow.valid() || !ps_shadow.valid()) {
        LOG_F(ERROR, "Failed to load static shaders");
        return false;
    }

    nw::gfx::PipelineDesc static_desc{};
    static_desc.vs = vs_static;
    static_desc.fs = ps;
    static_desc.uses_bindless_sampled_textures = true;
    static_desc.uses_draw_uniforms2 = true;
    static_desc.uses_storage_buffer = true;
    static_desc.storage_buffer_count = 1;
    static_desc.uses_single_texture = false;
    static_desc.depth_test = true;
    static_desc.depth_write = true;
    static_desc.blend_mode = nw::gfx::BlendMode::disabled;
    static_desc.vertex_stride = sizeof(Vertex);
    static_desc.vertex_attributes = {
        {0, offsetof(Vertex, position), nw::gfx::VertexFormat::Float3},
        {1, offsetof(Vertex, normal), nw::gfx::VertexFormat::Float3},
        {2, offsetof(Vertex, texcoord), nw::gfx::VertexFormat::Float2},
    };
    static_pipeline_ = nw::gfx::create_pipeline(ctx_, static_desc);
    if (!static_pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create static mesh pipeline");
        return false;
    }

    auto static_shadow_desc = static_desc;
    static_shadow_desc.vs = vs_static_shadow;
    static_shadow_desc.fs = ps_shadow;
    static_shadow_desc.uses_draw_uniforms2 = false;
    static_shadow_desc.uses_storage_buffer = false;
    static_shadow_desc.storage_buffer_count = 0;
    static_shadow_desc.has_color_attachment = false;
    static_shadow_desc.color_write = false;
    static_shadow_desc.blend_mode = nw::gfx::BlendMode::disabled;
    static_shadow_desc.depth_test = true;
    static_shadow_desc.depth_write = true;
    static_shadow_desc.use_swapchain_color_format = false;
    static_shadow_desc.depth_attachment_format = nw::gfx::Fmt::D32F;
    static_shadow_pipeline_ = nw::gfx::create_pipeline(ctx_, static_shadow_desc);
    if (!static_shadow_pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create static shadow pipeline");
        return false;
    }

    auto static_shadow_cutout_desc = static_shadow_desc;
    static_shadow_cutout_desc.uses_bindless_sampled_textures = true;
    static_shadow_cutout_desc.uses_single_texture = false;
    static_shadow_cutout_pipeline_ = nw::gfx::create_pipeline(ctx_, static_shadow_cutout_desc);
    if (!static_shadow_cutout_pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create static cutout shadow pipeline");
        return false;
    }

    auto static_offscreen_desc = static_desc;
    static_offscreen_desc.use_swapchain_color_format = false;
    static_offscreen_desc.color_attachment_format = nw::gfx::Fmt::RGBA16F;
    static_offscreen_desc.depth_attachment_format = nw::gfx::Fmt::D32F;
    static_offscreen_pipeline_ = nw::gfx::create_pipeline(ctx_, static_offscreen_desc);
    if (!static_offscreen_pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create offscreen static mesh pipeline");
        return false;
    }

    auto static_offscreen_transparent_desc = static_offscreen_desc;
    static_offscreen_transparent_desc.depth_write = false;
    static_offscreen_transparent_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
    static_offscreen_transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, static_offscreen_transparent_desc);
    if (!static_offscreen_transparent_pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create offscreen transparent static mesh pipeline");
        return false;
    }

    auto static_transparent_desc = static_desc;
    static_transparent_desc.depth_write = false;
    static_transparent_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
    static_transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, static_transparent_desc);
    if (!static_transparent_pipeline_.valid()) {
        LOG_F(ERROR, "Failed to create transparent static mesh pipeline");
        return false;
    }

    auto vs_skinned = shader_provider.get_shader("render_skinned_mesh.vs.hlsl");
    auto vs_skinned_shadow = shader_provider.get_shader("render_shadow_skinned.vs.hlsl");
    if (vs_skinned.valid()) {
        nw::gfx::PipelineDesc skin_desc{};
        skin_desc.vs = vs_skinned;
        skin_desc.fs = ps;
        skin_desc.uses_bindless_sampled_textures = true;
        skin_desc.uses_draw_uniforms2 = true;
        skin_desc.uses_storage_buffer = true;
        skin_desc.storage_buffer_count = 2;
        skin_desc.uses_single_texture = false;
        skin_desc.depth_test = true;
        skin_desc.depth_write = true;
        skin_desc.blend_mode = nw::gfx::BlendMode::disabled;
        skin_desc.vertex_stride = sizeof(SkinnedVertex);
        skin_desc.vertex_attributes = {
            {0, offsetof(SkinnedVertex, position), nw::gfx::VertexFormat::Float3},
            {1, offsetof(SkinnedVertex, normal), nw::gfx::VertexFormat::Float3},
            {2, offsetof(SkinnedVertex, texcoord), nw::gfx::VertexFormat::Float2},
            {4, offsetof(SkinnedVertex, joint_indices), nw::gfx::VertexFormat::UByte4},
            {5, offsetof(SkinnedVertex, joint_weights), nw::gfx::VertexFormat::UByte4Norm},
        };
        skinned_pipeline_ = nw::gfx::create_pipeline(ctx_, skin_desc);
        if (!skinned_pipeline_.valid()) {
            LOG_F(WARNING, "Failed to create skinned mesh pipeline — skinned meshes will use static pipeline");
        } else {
            if (vs_skinned_shadow.valid()) {
                auto skin_shadow_desc = skin_desc;
                skin_shadow_desc.vs = vs_skinned_shadow;
                skin_shadow_desc.fs = ps_shadow;
                skin_shadow_desc.uses_draw_uniforms2 = false;
                skin_shadow_desc.storage_buffer_count = 2;
                skin_shadow_desc.has_color_attachment = false;
                skin_shadow_desc.color_write = false;
                skin_shadow_desc.blend_mode = nw::gfx::BlendMode::disabled;
                skin_shadow_desc.use_swapchain_color_format = false;
                skin_shadow_desc.depth_attachment_format = nw::gfx::Fmt::D32F;
                skinned_shadow_pipeline_ = nw::gfx::create_pipeline(ctx_, skin_shadow_desc);
                if (!skinned_shadow_pipeline_.valid()) {
                    LOG_F(WARNING, "Failed to create skinned shadow pipeline");
                }

                auto skin_shadow_cutout_desc = skin_shadow_desc;
                skin_shadow_cutout_desc.uses_bindless_sampled_textures = true;
                skin_shadow_cutout_desc.uses_single_texture = false;
                skinned_shadow_cutout_pipeline_ = nw::gfx::create_pipeline(ctx_, skin_shadow_cutout_desc);
                if (!skinned_shadow_cutout_pipeline_.valid()) {
                    LOG_F(WARNING, "Failed to create skinned cutout shadow pipeline");
                }
            }

            auto skin_transparent_desc = skin_desc;
            skin_transparent_desc.depth_write = false;
            skin_transparent_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
            skinned_transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, skin_transparent_desc);
            if (!skinned_transparent_pipeline_.valid()) {
                LOG_F(WARNING, "Failed to create transparent skinned mesh pipeline");
            }

            auto skin_offscreen_desc = skin_desc;
            skin_offscreen_desc.use_swapchain_color_format = false;
            skin_offscreen_desc.color_attachment_format = nw::gfx::Fmt::RGBA16F;
            skin_offscreen_desc.depth_attachment_format = nw::gfx::Fmt::D32F;
            skinned_offscreen_pipeline_ = nw::gfx::create_pipeline(ctx_, skin_offscreen_desc);
            if (!skinned_offscreen_pipeline_.valid()) {
                LOG_F(WARNING, "Failed to create offscreen skinned mesh pipeline");
            }

            auto skin_offscreen_transparent_desc = skin_offscreen_desc;
            skin_offscreen_transparent_desc.depth_write = false;
            skin_offscreen_transparent_desc.blend_mode = nw::gfx::BlendMode::premultiplied_alpha;
            skinned_offscreen_transparent_pipeline_ = nw::gfx::create_pipeline(ctx_, skin_offscreen_transparent_desc);
            if (!skinned_offscreen_transparent_pipeline_.valid()) {
                LOG_F(WARNING, "Failed to create offscreen transparent skinned mesh pipeline");
            }
        }
    }

    fallback_texture_ = create_solid_texture(ctx_, nw::gfx::Fmt::RGBA8Srgb, {255, 255, 255, 255});
    fallback_normal_ = create_solid_texture(ctx_, nw::gfx::Fmt::RGBA8, {128, 128, 255, 255});
    fallback_surface_ = create_solid_texture(ctx_, nw::gfx::Fmt::RGBA8, {255, 128, 0, 255});
    fallback_emissive_ = create_solid_texture(ctx_, nw::gfx::Fmt::RGBA8Srgb, {0, 0, 0, 255});

    return true;
}

nw::gfx::BindlessTextureIndex ModelGpuBackend::fallback_albedo_index() const
{
    return nw::gfx::get_bindless_texture_index(ctx_, fallback_texture_);
}

nw::gfx::BindlessTextureIndex ModelGpuBackend::fallback_normal_index() const
{
    return nw::gfx::get_bindless_texture_index(ctx_, fallback_normal_);
}

nw::gfx::BindlessTextureIndex ModelGpuBackend::fallback_surface_index() const
{
    return nw::gfx::get_bindless_texture_index(ctx_, fallback_surface_);
}

nw::gfx::BindlessTextureIndex ModelGpuBackend::fallback_emissive_index() const
{
    return nw::gfx::get_bindless_texture_index(ctx_, fallback_emissive_);
}

nw::gfx::StorageSpan ModelGpuBackend::upload_bones(nw::gfx::CommandList* cmd, const glm::mat4* bones, uint32_t count)
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

bool ModelGpuBackend::ensure_shadow_resources(uint32_t resolution)
{
    resolution = std::max(resolution, 1u);
    if (shadow_render_targets_[0].valid() && shadow_map_resolution_ == resolution) {
        return true;
    }

    for (size_t i = 0; i < shadow_render_targets_.size(); ++i) {
        if (shadow_render_targets_[i].valid()) {
            nw::gfx::destroy_render_target(ctx_, shadow_render_targets_[i]);
            shadow_render_targets_[i] = {};
            shadow_depth_textures_[i] = {};
        }
    }

    for (size_t i = 0; i < shadow_render_targets_.size(); ++i) {
        nw::gfx::TextureDesc depth_desc{};
        depth_desc.width = resolution;
        depth_desc.height = resolution;
        depth_desc.format = nw::gfx::Fmt::D32F;
        depth_desc.sampled = true;
        depth_desc.render_target = true;
        shadow_depth_textures_[i] = nw::gfx::create_texture(ctx_, depth_desc);
        if (!shadow_depth_textures_[i].valid()) {
            LOG_F(ERROR, "Failed to create shadow depth target {}", i);
            return false;
        }

        nw::gfx::RenderTargetDesc rt_desc{};
        rt_desc.depth.texture = shadow_depth_textures_[i];
        shadow_render_targets_[i] = nw::gfx::create_render_target(ctx_, rt_desc);
        if (!shadow_render_targets_[i].valid()) {
            LOG_F(ERROR, "Failed to create shadow render target {}", i);
            nw::gfx::destroy_texture(ctx_, shadow_depth_textures_[i]);
            shadow_depth_textures_[i] = {};
            return false;
        }
    }

    shadow_map_resolution_ = resolution;
    return true;
}

} // namespace nw::render::nwn
