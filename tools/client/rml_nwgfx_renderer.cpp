#include "rml_nwgfx_renderer.hpp"

#include "rml_nwgfx_shaders.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>

#include <nw/formats/Image.hpp>
#include <nw/resources/assets.hpp>
#include <nw/util/profile.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct ShaderUniform {
    Rml::Matrix4f m_transform;
    Rml::Vector2f m_translate;
};

struct GpuBuffer {
    nw::gfx::Handle<nw::gfx::Buffer> handle;
    size_t size = 0;
};

struct TextureData {
    nw::gfx::Handle<nw::gfx::Texture> texture;
    int width = 0;
    int height = 0;
};

struct GeometryData {
    GpuBuffer vertex;
    GpuBuffer index;
    uint32_t index_count = 0;
};

bool create_gfx_buffer(nw::gfx::Context* context, size_t size, nw::gfx::BufferUsage usage, GpuBuffer& out)
{
    nw::gfx::BufferDesc desc{};
    desc.size = size;
    desc.usage = usage;
    desc.cpu_visible = true;

    out.handle = nw::gfx::create_buffer(context, desc);
    if (!out.handle.valid()) {
        return false;
    }

    out.size = size;
    return true;
}

void destroy_gfx_buffer(GpuBuffer& buffer)
{
    if (buffer.handle.valid()) {
        nw::gfx::destroy_buffer(buffer.handle);
    }
    buffer = GpuBuffer{};
}

void destroy_geometry_data(GeometryData* geometry)
{
    if (!geometry) {
        return;
    }
    destroy_gfx_buffer(geometry->vertex);
    destroy_gfx_buffer(geometry->index);
    delete geometry;
}

void destroy_texture_data(nw::gfx::Context* context, TextureData* texture)
{
    if (!texture) {
        return;
    }
    if (context && texture->texture.valid()) {
        nw::gfx::destroy_texture(context, texture->texture);
    }
    delete texture;
}

Rml::Vector2f round_to_pixel(Rml::Vector2f value)
{
    return {std::round(value.x), std::round(value.y)};
}

bool environment_flag_enabled(const char* name)
{
    const char* value = std::getenv(name);
    return value && (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 || std::strcmp(value, "yes") == 0 || std::strcmp(value, "on") == 0);
}

bool font_debug_logging_enabled()
{
    static const bool enabled = environment_flag_enabled("ROLLNW_RML_FONT_DEBUG");
    return enabled;
}

} // namespace

struct RmlNwgfxRenderer::Impl {
    nw::gfx::Handle<nw::gfx::Shader> shader_vert;
    nw::gfx::Handle<nw::gfx::Shader> shader_frag_color;
    nw::gfx::Handle<nw::gfx::Shader> shader_frag_texture;
    nw::gfx::Handle<nw::gfx::Pipeline> pipeline_color;
    nw::gfx::Handle<nw::gfx::Pipeline> pipeline_texture;

    // Stencil clip pipelines
    nw::gfx::Handle<nw::gfx::Pipeline> pipeline_stencil_mask;      // Writes to stencil, no color output
    nw::gfx::Handle<nw::gfx::Pipeline> pipeline_color_stenciled;   // Color geometry with stencil test
    nw::gfx::Handle<nw::gfx::Pipeline> pipeline_texture_stenciled; // Textured geometry with stencil test

    Rml::Matrix4f projection = Rml::Matrix4f::Identity();
    Rml::Matrix4f transform = Rml::Matrix4f::Identity();
    uint32_t layout_width = 0;
    uint32_t layout_height = 0;
    bool transform_enabled = false;

    bool scissor_enabled = false;
    Rml::Rectanglei scissor{};

    // Stencil clip state
    bool stencil_clip_active = false;
    uint8_t stencil_ref = 0;
    bool stencil_mask_dirty = false;       // True when stencil mask needs to be re-rendered
    Rml::Rectanglei stencil_clip_region{}; // The clip region for stencil

    bool render_pass_active = false;
    bool frame_color_initialized = false;

