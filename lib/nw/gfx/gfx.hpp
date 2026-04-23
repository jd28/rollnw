#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace nw::gfx {

// ============================================================================
// == Utilities ===============================================================
// ============================================================================

template <typename T>
struct Handle {
    Handle() = default;
    constexpr size_t index() const noexcept { return idx_; }
    constexpr bool valid() const noexcept { return gen_ != 0; }
    constexpr auto operator<=>(const Handle& rhs) const noexcept = default;

    template <typename Handle, typename Impl>
    friend struct Pool;

private:
    Handle(uint16_t idx, uint16_t gen)
        : idx_(idx)
        , gen_(gen)
    {
    }

    uint16_t idx_ = 0;
    uint16_t gen_ = 0;
};

template <typename Type, typename Impl>
struct Pool {

    Impl* get(Handle<Type> hndl)
    {
        if (!hndl.valid()) { return nullptr; }
        if (hndl.idx_ >= storage_.size()) { return nullptr; }
        auto& payload = storage_[hndl.idx_];
        if (payload.gen != hndl.gen_) { return nullptr; }
        return &payload.value;
    }

    Handle<Type> insert(Impl&& impl)
    {
        uint16_t idx = 0;
        uint16_t gen = 1;
        if (free_list_head_ != 0xFFFF) {
            idx = free_list_head_;
            free_list_head_ = storage_[idx].free_list_next;
            storage_[idx].free_list_next = 0xFFFF;
            storage_[idx].value = std::forward<Impl>(impl);
            gen = storage_[idx].gen;
        } else {
            idx = static_cast<uint16_t>(storage_.size());
            auto& payload = storage_.emplace_back(ImplPayload{
                .value = std::forward<Impl>(impl),
                .gen = 1,
                .free_list_next = 0xFFFF,
            });
        }

        return {idx, gen};
    }

    void destroy(Handle<Type> hndl)
    {
        if (!hndl.valid()) { return; }
        if (hndl.idx_ >= storage_.size()) { return; }

        auto& payload = storage_[hndl.idx_];
        if (payload.gen != hndl.gen_) { return; }
        payload.value = Impl{};
        ++payload.gen;
        payload.free_list_next = free_list_head_;
        free_list_head_ = hndl.idx_;
    }

private:
    struct ImplPayload {
        Impl value;
        uint16_t gen = 1;
        uint16_t free_list_next = 0xFFFF;
    };

    std::vector<ImplPayload> storage_;
    uint16_t free_list_head_ = 0xFFFF;
};

// ============================================================================
// == Core ====================================================================
// ============================================================================

struct Core;

struct CoreConfig {
    const char* app_name = "arclight";
    bool enable_validation = true;
};

Core* create_core(const CoreConfig& desc);
void destroy_core(Core* core);

// ============================================================================
// == Context =================================================================
// ============================================================================

struct ContextDesc {
    SDL_Window* window = nullptr; // null = headless
    uint32_t width = 1280;
    uint32_t height = 720;
};

struct Buffer;
struct CommandList;
struct Context;
struct Pipeline;
struct RenderTarget;
struct Shader;
struct Texture;

Context* create_context(Core* core, const ContextDesc& desc);
void destroy_context(Context* ctx);
bool resize_context(Context* ctx, uint32_t width, uint32_t height);
void wait_idle(Context* ctx);

CommandList* begin_frame(Context* ctx); // returns nullptr if frame cannot begin (e.g. swapchain out of date)
void end_frame(Context* ctx);
bool capture_screenshot(Context* ctx, CommandList* cmd, const char* path);

enum class BufferUsage {
    Vertex,
    Index,
    Uniform,
    Storage,
    TransferSrc,
    TransferDst
};

struct BufferDesc {
    size_t size;
    BufferUsage usage;
    bool cpu_visible;
};

struct UniformSpan {
    Handle<Buffer> buffer;
    uint32_t offset = 0;
    uint32_t size = 0;
    void* data = nullptr;
};

