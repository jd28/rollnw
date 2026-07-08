#include <nw/render/model_gpu_backend.hpp>

#include <nw/formats/Plt.hpp>
#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/render/model_pipeline_desc.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nw::render {

namespace {

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

bool create_zero_storage_buffer(nw::gfx::Context* ctx, nw::gfx::Handle<nw::gfx::Buffer>& out)
{
    nw::gfx::BufferDesc desc{};
    desc.size = sizeof(uint32_t) * 4;
    desc.usage = nw::gfx::BufferUsage::Storage;
    desc.cpu_visible = true;

    out = nw::gfx::create_buffer(ctx, desc);
    if (!out.valid()) {
        LOG_F(ERROR, "Failed to create dummy model storage buffer");
        return false;
    }

    void* mapped = nw::gfx::map_buffer(out);
    if (!mapped) {
        LOG_F(ERROR, "Failed to map dummy model storage buffer");
        nw::gfx::destroy_buffer(out);
        out = {};
        return false;
    }

    std::memset(mapped, 0, desc.size);
    nw::gfx::unmap_buffer(out);
    return true;
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
    constexpr uint32_t max_palette_width = 256;
    constexpr uint32_t max_palette_height = 176;
    constexpr size_t pixels_per_layer = static_cast<size_t>(max_palette_width) * max_palette_height;

    struct GpuPltPaletteLayer {
        std::array<uint32_t, 4> dimensions{};
        std::array<uint32_t, pixels_per_layer> colors{};
    };

    static_assert(offsetof(GpuPltPaletteLayer, colors) == sizeof(uint32_t) * 4);
    static_assert(sizeof(GpuPltPaletteLayer) == (4 + pixels_per_layer) * sizeof(uint32_t));

    std::vector<GpuPltPaletteLayer> layers(nw::plt_layer_size);

    for (uint32_t layer = 0; layer < nw::plt_layer_size; ++layer) {
        auto* img = nw::kernel::resman().palette_texture(static_cast<nw::PltLayer>(layer));
        if (!img || !img->valid() || img->channels() < 3) {
            LOG_F(ERROR, "Failed to load palette texture for PLT layer {}", layer);
            return false;
        }
        if (img->width() == 0 || img->width() > max_palette_width
            || img->height() == 0 || img->height() > max_palette_height) {
            LOG_F(ERROR, "PLT palette layer {} has unsupported dimensions {}x{} (max {}x{})",
                layer,
                img->width(),
                img->height(),
                max_palette_width,
                max_palette_height);
            return false;
        }

        auto& gpu_layer = layers[layer];
        gpu_layer.dimensions[0] = img->width();
        gpu_layer.dimensions[1] = img->height();

        const uint8_t* src = img->data();
        const uint32_t channels = img->channels();
        for (uint32_t y = 0; y < img->height(); ++y) {
            for (uint32_t x = 0; x < img->width(); ++x) {
                const size_t src_index = (static_cast<size_t>(y) * img->width() + x) * channels;
                const size_t dst_index = static_cast<size_t>(y) * max_palette_width + x;
                gpu_layer.colors[dst_index] = pack_rgba8(src + src_index, channels);
            }
        }
    }

    nw::gfx::BufferDesc desc{};
    desc.size = layers.size() * sizeof(GpuPltPaletteLayer);
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

    std::memcpy(mapped, layers.data(), desc.size);
    nw::gfx::unmap_buffer(out);
    return true;
}

} // namespace

ModelGpuBackend::~ModelGpuBackend()
{
    destroy_common_frame_storage();
    destroy_common_gpu_resources();
}

void ModelGpuBackend::destroy_common_frame_storage()
{
    for (auto& arena : bone_arenas_) {
        arena.destroy();
    }
    for (auto& arena : static_draw_arenas_) {
        arena.destroy();
    }
    for (auto& arena : indirect_draw_arenas_) {
        arena.destroy();
    }
}

void ModelGpuBackend::destroy_common_gpu_resources()
{
    if (dummy_storage_buffer_.valid()) { nw::gfx::destroy_buffer(dummy_storage_buffer_); }
    if (plt_palette_buffer_.valid()) { nw::gfx::destroy_buffer(plt_palette_buffer_); }
    pipeline_cache_.destroy();
    if (fallback_texture_.valid()) { nw::gfx::destroy_texture(ctx_, fallback_texture_); }
    if (fallback_normal_.valid()) { nw::gfx::destroy_texture(ctx_, fallback_normal_); }
    if (fallback_surface_.valid()) { nw::gfx::destroy_texture(ctx_, fallback_surface_); }
    if (fallback_emissive_.valid()) { nw::gfx::destroy_texture(ctx_, fallback_emissive_); }
}