    static constexpr uint32_t kFramesInFlight = 2;
    uint32_t current_bucket = 0;
    std::unordered_set<GeometryData*> active_geometry;
    std::vector<GeometryData*> deferred_geometry[kFramesInFlight];
    std::vector<TextureData*> deferred_textures[kFramesInFlight];
    Rml::TextureHandle next_texture_handle = 1;
    uint32_t generated_texture_log_count = 0;
    uint32_t snapped_translation_log_count = 0;
    std::unordered_map<Rml::TextureHandle, TextureData*> textures;
};

RmlNwgfxRenderer::~RmlNwgfxRenderer()
{
    shutdown();
}

bool RmlNwgfxRenderer::initialize(nw::gfx::Core* core, nw::gfx::Context* context)
{
    NW_PROFILE_SCOPE_N("RmlNwgfxRenderer::initialize");

    core_ = core;
    context_ = context;
    initialized_ = core_ && context_ && nw::gfx::get_frame_info(context_, frame_info_);
    warned_not_rendering_ = false;

    if (!initialized_) {
        return false;
    }

    impl_ = new Impl();
    impl_->layout_width = frame_info_.width;
    impl_->layout_height = frame_info_.height;

    nw::gfx::ShaderDesc shader_desc{};
    const auto vertex_shader = rml_nwgfx_vertex_shader();
    shader_desc.code = vertex_shader.data;
    shader_desc.size = vertex_shader.size;
    impl_->shader_vert = nw::gfx::create_shader(context_, shader_desc);
    if (!impl_->shader_vert.valid()) {
        shutdown();
        return false;
    }

    const auto color_fragment_shader = rml_nwgfx_color_fragment_shader();
    shader_desc.code = color_fragment_shader.data;
    shader_desc.size = color_fragment_shader.size;
    impl_->shader_frag_color = nw::gfx::create_shader(context_, shader_desc);
    if (!impl_->shader_frag_color.valid()) {
        shutdown();
        return false;
    }

    const auto texture_fragment_shader = rml_nwgfx_texture_fragment_shader();
    shader_desc.code = texture_fragment_shader.data;
    shader_desc.size = texture_fragment_shader.size;
    impl_->shader_frag_texture = nw::gfx::create_shader(context_, shader_desc);
    if (!impl_->shader_frag_texture.valid()) {
        shutdown();
        return false;
    }

    // RmlUi is purely 2D: all vertices have z=0, which projects to the same depth value.
    // Depth test must be off; otherwise only the first draw on each pixel survives.
    nw::gfx::PipelineDesc pipeline_desc{};
    pipeline_desc.vs = impl_->shader_vert;
    pipeline_desc.fs = impl_->shader_frag_color;
    pipeline_desc.depth_test = false;
    pipeline_desc.depth_write = false;
    impl_->pipeline_color = nw::gfx::create_pipeline(context_, pipeline_desc);
    if (!impl_->pipeline_color.valid()) {
        shutdown();
        return false;
    }

    pipeline_desc.fs = impl_->shader_frag_texture;
    impl_->pipeline_texture = nw::gfx::create_pipeline(context_, pipeline_desc);
    if (!impl_->pipeline_texture.valid()) {
        shutdown();
        return false;
    }

    // Create stencil pipelines
    // Stencil mask pipeline: writes to stencil, no color output, no depth test (2D)
    nw::gfx::PipelineDesc stencil_desc{};
    stencil_desc.vs = impl_->shader_vert;
    stencil_desc.fs = impl_->shader_frag_color;
    stencil_desc.depth_test = false;
    stencil_desc.depth_write = false;
    stencil_desc.stencil_test = true;
    stencil_desc.stencil_compare = nw::gfx::CompareOp::AlwaysPass; // Always pass, write ref value
    stencil_desc.stencil_pass = nw::gfx::StencilOp::Replace;       // Write stencil_ref to stencil
    stencil_desc.color_write = false;                              // No color output
    impl_->pipeline_stencil_mask = nw::gfx::create_pipeline(context_, stencil_desc);
    if (!impl_->pipeline_stencil_mask.valid()) {
        shutdown();
        return false;
    }

    // Color stenciled pipeline: tests stencil, draws color (2D - depth off)
    nw::gfx::PipelineDesc color_stenciled_desc{};
    color_stenciled_desc.vs = impl_->shader_vert;
    color_stenciled_desc.fs = impl_->shader_frag_color;
    color_stenciled_desc.depth_test = false;
    color_stenciled_desc.depth_write = false;
    color_stenciled_desc.stencil_test = true;
    color_stenciled_desc.stencil_compare = nw::gfx::CompareOp::Equal; // Pass if stencil == ref
    color_stenciled_desc.stencil_pass = nw::gfx::StencilOp::Keep;     // Don't modify stencil
    impl_->pipeline_color_stenciled = nw::gfx::create_pipeline(context_, color_stenciled_desc);
    if (!impl_->pipeline_color_stenciled.valid()) {
        shutdown();
        return false;
    }

    // Texture stenciled pipeline: tests stencil, draws textured
    nw::gfx::PipelineDesc texture_stenciled_desc{};
    texture_stenciled_desc.vs = impl_->shader_vert;
    texture_stenciled_desc.fs = impl_->shader_frag_texture;
    texture_stenciled_desc.depth_test = false;
    texture_stenciled_desc.depth_write = false;
    texture_stenciled_desc.stencil_test = true;
    texture_stenciled_desc.stencil_compare = nw::gfx::CompareOp::Equal; // Pass if stencil == ref
    texture_stenciled_desc.stencil_pass = nw::gfx::StencilOp::Keep;     // Don't modify stencil
    impl_->pipeline_texture_stenciled = nw::gfx::create_pipeline(context_, texture_stenciled_desc);
    if (!impl_->pipeline_texture_stenciled.valid()) {
        shutdown();
        return false;
    }

    return true;
}