struct VertexSpan {
    Handle<Buffer> buffer;
    uint32_t offset = 0;
    uint32_t size = 0;
    void* data = nullptr;
};

struct StorageSpan {
    Handle<Buffer> buffer;
    uint32_t offset = 0;
    uint32_t size = 0;

    StorageSpan() = default;
    StorageSpan(Handle<Buffer> buffer_) noexcept
        : buffer(buffer_)
    {
    }
    StorageSpan(Handle<Buffer> buffer_, uint32_t offset_, uint32_t size_) noexcept
        : buffer(buffer_)
        , offset(offset_)
        , size(size_)
    {
    }
};

Handle<Buffer> create_buffer(Context* ctx, const BufferDesc& desc);
void* map_buffer(Handle<Buffer> buffer);
void unmap_buffer(Handle<Buffer> buffer);
void destroy_buffer(Handle<Buffer> buffer);

enum class Fmt {
    RGBA8,
    RGBA8Srgb,
    RGBA16F,
    D32FS8, // D32_SFLOAT_S8_UINT - depth + stencil (cannot be used as sampled image)
    D32F,   // D32_SFLOAT - depth only, suitable for sampled image use
};

struct TextureDesc {
    uint32_t width;
    uint32_t height;
    uint32_t layers = 1;
    uint32_t mip_levels = 1;
    Fmt format;
    bool sampled = true;
    bool storage = false;
    bool render_target = false;
};

using BindlessTextureIndex = uint32_t;

struct TextureMipData {
    const void* data = nullptr;
    size_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

Handle<Texture> create_texture(Context* ctx, const TextureDesc& desc);
void destroy_texture(Context* ctx, Handle<Texture> handle);
bool upload_texture_rgba8(Context* ctx, Handle<Texture> handle, const void* data, size_t size);
bool upload_texture_rgba16f(Context* ctx, Handle<Texture> handle, const void* data, size_t size);
bool upload_texture_rgba16f_mips(Context* ctx, Handle<Texture> handle, const TextureMipData* levels, size_t count);

struct RenderTargetAttachment {
    Handle<Texture> texture;
    uint32_t mip_level = 0;
    uint32_t layer = 0;
    bool clear = true;
};

struct RenderTargetDesc {
    RenderTargetAttachment color[4];
    RenderTargetAttachment depth;
};

Handle<RenderTarget> create_render_target(Context* ctx, const RenderTargetDesc& desc);
void destroy_render_target(Context* ctx, Handle<RenderTarget> handle);

struct ShaderDesc {
    const void* code;
    size_t size;
};

Handle<Shader> create_shader(Context* ctx, const ShaderDesc& desc);
void destroy_shader(Context* ctx, Handle<Shader> handle);

enum class VertexFormat {
    Float2,
    Float3,
    Float4,
    UByte4Norm, // packed 4×uint8 normalized (e.g. colours, blend weights)
    UByte4,     // packed 4×uint8 unorm (e.g. blend indices)
};

struct VertexAttribute {
    uint32_t location; // shader input location
    uint32_t offset;   // byte offset within the vertex struct
    VertexFormat format;
};

enum class StencilOp {
    Keep,
    Zero,
    Replace,
    IncClamp,
    DecClamp,
    Invert,
};

enum class CompareOp {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    AlwaysPass,
};

enum class BlendMode {
    disabled,
    premultiplied_alpha,
    additive,
};

struct PipelineDesc {
    Handle<Shader> vs;
    Handle<Shader> fs;

    // Resource model. Defaults preserve the existing single-uniform + single-texture path.
    bool uses_draw_uniforms = true;
    bool uses_draw_uniforms2 = false;
    bool uses_storage_buffer = false;
    uint8_t storage_buffer_count = 0;
    bool uses_single_texture = true;
    bool uses_bindless_sampled_textures = false;

    // Depth state
    bool depth_test = true;
    bool depth_write = true;