nw::gfx::Handle<nw::gfx::Pipeline> ModelGpuBackend::create_pipeline_slot(PipelineSlot slot, const nw::gfx::PipelineDesc& desc)
{
    auto pipeline = pipeline_cache_.get_or_create(desc);
    pipeline_slot(slot) = pipeline;
    return pipeline;
}

bool ModelGpuBackend::require_pipeline_slot(PipelineSlot slot, const nw::gfx::PipelineDesc& desc, const char* message)
{
    if (create_pipeline_slot(slot, desc).valid()) {
        return true;
    }

    LOG_F(ERROR, "{}", message);
    return false;
}

bool ModelGpuBackend::warn_pipeline_slot(PipelineSlot slot, const nw::gfx::PipelineDesc& desc, const char* message)
{
    if (create_pipeline_slot(slot, desc).valid()) {
        return true;
    }

    LOG_F(WARNING, "{}", message);
    return false;
}

nw::gfx::Handle<nw::gfx::Pipeline> ModelGpuBackend::pipeline(ModelPipelineKey key) const
{
    const bool offscreen = key.target == ModelPipelineTarget::offscreen;
    const bool translucent = key.material == MaterialMode::transparent || key.material == MaterialMode::water;

    switch (key.pass) {
    case ModelPipelinePass::depth_prepass:
        return key.mesh == ModelPipelineMeshKind::static_batched
            ? pipeline(PipelineSlot::static_batched_depth)
            : nw::gfx::Handle<nw::gfx::Pipeline>{};
    case ModelPipelinePass::shadow:
        switch (key.mesh) {
        case ModelPipelineMeshKind::static_mesh:
            return key.material == MaterialMode::cutout ? pipeline(PipelineSlot::static_shadow_cutout) : pipeline(PipelineSlot::static_shadow);
        case ModelPipelineMeshKind::static_batched:
            return pipeline(PipelineSlot::static_batched_shadow);
        case ModelPipelineMeshKind::skinned:
            return key.material == MaterialMode::cutout ? pipeline(PipelineSlot::skinned_shadow_cutout) : pipeline(PipelineSlot::skinned_shadow);
        case ModelPipelineMeshKind::pbr_static:
            return key.material == MaterialMode::cutout ? pipeline(PipelineSlot::pbr_static_shadow_cutout) : pipeline(PipelineSlot::pbr_static_shadow);
        case ModelPipelineMeshKind::pbr_skinned:
            return key.material == MaterialMode::cutout ? pipeline(PipelineSlot::pbr_skinned_shadow_cutout) : pipeline(PipelineSlot::pbr_skinned_shadow);
        }
        return {};
    case ModelPipelinePass::color:
        switch (key.mesh) {
        case ModelPipelineMeshKind::static_mesh:
            if (key.material == MaterialMode::water) {
                return offscreen ? pipeline(PipelineSlot::static_offscreen_water) : pipeline(PipelineSlot::static_water);
            }
            if (key.material == MaterialMode::transparent) {
                return offscreen ? pipeline(PipelineSlot::static_offscreen_transparent) : pipeline(PipelineSlot::static_transparent);
            }
            return offscreen ? pipeline(PipelineSlot::static_offscreen) : pipeline(PipelineSlot::static_mesh);
        case ModelPipelineMeshKind::static_batched:
            if (key.material == MaterialMode::water) {
                return {};
            }
            if (key.material == MaterialMode::transparent) {
                return offscreen ? pipeline(PipelineSlot::static_batched_offscreen_transparent) : pipeline(PipelineSlot::static_batched_transparent);
            }
            return offscreen ? pipeline(PipelineSlot::static_batched_offscreen) : pipeline(PipelineSlot::static_batched);
        case ModelPipelineMeshKind::skinned:
            if (translucent) {
                return offscreen ? pipeline(PipelineSlot::skinned_offscreen_transparent) : pipeline(PipelineSlot::skinned_transparent);
            }
            return offscreen ? pipeline(PipelineSlot::skinned_offscreen) : pipeline(PipelineSlot::skinned);
        case ModelPipelineMeshKind::pbr_static:
            if (translucent) {
                return pipeline(PipelineSlot::pbr_static_transparent);
            }
            return key.material == MaterialMode::cutout ? pipeline(PipelineSlot::pbr_static_cutout) : pipeline(PipelineSlot::pbr_static_opaque);
        case ModelPipelineMeshKind::pbr_skinned:
            if (translucent) {
                return pipeline(PipelineSlot::pbr_skinned_transparent);
            }
            return key.material == MaterialMode::cutout ? pipeline(PipelineSlot::pbr_skinned_cutout) : pipeline(PipelineSlot::pbr_skinned_opaque);
        }
        return {};
    }

    return {};
}