void RmlNwgfxRenderer::shutdown()
{
    NW_PROFILE_SCOPE_N("RmlNwgfxRenderer::shutdown");

    command_list_ = nullptr;
    frame_active_ = false;
    initialized_ = false;

    if (!impl_) {
        core_ = nullptr;
        context_ = nullptr;
        return;
    }

    if (context_) {
        nw::gfx::wait_idle(context_);
    }

    for (uint32_t i = 0; i < Impl::kFramesInFlight; ++i) {
        for (auto* geometry : impl_->deferred_geometry[i]) {
            destroy_geometry_data(geometry);
        }
        impl_->deferred_geometry[i].clear();

        for (auto* tex : impl_->deferred_textures[i]) {
            destroy_texture_data(context_, tex);
        }
        impl_->deferred_textures[i].clear();
    }

    for (auto* geometry : impl_->active_geometry) {
        destroy_geometry_data(geometry);
    }
    impl_->active_geometry.clear();

    for (auto& [handle, tex] : impl_->textures) {
        (void)handle;
        destroy_texture_data(context_, tex);
    }
    impl_->textures.clear();

    if (impl_->pipeline_texture_stenciled.valid()) nw::gfx::destroy_pipeline(context_, impl_->pipeline_texture_stenciled);
    if (impl_->pipeline_color_stenciled.valid()) nw::gfx::destroy_pipeline(context_, impl_->pipeline_color_stenciled);
    if (impl_->pipeline_stencil_mask.valid()) nw::gfx::destroy_pipeline(context_, impl_->pipeline_stencil_mask);
    if (impl_->pipeline_texture.valid()) nw::gfx::destroy_pipeline(context_, impl_->pipeline_texture);
    if (impl_->pipeline_color.valid()) nw::gfx::destroy_pipeline(context_, impl_->pipeline_color);
    if (impl_->shader_frag_texture.valid()) nw::gfx::destroy_shader(context_, impl_->shader_frag_texture);
    if (impl_->shader_frag_color.valid()) nw::gfx::destroy_shader(context_, impl_->shader_frag_color);
    if (impl_->shader_vert.valid()) nw::gfx::destroy_shader(context_, impl_->shader_vert);
    delete impl_;
    impl_ = nullptr;

    core_ = nullptr;
    context_ = nullptr;
}