    // Stencil state (disabled by default)
    bool stencil_test = false;
    uint8_t stencil_ref = 0;
    uint8_t stencil_mask = 0xFF;
    CompareOp stencil_compare = CompareOp::AlwaysPass;
    StencilOp stencil_fail = StencilOp::Keep;
    StencilOp stencil_pass = StencilOp::Keep;
    StencilOp stencil_depth_fail = StencilOp::Keep;

    // Color write mask (all channels enabled by default)
    bool has_color_attachment = true;
    bool color_write = true;
    BlendMode blend_mode = BlendMode::premultiplied_alpha;

    // Render target formats. By default windowed rendering keeps using the
    // swapchain format for color, while headless/custom targets can opt into
    // explicit attachment formats.
    bool use_swapchain_color_format = true;
    Fmt color_attachment_format = Fmt::RGBA8;
    Fmt depth_attachment_format = Fmt::D32FS8;

    // Vertex layout. When non-empty, overrides the built-in RmlUi vertex layout.
    // vertex_stride must be set alongside vertex_attributes.
    std::vector<VertexAttribute> vertex_attributes = {};
    uint32_t vertex_stride = 0;
};

struct ComputePipelineDesc {
    Handle<Shader> cs;

    // Resource model. Defaults preserve a simple uniform-only path and allow
    // one optional storage buffer for compute experimentation.
    bool uses_uniforms = true;
    bool uses_storage_buffer = false;
};

Handle<Pipeline> create_pipeline(Context* ctx, const PipelineDesc& desc);
Handle<Pipeline> create_compute_pipeline(Context* ctx, const ComputePipelineDesc& desc);
void destroy_pipeline(Context* ctx, Handle<Pipeline> handle);

void cmd_begin_render(CommandList* cmd, Handle<RenderTarget> target);
void cmd_end_render(CommandList* cmd);

void cmd_bind_pipeline(CommandList* cmd, Handle<Pipeline> pipeline);
void cmd_bind_vertex_buffer(CommandList* cmd, Handle<Buffer> buffer, uint32_t stride);
void cmd_bind_vertex_buffer(CommandList* cmd, Handle<Buffer> buffer, uint32_t stride, uint32_t offset);
void cmd_bind_index_buffer(CommandList* cmd, Handle<Buffer> buffer, uint32_t index_size);
void cmd_set_viewport(CommandList* cmd, float x, float y, float width, float height, float min_depth = 0.0f, float max_depth = 1.0f);
void cmd_set_scissor(CommandList* cmd, int32_t x, int32_t y, uint32_t width, uint32_t height);
void cmd_set_stencil_ref(CommandList* cmd, uint8_t ref);
void cmd_bind_uniform_texture(CommandList* cmd, Handle<Pipeline> pipeline, Handle<Buffer> uniform, Handle<Texture> texture = {});
void cmd_bind_resources(CommandList* cmd, Handle<Pipeline> pipeline, const UniformSpan& uniforms,
    StorageSpan storage0 = {}, StorageSpan storage1 = {}, const UniformSpan& uniforms2 = {});
void cmd_bind_compute_resources(CommandList* cmd, Handle<Pipeline> pipeline, const UniformSpan& uniforms = {}, Handle<Buffer> storage = {});

void cmd_draw(CommandList* cmd, uint32_t vertex_count, uint32_t instance_count = 1);
void cmd_draw_indexed(CommandList* cmd, uint32_t index_count, uint32_t instance_count = 1);
void cmd_dispatch(CommandList* cmd, uint32_t group_count_x, uint32_t group_count_y = 1, uint32_t group_count_z = 1);

UniformSpan allocate_uniform_span(Context* ctx, uint32_t size, uint32_t alignment = 256);
VertexSpan allocate_vertex_span(Context* ctx, uint32_t size, uint32_t alignment = 16);
BindlessTextureIndex get_bindless_texture_index(Context* ctx, Handle<Texture> texture);

}