bool ModelGpuBackend::initialize_common_storage_resources()
{
    return create_zero_storage_buffer(ctx_, dummy_storage_buffer_)
        && create_plt_palette_buffer(ctx_, plt_palette_buffer_);
}

bool ModelGpuBackend::initialize_render_model_pbr_resources(nw::render::ShaderProvider& shader_provider)
{
    auto vs_pbr = shader_provider.get_shader("render_pbr_static.vs.hlsl");
    auto vs_pbr_skinned = shader_provider.get_shader("render_pbr_skinned.vs.hlsl");
    auto ps_pbr = shader_provider.get_shader("render_pbr_static.ps.hlsl");
    auto ps_pbr_shadow = shader_provider.get_shader("render_pbr_shadow.ps.hlsl");
    if (vs_pbr.valid() && ps_pbr.valid()) {
        auto pbr_desc = nw::render::make_pbr_static_pipeline_desc(vs_pbr, ps_pbr);
        create_pipeline_slot(PipelineSlot::pbr_static_opaque, pbr_desc);
        create_pipeline_slot(PipelineSlot::pbr_static_cutout, pbr_desc);
        if (!pipeline_slot(PipelineSlot::pbr_static_opaque).valid()
            || !pipeline_slot(PipelineSlot::pbr_static_cutout).valid()) {
            LOG_F(ERROR, "Failed to create static glTF PBR pipeline");
            return false;
        }

        auto pbr_transparent_desc = nw::render::make_transparent_pipeline_desc(pbr_desc);
        if (!require_pipeline_slot(PipelineSlot::pbr_static_transparent, pbr_transparent_desc,
                "Failed to create static glTF PBR transparent pipeline")) {
            return false;
        }

        if (ps_pbr_shadow.valid()) {
            auto pbr_shadow_desc = nw::render::make_pbr_shadow_pipeline_desc(pbr_desc, ps_pbr_shadow);
            warn_pipeline_slot(PipelineSlot::pbr_static_shadow, pbr_shadow_desc,
                "Failed to create static glTF PBR shadow pipeline");
            warn_pipeline_slot(PipelineSlot::pbr_static_shadow_cutout, pbr_shadow_desc,
                "Failed to create static glTF PBR cutout shadow pipeline");
        } else {
            LOG_F(WARNING, "Failed to load static glTF PBR shadow shader");
        }

        if (vs_pbr_skinned.valid()) {
            auto pbr_skinned_desc = nw::render::make_pbr_skinned_pipeline_desc(pbr_desc, vs_pbr_skinned);
            create_pipeline_slot(PipelineSlot::pbr_skinned_opaque, pbr_skinned_desc);
            create_pipeline_slot(PipelineSlot::pbr_skinned_cutout, pbr_skinned_desc);

            auto pbr_skinned_transparent_desc = nw::render::make_transparent_pipeline_desc(pbr_skinned_desc);
            create_pipeline_slot(PipelineSlot::pbr_skinned_transparent, pbr_skinned_transparent_desc);
            if (!pipeline_slot(PipelineSlot::pbr_skinned_opaque).valid()
                || !pipeline_slot(PipelineSlot::pbr_skinned_cutout).valid()
                || !pipeline_slot(PipelineSlot::pbr_skinned_transparent).valid()) {
                LOG_F(WARNING, "Failed to create skinned glTF PBR pipeline");
            }

            if (ps_pbr_shadow.valid()) {
                auto pbr_skinned_shadow_desc = nw::render::make_pbr_shadow_pipeline_desc(
                    pbr_skinned_desc, ps_pbr_shadow);
                warn_pipeline_slot(PipelineSlot::pbr_skinned_shadow, pbr_skinned_shadow_desc,
                    "Failed to create skinned glTF PBR shadow pipeline");
                warn_pipeline_slot(PipelineSlot::pbr_skinned_shadow_cutout, pbr_skinned_shadow_desc,
                    "Failed to create skinned glTF PBR cutout shadow pipeline");
            }
        }
    }

    return true;
}