bool RmlNwgfxRenderer::begin_frame()
{
    NW_PROFILE_SCOPE_N("RmlNwgfxRenderer::begin_frame");

    if (!initialized_) {
        frame_active_ = false;
        command_list_ = nullptr;
        return false;
    }

    command_list_ = nw::gfx::begin_frame(context_);
    frame_active_ = command_list_ && nw::gfx::get_frame_info(context_, frame_info_);
    if (!frame_active_) {
        return false;
    }

    impl_->render_pass_active = false;
    impl_->frame_color_initialized = false;
    impl_->stencil_clip_active = false;
    impl_->stencil_ref = 0;
    impl_->stencil_mask_dirty = false;
    impl_->current_bucket = frame_info_.frame_index % Impl::kFramesInFlight;
    for (auto* geometry : impl_->deferred_geometry[impl_->current_bucket]) {
        destroy_geometry_data(geometry);
    }
    impl_->deferred_geometry[impl_->current_bucket].clear();

    for (auto* tex : impl_->deferred_textures[impl_->current_bucket]) {
        destroy_texture_data(context_, tex);
    }
    impl_->deferred_textures[impl_->current_bucket].clear();

    const float width = static_cast<float>(std::max(1u, impl_->layout_width));
    const float height = static_cast<float>(std::max(1u, impl_->layout_height));
    impl_->projection = Rml::Matrix4f::ProjectOrtho(0.0f, width, height, 0.0f, -10000.0f, 10000.0f);
    Rml::Matrix4f correction;
    correction.SetColumns(Rml::Vector4f(1.0f, 0.0f, 0.0f, 0.0f),
        Rml::Vector4f(0.0f, -1.0f, 0.0f, 0.0f),
        Rml::Vector4f(0.0f, 0.0f, 0.5f, 0.0f),
        Rml::Vector4f(0.0f, 0.0f, 0.5f, 1.0f));
    impl_->projection = correction * impl_->projection;

    return true;
}

void RmlNwgfxRenderer::end_frame()
{
    NW_PROFILE_SCOPE_N("RmlNwgfxRenderer::end_frame");

    finish_render_pass();

    if (command_list_) {
        nw::gfx::end_frame(context_);
    }
    command_list_ = nullptr;
    frame_active_ = false;
}

void RmlNwgfxRenderer::finish_render_pass()
{
    if (impl_ && impl_->render_pass_active) {
        nw::gfx::cmd_end_render(command_list_);
        impl_->render_pass_active = false;
    }
}

