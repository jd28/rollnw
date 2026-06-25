#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <type_traits>
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
            storage_.emplace_back(ImplPayload{
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

struct ValidationReport {
    uint64_t warning_count = 0;
    uint64_t error_count = 0;
    std::string first_warning;
    std::string first_error;
};

Core* create_core(const CoreConfig& desc);
void destroy_core(Core* core);
void reset_validation_report(Core* core);
ValidationReport validation_report(Core* core);

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

struct FrameInfo {
    uint32_t frame_index = 0;
    uint64_t frame_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool headless = false;
    bool drawable = false;
};

struct CommandStats {
    uint64_t render_pass_count = 0;
    uint64_t pipeline_bind_count = 0;
    uint64_t pipeline_bind_skipped_count = 0;
    uint64_t vertex_buffer_bind_count = 0;
    uint64_t vertex_buffer_bind_skipped_count = 0;
    uint64_t index_buffer_bind_count = 0;
    uint64_t index_buffer_bind_skipped_count = 0;
    uint64_t resource_bind_count = 0;
    uint64_t resource_bind_skipped_count = 0;
    uint64_t descriptor_buffer_bind_count = 0;
    uint64_t descriptor_buffer_bind_skipped_count = 0;
    uint64_t draw_count = 0;
    uint64_t indexed_draw_count = 0;
    uint64_t nonindexed_draw_count = 0;
    uint64_t indirect_draw_call_count = 0;
    uint64_t draw_instance_count = 0;
    uint64_t draw_index_count = 0;
    uint64_t draw_vertex_count = 0;
    uint64_t dispatch_count = 0;
    uint64_t uniform_allocation_count = 0;
    uint64_t uniform_allocation_bytes = 0;
    uint64_t vertex_allocation_count = 0;
    uint64_t vertex_allocation_bytes = 0;
};

struct GpuTimerScope {
    uint32_t index = UINT32_MAX;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != UINT32_MAX; }
};

struct GpuTimerResult {
    const char* label = nullptr;
    float seconds = 0.0f;
    bool available = false;
};

bool get_frame_info(Context* ctx, FrameInfo& out) noexcept;
bool get_command_stats(CommandList* cmd, CommandStats& out) noexcept;
bool get_completed_gpu_timer_results(Context* ctx, std::vector<GpuTimerResult>& out) noexcept;
CommandList* begin_frame(Context* ctx); // returns nullptr if frame cannot begin (e.g. swapchain out of date)
void end_frame(Context* ctx);
bool capture_screenshot(Context* ctx, CommandList* cmd, const char* path);

enum class BufferUsage {
    Vertex,
    Index,
    Uniform,
    Storage,
    TransferSrc,
    TransferDst,
    Indirect
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

struct IndexedIndirectDrawCommand {
    uint32_t index_count = 0;
    uint32_t instance_count = 0;
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;
    uint32_t first_instance = 0;
};

struct IndexedIndirectDrawStats {
    uint64_t index_count = 0;
    uint64_t instance_count = 0;
};

struct IndirectDrawSpan {
    Handle<Buffer> buffer;
    uint32_t offset = 0;
    uint32_t size = 0;

    IndirectDrawSpan() = default;
    IndirectDrawSpan(Handle<Buffer> buffer_) noexcept
        : buffer(buffer_)
    {
    }
    IndirectDrawSpan(Handle<Buffer> buffer_, uint32_t offset_, uint32_t size_) noexcept
        : buffer(buffer_)
        , offset(offset_)
        , size(size_)
    {
    }
};

static_assert(std::is_standard_layout_v<IndexedIndirectDrawCommand>);
static_assert(sizeof(IndexedIndirectDrawCommand) == 20);

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
inline constexpr BindlessTextureIndex kInvalidBindlessTextureIndex = 0;

// Bindless sampled-texture slot 0 is reserved as the shader-visible "no texture"
// sentinel. Backends allocate real sampled texture slots from 1 upward.
[[nodiscard]] constexpr bool bindless_texture_index_valid(BindlessTextureIndex index) noexcept
{
    return index != kInvalidBindlessTextureIndex;
}

enum class TextureFilter {
    Linear,
    Nearest,
};

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

    bool operator==(const VertexAttribute&) const = default;
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
    CompareOp depth_compare = CompareOp::Less;

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

    bool operator==(const PipelineDesc&) const = default;
};

struct ComputePipelineDesc {
    Handle<Shader> cs;

    // Resource model. Defaults preserve a simple uniform-only path. Storage slots map
    // to the same registers as graphics pipelines (t2, t3, t5, t6, t7), so a buffer can
    // be written by compute (as RWStructuredBuffer) and read by a draw at the same slot.
    bool uses_uniforms = true;
    bool uses_storage_buffer = false;
    uint8_t storage_buffer_count = 0;
};

Handle<Pipeline> create_pipeline(Context* ctx, const PipelineDesc& desc);
Handle<Pipeline> create_compute_pipeline(Context* ctx, const ComputePipelineDesc& desc);
void destroy_pipeline(Context* ctx, Handle<Pipeline> handle);

enum class RenderLoadOp {
    clear,
    load,
};

void cmd_begin_render(CommandList* cmd, Handle<RenderTarget> target, RenderLoadOp color_load_op = RenderLoadOp::clear);
void cmd_end_render(CommandList* cmd);
GpuTimerScope cmd_begin_gpu_timer(CommandList* cmd, const char* label);
void cmd_end_gpu_timer(CommandList* cmd, GpuTimerScope scope);

void cmd_bind_pipeline(CommandList* cmd, Handle<Pipeline> pipeline);
void cmd_bind_vertex_buffer(CommandList* cmd, Handle<Buffer> buffer, uint32_t stride);
void cmd_bind_vertex_buffer(CommandList* cmd, Handle<Buffer> buffer, uint32_t stride, uint32_t offset);
void cmd_bind_index_buffer(CommandList* cmd, Handle<Buffer> buffer, uint32_t index_size);
void cmd_set_viewport(CommandList* cmd, float x, float y, float width, float height, float min_depth = 0.0f, float max_depth = 1.0f);
void cmd_set_scissor(CommandList* cmd, int32_t x, int32_t y, uint32_t width, uint32_t height);
void cmd_set_stencil_ref(CommandList* cmd, uint8_t ref);
void cmd_bind_uniform_texture(CommandList* cmd, Handle<Pipeline> pipeline, Handle<Buffer> uniform,
    Handle<Texture> texture = {}, TextureFilter filter = TextureFilter::Linear);
void cmd_bind_uniform_texture(CommandList* cmd, Handle<Pipeline> pipeline, const UniformSpan& uniform,
    Handle<Texture> texture = {}, TextureFilter filter = TextureFilter::Linear);
// Graphics storage slots map to shader storage registers t2, t3, t5, t6, t7.
// Register b4 is reserved for the optional secondary uniform span.
void cmd_bind_resources(CommandList* cmd, Handle<Pipeline> pipeline, const UniformSpan& uniforms,
    StorageSpan storage0 = {}, StorageSpan storage1 = {}, const UniformSpan& uniforms2 = {});
void cmd_bind_resources(CommandList* cmd, Handle<Pipeline> pipeline, const UniformSpan& uniforms,
    StorageSpan storage0, StorageSpan storage1, StorageSpan storage2, StorageSpan storage3, StorageSpan storage4,
    const UniformSpan& uniforms2);
void cmd_bind_compute_resources(CommandList* cmd, Handle<Pipeline> pipeline, const UniformSpan& uniforms = {},
    StorageSpan storage0 = {}, StorageSpan storage1 = {}, StorageSpan storage2 = {}, StorageSpan storage3 = {},
    StorageSpan storage4 = {});

void cmd_draw(CommandList* cmd, uint32_t vertex_count, uint32_t instance_count = 1);
void cmd_draw_indexed(CommandList* cmd, uint32_t index_count, uint32_t instance_count = 1);
void cmd_draw_indexed_base_instance(CommandList* cmd, uint32_t index_count, uint32_t first_instance,
    uint32_t instance_count = 1);
void cmd_draw_indexed_indirect(CommandList* cmd, IndirectDrawSpan commands, uint32_t draw_count,
    uint32_t stride = sizeof(IndexedIndirectDrawCommand), IndexedIndirectDrawStats stats = {});
// GPU-driven draw count: the number of commands to execute is read from count_buffer (a uint32
// at count_buffer.offset), clamped to max_draw_count. For a GPU cull/compaction pass that writes
// both the commands and the count.
void cmd_draw_indexed_indirect_count(CommandList* cmd, IndirectDrawSpan commands, StorageSpan count_buffer,
    uint32_t max_draw_count, uint32_t stride = sizeof(IndexedIndirectDrawCommand));
void cmd_dispatch(CommandList* cmd, uint32_t group_count_x, uint32_t group_count_y = 1, uint32_t group_count_z = 1);
// Make compute storage-buffer writes visible to subsequent draws (vertex/fragment shader
// reads and indirect-command reads) within the same command list.
void cmd_barrier_compute_to_graphics(CommandList* cmd);

UniformSpan allocate_uniform_span(Context* ctx, uint32_t size, uint32_t alignment = 256);
VertexSpan allocate_vertex_span(Context* ctx, uint32_t size, uint32_t alignment = 16);
BindlessTextureIndex get_bindless_texture_index(Context* ctx, Handle<Texture> texture);

}