void ModelGpuBackend::initialize_shared_fallback_textures()
{
    // Preserve the existing startup policy: fallback texture allocation is
    // best-effort, and invalid handles remain visible to the existing resource
    // and bindless texture error paths.
    fallback_texture_ = create_solid_texture(ctx_, nw::gfx::Fmt::RGBA8Srgb, {255, 255, 255, 255});
    fallback_normal_ = create_solid_texture(ctx_, nw::gfx::Fmt::RGBA8, {128, 128, 255, 255});
    fallback_surface_ = create_solid_texture(ctx_, nw::gfx::Fmt::RGBA8, {
                                                                            kModelSurfaceNeutralOcclusion,
                                                                            kModelSurfaceNeutralRoughness,
                                                                            kModelSurfaceNeutralMetallic,
                                                                            kModelSurfaceNeutralAlpha,
                                                                        });
    fallback_emissive_ = create_solid_texture(ctx_, nw::gfx::Fmt::RGBA8Srgb, {0, 0, 0, 255});
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
    if (!bones || count == 0 || !model_skin_bone_count_supported(count)) return {};

    nw::gfx::FrameInfo frame{};
    if (!cmd || !nw::gfx::get_frame_info(ctx_, frame)) return {};

    if (frame.frame_index >= bone_arenas_.size()) return {};
    const auto idx = static_cast<size_t>(frame.frame_index);
    auto& arena = bone_arenas_[idx];
    if (arena.frame_id() != frame.frame_id) {
        const uint32_t size = count * sizeof(glm::mat4);
        if (!arena.reset(ctx_, frame.frame_id, size)) return {};
    }
    return arena.write(ctx_, bones, count * sizeof(glm::mat4));
}

nw::gfx::StorageSpan ModelGpuBackend::upload_frame_storage(
    nw::gfx::CommandList* cmd, const void* data, uint32_t size, uint32_t alignment)
{
    if (!data || size == 0) {
        return {};
    }

    nw::gfx::FrameInfo frame{};
    if (!cmd || !nw::gfx::get_frame_info(ctx_, frame)) {
        return {};
    }

    if (frame.frame_index >= static_draw_arenas_.size()) {
        return {};
    }
    const auto idx = static_cast<size_t>(frame.frame_index);
    auto& arena = static_draw_arenas_[idx];
    if (arena.frame_id() != frame.frame_id) {
        if (!arena.reset(ctx_, frame.frame_id, size)) {
            return {};
        }
    }
    return arena.write(ctx_, data, size, alignment);
}

MappedStorageSpan ModelGpuBackend::allocate_static_draw_data(nw::gfx::CommandList* cmd, uint32_t size)
{
    if (size == 0) {
        return {};
    }

    nw::gfx::FrameInfo frame{};
    if (!cmd || !nw::gfx::get_frame_info(ctx_, frame)) {
        return {};
    }

    if (frame.frame_index >= static_draw_arenas_.size()) {
        return {};
    }
    const auto idx = static_cast<size_t>(frame.frame_index);
    auto& arena = static_draw_arenas_[idx];
    if (arena.frame_id() != frame.frame_id) {
        if (!arena.reset(ctx_, frame.frame_id, size)) {
            return {};
        }
    }
    return arena.allocate_mapped(ctx_, size);
}

MappedIndirectDrawSpan ModelGpuBackend::allocate_indirect_draw_data(nw::gfx::CommandList* cmd, uint32_t size)
{
    if (size == 0) {
        return {};
    }

    nw::gfx::FrameInfo frame{};
    if (!cmd || !nw::gfx::get_frame_info(ctx_, frame)) {
        return {};
    }

    if (frame.frame_index >= indirect_draw_arenas_.size()) {
        return {};
    }
    const auto idx = static_cast<size_t>(frame.frame_index);
    auto& arena = indirect_draw_arenas_[idx];
    if (arena.frame_id() != frame.frame_id) {
        if (!arena.reset(ctx_, frame.frame_id, size, nw::gfx::BufferUsage::Indirect)) {
            return {};
        }
    }
    const auto mapped = arena.allocate_mapped(
        ctx_, size, static_cast<uint32_t>(alignof(nw::gfx::IndexedIndirectDrawCommand)));
    return {
        .span = nw::gfx::IndirectDrawSpan{mapped.span.buffer, mapped.span.offset, mapped.span.size},
        .data = mapped.data,
    };
}

nw::gfx::StorageSpan ModelGpuBackend::upload_static_draw_data(nw::gfx::CommandList* cmd, const void* data, uint32_t size)
{
    return upload_frame_storage(cmd, data, size);
}

} // namespace nw::render