void RmlNwgfxRenderer::on_resize(uint32_t width, uint32_t height, Rml::Context* context)
{
    width = std::max(1u, width);
    height = std::max(1u, height);
    impl_->layout_width = width;
    impl_->layout_height = height;
    if (context) {
        context->SetDimensions(Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
    }
}

Rml::CompiledGeometryHandle RmlNwgfxRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
    NW_PROFILE_SCOPE_N("RmlNwgfxRenderer::CompileGeometry");
    NW_PROFILE_VALUE(vertices.size());

    auto* geometry = new GeometryData{};

    if (vertices.empty() || indices.empty()) {
        impl_->active_geometry.insert(geometry);
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
    }

    if (!create_gfx_buffer(context_, vertices.size() * sizeof(Rml::Vertex), nw::gfx::BufferUsage::Vertex, geometry->vertex)
        || !create_gfx_buffer(context_, indices.size() * sizeof(int), nw::gfx::BufferUsage::Index, geometry->index)) {
        destroy_geometry_data(geometry);
        auto* noop_geometry = new GeometryData{};
        impl_->active_geometry.insert(noop_geometry);
        return reinterpret_cast<Rml::CompiledGeometryHandle>(noop_geometry);
    }

    if (void* vertex_ptr = nw::gfx::map_buffer(geometry->vertex.handle)) {
        std::memcpy(vertex_ptr, vertices.data(), vertices.size() * sizeof(Rml::Vertex));
    }
    if (void* index_ptr = nw::gfx::map_buffer(geometry->index.handle)) {
        std::memcpy(index_ptr, indices.data(), indices.size() * sizeof(int));
    }
    geometry->index_count = static_cast<uint32_t>(indices.size());
    impl_->active_geometry.insert(geometry);

    return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void RmlNwgfxRenderer::render_stencil_mask_if_needed()
{
    if (!impl_->stencil_clip_active || !impl_->stencil_mask_dirty) {
        return;
    }

    // Build clip region geometry (rectangle)
    Rml::Vertex vertices[4];
    vertices[0].position = Rml::Vector2f(static_cast<float>(impl_->stencil_clip_region.Left()), static_cast<float>(impl_->stencil_clip_region.Top()));
    vertices[0].colour = Rml::ColourbPremultiplied(255, 255, 255, 255);
    vertices[0].tex_coord = Rml::Vector2f(0, 0);
    vertices[1].position = Rml::Vector2f(static_cast<float>(impl_->stencil_clip_region.Right()), static_cast<float>(impl_->stencil_clip_region.Top()));
    vertices[1].colour = Rml::ColourbPremultiplied(255, 255, 255, 255);
    vertices[1].tex_coord = Rml::Vector2f(0, 0);
    vertices[2].position = Rml::Vector2f(static_cast<float>(impl_->stencil_clip_region.Right()), static_cast<float>(impl_->stencil_clip_region.Bottom()));
    vertices[2].colour = Rml::ColourbPremultiplied(255, 255, 255, 255);
    vertices[2].tex_coord = Rml::Vector2f(0, 0);
    vertices[3].position = Rml::Vector2f(static_cast<float>(impl_->stencil_clip_region.Left()), static_cast<float>(impl_->stencil_clip_region.Bottom()));
    vertices[3].colour = Rml::ColourbPremultiplied(255, 255, 255, 255);
    vertices[3].tex_coord = Rml::Vector2f(0, 0);

    int indices[6] = {0, 2, 1, 0, 3, 2};

    // Compile and render the clip region to stencil
    if (auto clip_geom = CompileGeometry({vertices, 4}, {indices, 6})) {
        // Set up uniform for clip geometry (identity transform, no translation)
        ShaderUniform uniform{};
        uniform.m_transform = impl_->projection;
        uniform.m_translate = Rml::Vector2f(0, 0);
        const auto uniform_span = nw::gfx::allocate_uniform_span(context_, sizeof(ShaderUniform));
        if (uniform_span.data) {
            std::memcpy(uniform_span.data, &uniform, sizeof(uniform));

            // Disable scissor for stencil mask (we want full rectangle)
            nw::gfx::cmd_set_scissor(command_list_, 0, 0, frame_info_.width, frame_info_.height);

            // Bind stencil mask pipeline and draw clip region
            nw::gfx::cmd_set_stencil_ref(command_list_, impl_->stencil_ref);
            nw::gfx::cmd_bind_pipeline(command_list_, impl_->pipeline_stencil_mask);
            nw::gfx::cmd_bind_vertex_buffer(command_list_,
                reinterpret_cast<GeometryData*>(clip_geom)->vertex.handle, sizeof(Rml::Vertex));
            nw::gfx::cmd_bind_index_buffer(command_list_,
                reinterpret_cast<GeometryData*>(clip_geom)->index.handle, sizeof(uint32_t));
            nw::gfx::cmd_bind_uniform_texture(command_list_,
                impl_->pipeline_stencil_mask, uniform_span, {});
            nw::gfx::cmd_draw_indexed(command_list_, 6, 1);
        }

        ReleaseGeometry(clip_geom);
    }

    impl_->stencil_mask_dirty = false;
}

void RmlNwgfxRenderer::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture)
{
    NW_PROFILE_SCOPE_N("RmlNwgfxRenderer::RenderGeometry");

    auto* geometry = reinterpret_cast<GeometryData*>(handle);
    if (!geometry || geometry->index_count == 0) {
        return;
    }

    if (!geometry->vertex.handle.valid() || !geometry->index.handle.valid()
        || !frame_active_ || !frame_info_.drawable) {
        if (!warned_not_rendering_) {
            Rml::Log::Message(Rml::Log::LT_WARNING, "RmlNwgfxRenderer: skipping draw (invalid frame or geometry)");
            warned_not_rendering_ = true;
        }
        return;
    }

    warned_not_rendering_ = false;

    if (!impl_->render_pass_active) {
        const auto load_op = impl_->frame_color_initialized
            ? nw::gfx::RenderLoadOp::load
            : nw::gfx::RenderLoadOp::clear;
        nw::gfx::cmd_begin_render(command_list_, {}, load_op);
        impl_->render_pass_active = true;
        impl_->frame_color_initialized = true;
    }

    // Render stencil mask if needed (must be after render pass is active)
    render_stencil_mask_if_needed();

    nw::gfx::cmd_set_viewport(command_list_, 0.0f, 0.0f,
        static_cast<float>(frame_info_.width), static_cast<float>(frame_info_.height),
        0.0f, 1.0f);

    int32_t scissor_x = 0;
    int32_t scissor_y = 0;
    uint32_t scissor_width = frame_info_.width;
    uint32_t scissor_height = frame_info_.height;
    if (impl_->scissor_enabled) {
        const float scale_x = static_cast<float>(frame_info_.width) / static_cast<float>(std::max(1u, impl_->layout_width));
        const float scale_y = static_cast<float>(frame_info_.height) / static_cast<float>(std::max(1u, impl_->layout_height));
        const int32_t left = static_cast<int32_t>(std::floor(static_cast<float>(impl_->scissor.Left()) * scale_x));
        const int32_t top = static_cast<int32_t>(std::floor(static_cast<float>(impl_->scissor.Top()) * scale_y));
        const int32_t right = static_cast<int32_t>(std::ceil(static_cast<float>(impl_->scissor.Right()) * scale_x));
        const int32_t bottom = static_cast<int32_t>(std::ceil(static_cast<float>(impl_->scissor.Bottom()) * scale_y));

        const int32_t clamped_left = std::clamp(left, 0, static_cast<int32_t>(frame_info_.width));
        const int32_t clamped_top = std::clamp(top, 0, static_cast<int32_t>(frame_info_.height));
        const int32_t clamped_right = std::clamp(right, clamped_left, static_cast<int32_t>(frame_info_.width));
        const int32_t clamped_bottom = std::clamp(bottom, clamped_top, static_cast<int32_t>(frame_info_.height));

        scissor_x = clamped_left;
        scissor_y = clamped_top;
        scissor_width = static_cast<uint32_t>(clamped_right - clamped_left);
        scissor_height = static_cast<uint32_t>(clamped_bottom - clamped_top);
    }
    nw::gfx::cmd_set_scissor(command_list_,
        scissor_x,
        scissor_y,
        scissor_width,
        scissor_height);

    nw::gfx::Handle<nw::gfx::Texture> texture_handle;
    TextureData* texture_data = nullptr;
    bool textured = false;
    if (texture != 0) {
        auto it = impl_->textures.find(texture);
        texture_data = it != impl_->textures.end() ? it->second : nullptr;
        if (texture_data && texture_data->texture.valid()) {
            texture_handle = texture_data->texture;
            textured = true;
        }
    }

    // Select pipeline based on stencil and textured state
    nw::gfx::Handle<nw::gfx::Pipeline> pipeline;
    if (impl_->stencil_clip_active) {
        nw::gfx::cmd_set_stencil_ref(command_list_, impl_->stencil_ref);
        pipeline = textured ? impl_->pipeline_texture_stenciled : impl_->pipeline_color_stenciled;
    } else {
        pipeline = textured ? impl_->pipeline_texture : impl_->pipeline_color;
    }

    Rml::Vector2f draw_translation = translation;
    if (textured && !impl_->transform_enabled) {
        draw_translation = round_to_pixel(translation);
        if ((draw_translation.x != translation.x || draw_translation.y != translation.y)
            && impl_->snapped_translation_log_count < 8) {
            Rml::Log::Message(Rml::Log::LT_INFO,
                "RmlNwgfxRenderer: snapped textured geometry translation from %.3f, %.3f to %.3f, %.3f",
                static_cast<double>(translation.x),
                static_cast<double>(translation.y),
                static_cast<double>(draw_translation.x),
                static_cast<double>(draw_translation.y));
            ++impl_->snapped_translation_log_count;
        }
    }

    ShaderUniform uniform{};
    uniform.m_transform = impl_->projection * (impl_->transform_enabled ? impl_->transform : Rml::Matrix4f::Identity());
    uniform.m_translate = draw_translation;
    const auto uniform_span = nw::gfx::allocate_uniform_span(context_, sizeof(ShaderUniform));
    if (!uniform_span.data) {
        return;
    }
    std::memcpy(uniform_span.data, &uniform, sizeof(uniform));

    nw::gfx::cmd_bind_pipeline(command_list_, pipeline);

    nw::gfx::cmd_bind_vertex_buffer(command_list_, geometry->vertex.handle, sizeof(Rml::Vertex));
    nw::gfx::cmd_bind_index_buffer(command_list_, geometry->index.handle, sizeof(uint32_t));

    nw::gfx::cmd_bind_uniform_texture(command_list_,
        pipeline,
        uniform_span,
        textured ? texture_handle : nw::gfx::Handle<nw::gfx::Texture>{},
        nw::gfx::TextureFilter::Linear);

    nw::gfx::cmd_draw_indexed(command_list_, geometry->index_count, 1);
}

void RmlNwgfxRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
    if (!impl_) {
        return;
    }

    auto* geometry = reinterpret_cast<GeometryData*>(handle);
    if (!geometry) {
        return;
    }

    if (impl_->active_geometry.erase(geometry) == 0) {
        return;
    }

    impl_->deferred_geometry[impl_->current_bucket].push_back(geometry);
}

Rml::TextureHandle RmlNwgfxRenderer::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
    texture_dimensions = Rml::Vector2i(0, 0);
    if (generated_textures_) {
        const auto* generated = nw::toolset::find_generated_texture(
            *generated_textures_, source);
        if (generated) {
            const uint64_t expected_size = static_cast<uint64_t>(generated->width)
                * generated->height * 4;
            if (generated->width == 0 || generated->height == 0
                || generated->width > static_cast<uint32_t>(std::numeric_limits<int>::max())
                || generated->height > static_cast<uint32_t>(std::numeric_limits<int>::max())
                || expected_size != generated->rgba.size()) {
                Rml::Log::Message(Rml::Log::LT_WARNING,
                    "RmlNwgfxRenderer: generated texture '%s' has invalid dimensions",
                    source.c_str());
                return {};
            }
            const Rml::Vector2i dimensions{
                static_cast<int>(generated->width),
                static_cast<int>(generated->height),
            };
            const auto handle = GenerateTexture(
                Rml::Span<const Rml::byte>{generated->rgba.data(), generated->rgba.size()},
                dimensions);
            if (handle) {
                texture_dimensions = dimensions;
            }
            return handle;
        }
    }
    auto* files = Rml::GetFileInterface();
    Rml::String encoded;
    if (!files || !files->LoadFile(source, encoded) || encoded.empty()) {
        Rml::Log::Message(Rml::Log::LT_WARNING,
            "RmlNwgfxRenderer: failed to read texture '%s'", source.c_str());
        return {};
    }

    nw::ResourceData data;
    data.name = nw::Resource::from_filename(source);
    data.bytes.append(encoded.data(), encoded.size());
    nw::Image image{std::move(data), false};
    if (!image.valid() || image.width() == 0 || image.height() == 0
        || image.width() > static_cast<uint32_t>(std::numeric_limits<int>::max())
        || image.height() > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        Rml::Log::Message(Rml::Log::LT_WARNING,
            "RmlNwgfxRenderer: failed to decode texture '%s'", source.c_str());
        return {};
    }

    const size_t pixel_count = static_cast<size_t>(image.width()) * image.height();
    if (pixel_count > std::numeric_limits<size_t>::max() / 4) {
        return {};
    }
    std::vector<Rml::byte> rgba(pixel_count * 4);
    const auto* source_pixels = image.data();
    const uint32_t channels = image.channels();
    if (channels == 4) {
        std::memcpy(rgba.data(), source_pixels, rgba.size());
    } else if (channels >= 1 && channels <= 3) {
        for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
            const auto* input = source_pixels + pixel * channels;
            auto* output = rgba.data() + pixel * 4;
            if (channels == 1 || channels == 2) {
                output[0] = input[0];
                output[1] = input[0];
                output[2] = input[0];
                output[3] = channels == 2 ? input[1] : 255;
            } else {
                output[0] = input[0];
                output[1] = input[1];
                output[2] = input[2];
                output[3] = 255;
            }
        }
    } else {
        Rml::Log::Message(Rml::Log::LT_WARNING,
            "RmlNwgfxRenderer: texture '%s' has unsupported channel count %u",
            source.c_str(), channels);
        return {};
    }

    const Rml::Vector2i dimensions{
        static_cast<int>(image.width()),
        static_cast<int>(image.height()),
    };
    const auto handle = GenerateTexture(
        Rml::Span<const Rml::byte>{rgba.data(), rgba.size()}, dimensions);
    if (handle) {
        texture_dimensions = dimensions;
    }
    return handle;
}

Rml::TextureHandle RmlNwgfxRenderer::GenerateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions)
{
    NW_PROFILE_SCOPE_N("RmlNwgfxRenderer::GenerateTexture");

    if (source_dimensions.x <= 0 || source_dimensions.y <= 0) {
        return {};
    }

    auto* tex = new TextureData{};
    tex->width = source_dimensions.x;
    tex->height = source_dimensions.y;
    if (font_debug_logging_enabled() && impl_ && impl_->generated_texture_log_count < 8) {
        Rml::Log::Message(Rml::Log::LT_INFO,
            "RmlNwgfxRenderer: generated texture %dx%d, %zu bytes",
            tex->width, tex->height, source_data.size());
        ++impl_->generated_texture_log_count;
    }

    nw::gfx::TextureDesc texture_desc{};
    texture_desc.width = static_cast<uint32_t>(tex->width);
    texture_desc.height = static_cast<uint32_t>(tex->height);
    texture_desc.format = nw::gfx::Fmt::RGBA8;
    texture_desc.sampled = true;
    texture_desc.storage = false;
    texture_desc.render_target = false;

    tex->texture = nw::gfx::create_texture(context_, texture_desc);
    if (!tex->texture.valid()) {
        delete tex;
        return {};
    }

    const size_t upload_size = source_data.size();
    if (!nw::gfx::upload_texture_rgba8(context_, tex->texture, source_data.data(), upload_size)) {
        nw::gfx::destroy_texture(context_, tex->texture);
        delete tex;
        return {};
    }

    const auto handle = impl_->next_texture_handle++;
    impl_->textures[handle] = tex;
    return handle;
}

void RmlNwgfxRenderer::ReleaseTexture(Rml::TextureHandle texture_handle)
{
    if (!impl_) {
        return;
    }

    auto it = impl_->textures.find(texture_handle);
    auto* tex = it != impl_->textures.end() ? it->second : nullptr;
    if (!tex) {
        return;
    }

    impl_->textures.erase(it);

    impl_->deferred_textures[impl_->current_bucket].push_back(tex);
}

void RmlNwgfxRenderer::EnableScissorRegion(bool enable)
{
    impl_->scissor_enabled = enable;
    if (!enable) {
        impl_->stencil_clip_active = false;
        impl_->stencil_ref = 0;
    }
}

void RmlNwgfxRenderer::SetScissorRegion(Rml::Rectanglei region)
{
    impl_->scissor = region;

    // When transform is enabled, we need stencil-based clipping
    // because scissor rectangles can't handle rotated/transformed clips
    if (impl_->transform_enabled && impl_->scissor_enabled) {
        impl_->stencil_clip_active = true;
        impl_->stencil_ref = 1; // Use reference value 1 for clip region
        impl_->stencil_clip_region = region;
        impl_->stencil_mask_dirty = true; // Mark stencil as needing update
    }
}

void RmlNwgfxRenderer::SetTransform(const Rml::Matrix4f* transform)
{
    impl_->transform_enabled = transform != nullptr;
    impl_->transform = transform ? *transform : Rml::Matrix4f::Identity();

    // When transform is disabled, also disable stencil clipping
    // since scissor will work correctly without transforms
    if (!impl_->transform_enabled) {
        impl_->stencil_clip_active = false;
        impl_->stencil_ref = 0;
    }
}
