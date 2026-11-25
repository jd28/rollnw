#include "../../../util/profile.hpp"
#include "../../gfx.hpp"
#include "metal_internal.hpp"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <bit>
#include <bitset>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>

namespace nw::gfx {

namespace {

constexpr uint64_t kDescriptorRingSize = 2 * 1024 * 1024;
constexpr uint64_t kUniformRingSize = 32 * 1024 * 1024;
constexpr uint64_t kVertexRingSize = 16 * 1024 * 1024;
constexpr uint32_t kMaxTextureDimension2D = 16384;
constexpr uint32_t kMaxTextureArrayLayers = 2048;
constexpr double kNanosecondsToSeconds = 1.0e-9;

// Descriptor binding contract mirrored from vulkan_context.cpp:
//   buffer(0)  vertex buffer 0 (vertex stage only)
//   buffer(1)  primary uniform span        (b1, space0)
//   buffer(2)  storage slot 0              (t2 / u2, space0)
//   buffer(3)  storage slot 1              (t3 / u3, space0)
//   buffer(4)  secondary uniform span      (b4, space0)
//   buffer(5)  storage slot 2              (t5 / u5, space0)
//   buffer(6)  storage slot 3              (t6, space0)
//   buffer(7)  storage slot 4              (t7, space0)
//   buffer(8)  bindless texture/sampler argument buffer (b8, space1, fragment)
//   texture(0) + sampler(0) single-texture path (RmlUi 2D shaders, fragment)
uint32_t graphics_storage_binding_for_slot(uint32_t slot) noexcept
{
    return slot < 2 ? 2u + slot : 5u + (slot - 2u);
}

struct LegacyUiVertex {
    float position[2];
    uint32_t colour;
    float tex_coord[2];
};

inline uint64_t align_up(uint64_t val, uint64_t alignment) noexcept
{
    return (val + alignment - 1) & ~(alignment - 1);
}

MTLCompareFunction to_mtl_compare_op(CompareOp op) noexcept
{
    switch (op) {
    case CompareOp::Never:
        return MTLCompareFunctionNever;
    case CompareOp::Less:
        return MTLCompareFunctionLess;
    case CompareOp::Equal:
        return MTLCompareFunctionEqual;
    case CompareOp::LessEqual:
        return MTLCompareFunctionLessEqual;
    case CompareOp::Greater:
        return MTLCompareFunctionGreater;
    case CompareOp::NotEqual:
        return MTLCompareFunctionNotEqual;
    case CompareOp::GreaterEqual:
        return MTLCompareFunctionGreaterEqual;
    case CompareOp::AlwaysPass:
        return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionAlways;
}

MTLStencilOperation to_mtl_stencil_op(StencilOp op) noexcept
{
    switch (op) {
    case StencilOp::Keep:
        return MTLStencilOperationKeep;
    case StencilOp::Zero:
        return MTLStencilOperationZero;
    case StencilOp::Replace:
        return MTLStencilOperationReplace;
    case StencilOp::IncClamp:
        return MTLStencilOperationIncrementClamp;
    case StencilOp::DecClamp:
        return MTLStencilOperationDecrementClamp;
    case StencilOp::Invert:
        return MTLStencilOperationInvert;
    }
    return MTLStencilOperationKeep;
}

MTLVertexFormat to_mtl_vertex_format(VertexFormat fmt) noexcept
{
    switch (fmt) {
    case VertexFormat::Float2:
        return MTLVertexFormatFloat2;
    case VertexFormat::Float3:
        return MTLVertexFormatFloat3;
    case VertexFormat::Float4:
        return MTLVertexFormatFloat4;
    case VertexFormat::UByte4Norm:
        return MTLVertexFormatUChar4Normalized;
    case VertexFormat::UByte4:
        return MTLVertexFormatUChar4;
    }
    return MTLVertexFormatInvalid;
}

void check_command_buffer_error(MetalCore* core, id<MTLCommandBuffer> cb, const char* where) noexcept
{
    if (!core || !cb) {
        return;
    }
    if (cb.status == MTLCommandBufferStatusError) {
        NSError* err = cb.error;
        char buf[512];
        std::snprintf(buf, sizeof(buf), "Metal command buffer failed (%s): %s",
            where, err ? err.localizedDescription.UTF8String : "unknown error");
        record_metal_validation_error(core, buf);
    }
}

void write_bindless_texture_entry(MetalContext* ctx, uint32_t slot, id<MTLTexture> texture) noexcept
{
    if (!ctx || !ctx->bindless_texture_table_mapped
        || !bindless_texture_index_valid(slot)
        || slot >= ctx->bindless_texture_capacity) {
        return;
    }
    if (texture) {
        ctx->bindless_texture_table_mapped[slot] = texture.gpuResourceID;
    } else {
        ctx->bindless_texture_table_mapped[slot] = MTLResourceID{0};
    }
}

void write_bindless_sampler_entry(MetalContext* ctx) noexcept
{
    if (!ctx || !ctx->bindless_texture_table_mapped || !ctx->linear_sampler) {
        return;
    }
    ctx->bindless_texture_table_mapped[ctx->bindless_texture_capacity] = ctx->linear_sampler.gpuResourceID;
}

void remove_bindless_resident(MetalContext* ctx, MetalTexture* tex) noexcept
{
    if (!ctx || !tex || tex->resident_index == UINT32_MAX) {
        return;
    }
    const uint32_t idx = tex->resident_index;
    const size_t last = ctx->bindless_resident_textures.size() - 1;
    if (idx < last) {
        ctx->bindless_resident_textures[idx] = ctx->bindless_resident_textures[last];
        const Handle<Texture> moved_handle = ctx->bindless_resident_handles[last];
        ctx->bindless_resident_handles[idx] = moved_handle;
        if (auto* moved_tex = ctx->texture_pool_.get(moved_handle)) {
            moved_tex->resident_index = idx;
        }
    }
    ctx->bindless_resident_textures.pop_back();
    ctx->bindless_resident_handles.pop_back();
    tex->resident_index = UINT32_MAX;
}

bool render_target_desc_supported(MetalContext* ctx, const RenderTargetDesc& desc) noexcept
{
    const auto* color = ctx->texture_pool_.get(desc.color.texture);
    const auto* depth = ctx->texture_pool_.get(desc.depth.texture);
    const auto valid_attachment = [](const MetalTexture* image, bool expect_depth) {
        return image && image->render_target && image->layers == 1 && image->mip_levels == 1
            && is_mtl_depth_format(image->format) == expect_depth;
    };
    if ((!color && !depth)
        || (desc.color.texture.valid() && !valid_attachment(color, false))
        || (desc.depth.texture.valid() && !valid_attachment(depth, true))
        || (color && depth && (color->width != depth->width || color->height != depth->height))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "RenderTargetDesc requires live single-level color/depth attachments of matching extent");
        return false;
    }
    return true;
}

void end_compute_encoder_if_active(MetalCommandList* cmd) noexcept
{
    if (cmd && cmd->compute_encoder) {
        [cmd->compute_encoder endEncoding];
        cmd->compute_encoder = nil;
    }
}

void sample_stage_boundary_timestamp(MetalCommandList* cmd, uint32_t sample_index, bool at_end) noexcept
{
    if (!cmd || !cmd->context || !cmd->command_buffer) {
        return;
    }
    auto& frame = cmd->context->frames[cmd->pool_index];
    if (!frame.timestamp_sample_buffer) {
        return;
    }
    end_compute_encoder_if_active(cmd);
    MTLBlitPassDescriptor* desc = [MTLBlitPassDescriptor blitPassDescriptor];
    desc.sampleBufferAttachments[0].sampleBuffer = frame.timestamp_sample_buffer;
    if (at_end) {
        desc.sampleBufferAttachments[0].startOfEncoderSampleIndex = MTLCounterDontSample;
        desc.sampleBufferAttachments[0].endOfEncoderSampleIndex = sample_index;
    } else {
        desc.sampleBufferAttachments[0].startOfEncoderSampleIndex = sample_index;
        desc.sampleBufferAttachments[0].endOfEncoderSampleIndex = MTLCounterDontSample;
    }
    id<MTLBlitCommandEncoder> blit = [cmd->command_buffer blitCommandEncoderWithDescriptor:desc];
    if (cmd->context->timestamp_scratch_buffer) {
        [blit fillBuffer:cmd->context->timestamp_scratch_buffer range:NSMakeRange(0, 4) value:0];
    }
    [blit endEncoding];
}

void collect_completed_gpu_timer_results(MetalContext* ctx, const MetalPerFrame& frame)
{
    ctx->completed_gpu_timer_results.clear();
    if (!ctx->gpu_timers_supported || !frame.timestamp_sample_buffer) {
        return;
    }

    const auto& cmds = frame.cmds;
    if (cmds.timestamp_sample_count == 0 || cmds.gpu_timer_count == 0) {
        return;
    }

    const uint32_t sample_count = std::min(cmds.timestamp_sample_count, kMetalMaxGpuTimestampSamplesPerFrame);
    NSData* resolved = [frame.timestamp_sample_buffer resolveCounterRange:NSMakeRange(0, sample_count)];
    if (!resolved || resolved.length < sample_count * sizeof(MTLCounterResultTimestamp)) {
        return;
    }

    const auto* samples = static_cast<const MTLCounterResultTimestamp*>(resolved.bytes);
    ctx->completed_gpu_timer_results.reserve(cmds.gpu_timer_count);
    for (uint32_t i = 0; i < cmds.gpu_timer_count; ++i) {
        const auto& timer = cmds.gpu_timers[i];
        if (!timer.ended || timer.start_sample >= sample_count || timer.end_sample >= sample_count) {
            continue;
        }
        const uint64_t start_ts = samples[timer.start_sample].timestamp;
        const uint64_t end_ts = samples[timer.end_sample].timestamp;
        const bool valid = start_ts != MTLCounterErrorValue
            && end_ts != MTLCounterErrorValue
            && end_ts >= start_ts;
        GpuTimerResult result{
            .label = timer.label,
            .seconds = 0.0f,
            .available = valid,
        };
        if (valid) {
            // Metal timestamp counter values on Apple Silicon are reported in nanoseconds.
            result.seconds = static_cast<float>(static_cast<double>(end_ts - start_ts) * kNanosecondsToSeconds);
        }
        ctx->completed_gpu_timer_results.push_back(result);
    }
}

void report_failed_bind(MetalCommandList* cmd, const char* kind) noexcept
{
    if (cmd && cmd->context && cmd->context->core && cmd->context->core->validation_enabled) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "gfx: invalid %s bind; following draws are rejected until a valid rebind", kind);
    }
}

bool uniform_span_matches(const UniformSpan& lhs, const UniformSpan& rhs) noexcept
{
    return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.size == rhs.size;
}

bool storage_span_matches(const StorageSpan& lhs, const StorageSpan& rhs) noexcept
{
    return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.size == rhs.size;
}

bool resource_bind_matches(
    const MetalCommandList::BoundResources& lhs,
    const MetalCommandList::BoundResources& rhs) noexcept
{
    return lhs.valid
        && rhs.valid
        && lhs.compute == rhs.compute
        && lhs.pipeline == rhs.pipeline
        && uniform_span_matches(lhs.uniforms, rhs.uniforms)
        && storage_span_matches(lhs.storages[0], rhs.storages[0])
        && storage_span_matches(lhs.storages[1], rhs.storages[1])
        && storage_span_matches(lhs.storages[2], rhs.storages[2])
        && storage_span_matches(lhs.storages[3], rhs.storages[3])
        && storage_span_matches(lhs.storages[4], rhs.storages[4])
        && uniform_span_matches(lhs.uniforms2, rhs.uniforms2)
        && lhs.texture == rhs.texture
        && lhs.texture_filter == rhs.texture_filter;
}

void fail_resource_bind(MetalCommandList* cmd) noexcept
{
    if (!cmd) {
        return;
    }
    cmd->bound_resources = {};
    cmd->resource_bind_failed = true;
    ++cmd->stats.resource_bind_failure_count;
    report_failed_bind(cmd, "resources");
}

void ensure_bindless_bound_in_pass(MetalCommandList* cmd) noexcept
{
    if (!cmd || !cmd->context || !cmd->render_encoder || cmd->bindless_bound_in_pass) {
        return;
    }
    auto* ctx = cmd->context;
    if (ctx->bindless_texture_table) {
        [cmd->render_encoder setFragmentBuffer:ctx->bindless_texture_table
                                        offset:0
                                       atIndex:kMetalBindlessBufferIndex];
    }
    if (!ctx->bindless_resident_textures.empty()) {
        [cmd->render_encoder useResources:ctx->bindless_resident_textures.data()
                                    count:ctx->bindless_resident_textures.size()
                                    usage:MTLResourceUsageRead
                                   stages:MTLRenderStageFragment];
    }
    cmd->bindless_bound_in_pass = true;
}

bool bind_pipeline_resources(MetalCommandList* cmd, MetalPipeline* pipeline,
    const MetalBuffer* uniform, uint32_t uniform_offset, uint32_t uniform_size,
    const StorageSpan& storage0, const StorageSpan& storage1, const StorageSpan& storage2,
    const StorageSpan& storage3, const StorageSpan& storage4, const MetalTexture* texture,
    TextureFilter texture_filter, const MetalBuffer* uniform2, uint32_t uniform2_offset, uint32_t uniform2_size)
{
    auto* ctx = cmd ? cmd->context : nullptr;
    if (!cmd || !ctx || !pipeline || pipeline->core != ctx->core) {
        return false;
    }

    if (pipeline->uses_draw_uniforms
        && (!uniform || uniform->core != ctx->core || uniform_offset > uniform->size || uniform_size == 0
            || uniform_size > uniform->size - uniform_offset)) {
        return false;
    }
    if (pipeline->uses_draw_uniforms2
        && (!uniform2 || uniform2->core != ctx->core || uniform2_offset > uniform2->size || uniform2_size == 0
            || uniform2_size > uniform2->size - uniform2_offset)) {
        return false;
    }

    const StorageSpan storages[5] = {storage0, storage1, storage2, storage3, storage4};
    const MetalBuffer* storage_buffers[5] = {};
    uint64_t storage_ranges[5] = {};
    if (pipeline->uses_storage_buffer) {
        for (uint32_t i = 0; i < pipeline->storage_buffer_count; ++i) {
            storage_buffers[i] = g_metal_buffer_pool.get(storages[i].buffer);
            if (!storage_buffers[i] || storage_buffers[i]->core != ctx->core || storages[i].offset > storage_buffers[i]->size) {
                return false;
            }
            storage_ranges[i] = storages[i].size != 0
                ? storages[i].size
                : storage_buffers[i]->size - storages[i].offset;
            if (storage_ranges[i] == 0
                || storage_ranges[i] > storage_buffers[i]->size - storages[i].offset) {
                return false;
            }
        }
    }

    const bool uses_set0 = pipeline->uses_draw_uniforms
        || pipeline->uses_draw_uniforms2
        || pipeline->uses_storage_buffer;
    const bool uses_texture_descriptor = pipeline->uses_single_texture && texture && texture->texture;
    if (pipeline->uses_single_texture && !uses_texture_descriptor) {
        return false;
    }

    // Track logical descriptor-ring demand for CommandStats parity and capacity tests.
    uint64_t& ring_offset = ctx->descriptor_ring_offset[cmd->pool_index];
    const uint64_t allocation_start = ring_offset;
    uint64_t next_ring_offset = ring_offset;
    uint64_t descriptor_allocation_count = 0;
    if (uses_set0) {
        next_ring_offset = align_up(next_ring_offset, 64) + 64;
        ++descriptor_allocation_count;
    }
    if (uses_texture_descriptor) {
        next_ring_offset = align_up(next_ring_offset, 64) + 64;
        ++descriptor_allocation_count;
    }
    cmd->stats.descriptor_ring_required_bytes = std::max<uint64_t>(
        cmd->stats.descriptor_ring_required_bytes, next_ring_offset);
    if (next_ring_offset > kDescriptorRingSize) {
        ++cmd->stats.descriptor_allocation_failure_count;
        return false;
    }
    ring_offset = next_ring_offset;
    cmd->stats.descriptor_allocation_count += descriptor_allocation_count;
    cmd->stats.descriptor_allocation_bytes += next_ring_offset - allocation_start;

    if (pipeline->uses_storage_buffer) {
        for (uint32_t i = 0; i < pipeline->storage_buffer_count; ++i) {
            const uint64_t address = storage_buffers[i]->gpu_address + storages[i].offset;
            auto& key = cmd->storage_descriptor_keys[i];
            if (key.buffer != storages[i].buffer || key.address != address || key.range != storage_ranges[i]) {
                key = {storages[i].buffer, address, storage_ranges[i]};
                ++cmd->stats.storage_descriptor_encode_count;
            } else {
                ++cmd->stats.storage_descriptor_reuse_count;
            }
        }
    }

    if (!pipeline->is_compute && cmd->render_encoder) {
        if (pipeline->uses_draw_uniforms) {
            [cmd->render_encoder setVertexBuffer:uniform->buffer offset:uniform_offset atIndex:1];
            [cmd->render_encoder setFragmentBuffer:uniform->buffer offset:uniform_offset atIndex:1];
        }
        if (pipeline->uses_draw_uniforms2) {
            [cmd->render_encoder setVertexBuffer:uniform2->buffer offset:uniform2_offset atIndex:4];
            [cmd->render_encoder setFragmentBuffer:uniform2->buffer offset:uniform2_offset atIndex:4];
        }
        if (pipeline->uses_storage_buffer) {
            for (uint32_t i = 0; i < pipeline->storage_buffer_count; ++i) {
                const uint32_t slot_index = graphics_storage_binding_for_slot(i);
                [cmd->render_encoder setVertexBuffer:storage_buffers[i]->buffer
                                              offset:storages[i].offset
                                             atIndex:slot_index];
                [cmd->render_encoder setFragmentBuffer:storage_buffers[i]->buffer
                                                offset:storages[i].offset
                                               atIndex:slot_index];
            }
        }
        if (pipeline->uses_single_texture) {
            [cmd->render_encoder setFragmentTexture:texture->texture atIndex:0];
            id<MTLSamplerState> sampler = (texture_filter == TextureFilter::Nearest)
                ? ctx->nearest_sampler
                : ctx->linear_sampler;
            [cmd->render_encoder setFragmentSamplerState:sampler atIndex:0];
        } else if (pipeline->uses_bindless_sampled_textures) {
            ensure_bindless_bound_in_pass(cmd);
        }
    }

    return true;
}

struct UploadObservation {
    MetalContext* ctx;
    uint64_t start = SDL_GetTicksNS();
    uint64_t textures = 1;
    uint64_t mips = 0;
    uint64_t bytes = 0;
    uint64_t largest_payload_bytes = 0;
    bool success = false;

    ~UploadObservation()
    {
        const double seconds = static_cast<double>(SDL_GetTicksNS() - start) * kNanosecondsToSeconds;
        auto& stats = ctx->resource_stats;
        ++stats.upload_batch_count;
        stats.upload_seconds += seconds;
        if (success) {
            stats.upload_texture_count += textures;
            stats.upload_mip_count += mips;
            stats.upload_bytes += bytes;
            stats.largest_payload_bytes = std::max(stats.largest_payload_bytes, largest_payload_bytes);
        } else {
            ++stats.upload_failure_count;
        }
        if (ctx->trace_resources) {
            SDL_Log("gfx-resource upload start_ns=%llu textures=%llu mips=%llu bytes=%llu seconds=%.9f success=%d",
                static_cast<unsigned long long>(start), static_cast<unsigned long long>(textures),
                static_cast<unsigned long long>(mips), static_cast<unsigned long long>(bytes), seconds, success);
        }
    }
};

struct MetalUploadRegion {
    size_t buffer_offset = 0;
    uint32_t mip_level = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    size_t bytes_per_row = 0;
    size_t bytes_per_image = 0;
};

bool prepare_texture_uploads(MetalContext* ctx, std::span<const TextureUpload> uploads,
    std::vector<MetalUploadRegion>& regions, size_t& staging_bytes, UploadObservation& observation)
{
    std::bitset<Pool<Texture, MetalTexture>::max_capacity()> seen;
    for (const auto& upload : uploads) {
        const auto* tex = ctx->texture_pool_.get(upload.texture);
        if (!tex || !tex->texture || tex->layers != 1 || upload.mips.empty() || !upload.mips.data()
            || (upload.mips.size() != 1 && upload.mips.size() != tex->mip_levels)) {
            return false;
        }
        if (seen.test(upload.texture.index())) {
            return false;
        }
        seen.set(upload.texture.index());

        size_t bytes_per_pixel = 0;
        switch (tex->format) {
        case MTLPixelFormatRGBA8Unorm:
        case MTLPixelFormatRGBA8Unorm_sRGB:
            bytes_per_pixel = 4;
            break;
        case MTLPixelFormatRGBA16Float:
            bytes_per_pixel = 8;
            break;
        default:
            return false;
        }

        if (tex->width == 0 || tex->height == 0 || tex->mip_levels == 0
            || tex->mip_levels > static_cast<uint32_t>(std::bit_width(std::max(tex->width, tex->height)))) {
            return false;
        }

        uint32_t width = tex->width;
        uint32_t height = tex->height;
        uint64_t texture_bytes = 0;
        for (size_t level = 0; level < upload.mips.size(); ++level) {
            const auto& mip = upload.mips[level];
            if (!mip.data || mip.width != width || mip.height != height) {
                return false;
            }
            constexpr size_t limit = std::numeric_limits<size_t>::max();
            if (width > limit / height || static_cast<size_t>(width) * height > limit / bytes_per_pixel) {
                return false;
            }
            const size_t bytes = static_cast<size_t>(width) * height * bytes_per_pixel;
            // 16-byte alignment satisfies Metal's pixel-size offset alignment for both RGBA8 (4B) and RGBA16F (8B).
            if (mip.size != bytes || staging_bytes > limit - 15) {
                return false;
            }
            const size_t offset = (staging_bytes + 15) & ~size_t{15};
            if (bytes > limit - offset) {
                return false;
            }
            regions.push_back(MetalUploadRegion{
                .buffer_offset = offset,
                .mip_level = static_cast<uint32_t>(level),
                .width = width,
                .height = height,
                .bytes_per_row = static_cast<size_t>(width) * bytes_per_pixel,
                .bytes_per_image = bytes,
            });
            staging_bytes = offset + bytes;
            texture_bytes += bytes;
            width = std::max(1u, width / 2u);
            height = std::max(1u, height / 2u);
        }
        observation.bytes += texture_bytes;
        observation.mips += tex->mip_levels;
        observation.largest_payload_bytes = std::max(observation.largest_payload_bytes, texture_bytes);
    }
    return true;
}

bool upload_texture_base(Context* ctx, Handle<Texture> handle, const void* data, size_t size, bool half_float)
{
    auto* context = as_metal(ctx);
    const auto* tex = context ? context->texture_pool_.get(handle) : nullptr;
    if (!tex
        || (half_float
                ? tex->format != MTLPixelFormatRGBA16Float
                : (tex->format != MTLPixelFormatRGBA8Unorm && tex->format != MTLPixelFormatRGBA8Unorm_sRGB))) {
        return false;
    }
    const TextureMipData mip{data, size, tex->width, tex->height};
    const TextureUpload upload{handle, {&mip, 1}};
    return upload_textures(ctx, {&upload, 1});
}

void create_swapchain_depth_target(MetalContext* ctx)
{
    TextureDesc depth_desc = {
        .width = ctx->width,
        .height = ctx->height,
        .format = Fmt::D32FS8,
        .sampled = false,
        .render_target = true,
    };

    MetalTexture color_placeholder{};
    color_placeholder.texture = nil; // Resolved dynamically from current_drawable.texture
    color_placeholder.format = ctx->swapchain_format;
    color_placeholder.width = ctx->width;
    color_placeholder.height = ctx->height;
    color_placeholder.layers = 1;
    color_placeholder.mip_levels = 1;
    color_placeholder.owned = false;
    color_placeholder.render_target = true;

    RenderTargetDesc rt_desc{};
    rt_desc.color.texture = ctx->texture_pool_.insert(std::move(color_placeholder));
    rt_desc.depth.texture = create_texture(as_gfx(ctx), depth_desc);
    ctx->swapchain_depth_texture = rt_desc.depth.texture;
    ctx->swapchain_render_target = create_render_target(as_gfx(ctx), rt_desc);
}

void reset_command_list_for_frame(MetalContext* ctx, uint32_t frame_index, id<MTLCommandBuffer> cb)
{
    MetalCommandList& cmds = ctx->frames[frame_index].cmds;
    cmds.context = ctx;
    cmds.pool_index = frame_index;
    cmds.command_buffer = cb;
    cmds.render_encoder = nil;
    cmds.compute_encoder = nil;
    cmds.active_render_pass_descriptor = nil;
    cmds.current_render_target = {};
    cmds.render_target_width = 0;
    cmds.render_target_height = 0;
    cmds.stats = {};
    cmds.bound_pipeline = {};
    cmds.bound_vertex_buffer = {};
    cmds.bound_vertex_stride = 0;
    cmds.bound_vertex_offset = 0;
    cmds.bound_index_buffer = {};
    cmds.bound_index_size = 0;
    cmds.stencil_ref_set = false;
    cmds.dynamic_stencil_ref = 0;
    cmds.bindless_bound_in_pass = false;
    cmds.timestamp_sample_count = 0;
    cmds.gpu_timer_count = 0;
    cmds.gpu_timers = {};
    cmds.bound_resources = {};
    cmds.storage_descriptor_keys = {};
    cmds.pipeline_bind_failed = false;
    cmds.vertex_bind_failed = false;
    cmds.index_bind_failed = false;
    cmds.resource_bind_failed = false;
    cmds.recording = true;
    cmds.in_render_pass = false;
    cmds.rendered = false;
}

} // namespace

Pool<Buffer, MetalBuffer> g_metal_buffer_pool;
Pool<Shader, MetalShader> g_metal_shader_pool;
Pool<Pipeline, MetalPipeline> g_metal_pipeline_pool;

// ============================================================================
// == Buffers =================================================================
// ============================================================================

Handle<Buffer> create_buffer(Context* ctx_ptr, const BufferDesc& desc)
{
    @autoreleasepool {
        auto* ctx = as_metal(ctx_ptr);
        if (!ctx || !ctx->core || !ctx->core->device || desc.size == 0) {
            return {};
        }

        const MTLResourceOptions options = desc.cpu_visible
            ? MTLResourceStorageModeShared
            : MTLResourceStorageModePrivate;

        id<MTLBuffer> mtl_buf = [ctx->core->device newBufferWithLength:desc.size options:options];
        if (!mtl_buf) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Metal newBufferWithLength(%zu) failed", desc.size);
            return {};
        }

        MetalBuffer buf{};
        buf.core = ctx->core;
        buf.buffer = mtl_buf;
        buf.size = desc.size;
        buf.cpu_visible = desc.cpu_visible;
        buf.mapped = desc.cpu_visible ? [mtl_buf contents] : nullptr;
        buf.gpu_address = mtl_buf.gpuAddress;

        const auto handle = g_metal_buffer_pool.insert(std::move(buf));
        if (!handle.valid()) {
            // Pool exhausted; ARC releases mtl_buf when leaving scope.
            return {};
        }
        ++ctx->core->live_buffer_allocation_count;
        ctx->core->live_buffer_allocation_bytes += desc.size;
        return handle;
    }
}

void* map_buffer(Handle<Buffer> handle)
{
    auto* buf = g_metal_buffer_pool.get(handle);
    if (!buf || !buf->buffer) {
        return nullptr;
    }
    return buf->mapped;
}

void unmap_buffer(Handle<Buffer> handle)
{
    (void)handle;
    // MTLResourceStorageModeShared on Apple Silicon unified memory stays persistently coherent.
}

void destroy_buffer(Handle<Buffer> handle)
{
    @autoreleasepool {
        auto* buf = g_metal_buffer_pool.get(handle);
        if (!buf) {
            return;
        }
        if (buf->core && buf->buffer) {
            if (buf->core->live_buffer_allocation_count > 0) {
                --buf->core->live_buffer_allocation_count;
            }
            if (buf->core->live_buffer_allocation_bytes >= buf->size) {
                buf->core->live_buffer_allocation_bytes -= buf->size;
            }
        }
        g_metal_buffer_pool.destroy(handle);
    }
}

// ============================================================================
// == Shaders & Pipelines =====================================================
// ============================================================================

Handle<Shader> create_shader(Context* ctx_ptr, const ShaderDesc& desc)
{
    @autoreleasepool {
        auto* ctx = as_metal(ctx_ptr);
        if (!ctx || !ctx->core || !ctx->core->device || !desc.code || desc.size == 0) {
            return {};
        }

        NSString* source = [[NSString alloc] initWithBytes:desc.code
                                                    length:desc.size
                                                  encoding:NSUTF8StringEncoding];
        if (!source) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Metal create_shader: invalid UTF-8 MSL source");
            return {};
        }

        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
        if (@available(macOS 14.0, iOS 17.0, *)) {
            options.languageVersion = MTLLanguageVersion3_1;
        } else {
            options.languageVersion = MTLLanguageVersion3_0;
        }

        NSError* error = nil;
        id<MTLLibrary> library = [ctx->core->device newLibraryWithSource:source options:options error:&error];
        if (!library) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Metal shader compile failed: %s",
                error ? error.localizedDescription.UTF8String : "unknown error");
            return {};
        }

        // Slang renames HLSL entry point `main` to `main_0` for Metal; handwritten
        // RmlUi MSL shaders may use `main_0` or a custom entry name, so fall back to
        // the library's single exported function if `main_0` is absent.
        id<MTLFunction> fn = [library newFunctionWithName:@"main_0"];
        if (!fn) {
            fn = [library newFunctionWithName:@"main"];
        }
        if (!fn && library.functionNames.count > 0) {
            fn = [library newFunctionWithName:library.functionNames[0]];
        }
        if (!fn) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Metal shader library contains no entry point function");
            return {};
        }

        MetalShader shader{};
        shader.core = ctx->core;
        shader.library = library;
        shader.function = fn;
        shader.threadgroup_size[0] = std::max<uint32_t>(1u, desc.threadgroup_size[0]);
        shader.threadgroup_size[1] = std::max<uint32_t>(1u, desc.threadgroup_size[1]);
        shader.threadgroup_size[2] = std::max<uint32_t>(1u, desc.threadgroup_size[2]);

        return g_metal_shader_pool.insert(std::move(shader));
    }
}

void destroy_shader(Context* ctx, Handle<Shader> handle)
{
    @autoreleasepool {
        (void)ctx;
        if (!g_metal_shader_pool.get(handle)) {
            return;
        }
        g_metal_shader_pool.destroy(handle);
    }
}

Handle<Pipeline> create_pipeline(Context* ctx_ptr, const PipelineDesc& desc)
{
    @autoreleasepool {
        MTLCullMode cull_mode = MTLCullModeNone;
        switch (desc.cull_mode) {
        case CullMode::none:
            cull_mode = MTLCullModeNone;
            break;
        case CullMode::back:
            cull_mode = MTLCullModeBack;
            break;
        case CullMode::front:
            cull_mode = MTLCullModeFront;
            break;
        default:
            return {};
        }

        auto* ctx = as_metal(ctx_ptr);
        auto* vs = g_metal_shader_pool.get(desc.vs);
        auto* fs = g_metal_shader_pool.get(desc.fs);
        if (!ctx || !ctx->core || !vs || !fs || !vs->function || !fs->function) {
            return {};
        }
        if (desc.uses_single_texture && desc.uses_bindless_sampled_textures) {
            return {};
        }

        MTLRenderPipelineDescriptor* pipe_desc = [[MTLRenderPipelineDescriptor alloc] init];
        pipe_desc.vertexFunction = vs->function;
        pipe_desc.fragmentFunction = fs->function;
        pipe_desc.rasterSampleCount = 1;

        MTLVertexDescriptor* vdesc = [MTLVertexDescriptor vertexDescriptor];
        if (!desc.vertex_attributes.empty()) {
            vdesc.layouts[0].stride = desc.vertex_stride;
            vdesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
            vdesc.layouts[0].stepRate = 1;
            for (const auto& attr : desc.vertex_attributes) {
                const MTLVertexFormat mtl_fmt = to_mtl_vertex_format(attr.format);
                if (mtl_fmt == MTLVertexFormatInvalid) {
                    return {};
                }
                vdesc.attributes[attr.location].format = mtl_fmt;
                vdesc.attributes[attr.location].offset = attr.offset;
                vdesc.attributes[attr.location].bufferIndex = 0;
            }
        } else {
            vdesc.layouts[0].stride = sizeof(LegacyUiVertex);
            vdesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
            vdesc.layouts[0].stepRate = 1;
            vdesc.attributes[0].format = MTLVertexFormatFloat2;
            vdesc.attributes[0].offset = offsetof(LegacyUiVertex, position);
            vdesc.attributes[0].bufferIndex = 0;
            vdesc.attributes[1].format = MTLVertexFormatUChar4Normalized;
            vdesc.attributes[1].offset = offsetof(LegacyUiVertex, colour);
            vdesc.attributes[1].bufferIndex = 0;
            vdesc.attributes[2].format = MTLVertexFormatFloat2;
            vdesc.attributes[2].offset = offsetof(LegacyUiVertex, tex_coord);
            vdesc.attributes[2].bufferIndex = 0;
        }
        pipe_desc.vertexDescriptor = vdesc;

        if (desc.has_color_attachment) {
            const MTLPixelFormat color_format = (!ctx->headless && desc.use_swapchain_color_format)
                ? ctx->swapchain_format
                : to_mtl_format(desc.color_attachment_format);
            if (color_format == MTLPixelFormatInvalid) {
                return {};
            }
            MTLRenderPipelineColorAttachmentDescriptor* color_att = pipe_desc.colorAttachments[0];
            color_att.pixelFormat = color_format;
            color_att.writeMask = desc.color_write ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
            if (desc.color_write && desc.blend_mode != BlendMode::disabled) {
                color_att.blendingEnabled = YES;
                color_att.rgbBlendOperation = MTLBlendOperationAdd;
                color_att.alphaBlendOperation = MTLBlendOperationAdd;
                color_att.sourceRGBBlendFactor = MTLBlendFactorOne;
                color_att.sourceAlphaBlendFactor = MTLBlendFactorOne;
                if (desc.blend_mode == BlendMode::additive) {
                    color_att.destinationRGBBlendFactor = MTLBlendFactorOne;
                    color_att.destinationAlphaBlendFactor = MTLBlendFactorOne;
                } else {
                    color_att.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                    color_att.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                }
            } else {
                color_att.blendingEnabled = NO;
            }
        }

        const MTLPixelFormat depth_format = to_mtl_format(desc.depth_attachment_format);
        if (depth_format != MTLPixelFormatInvalid) {
            pipe_desc.depthAttachmentPixelFormat = depth_format;
            if (has_mtl_stencil_component(depth_format)) {
                pipe_desc.stencilAttachmentPixelFormat = depth_format;
            }
        }

        NSError* error = nil;
        id<MTLRenderPipelineState> render_state =
            [ctx->core->device newRenderPipelineStateWithDescriptor:pipe_desc error:&error];
        if (!render_state) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Metal newRenderPipelineState failed: %s",
                error ? error.localizedDescription.UTF8String : "unknown error");
            record_metal_validation_error(ctx->core,
                error ? error.localizedDescription.UTF8String : "newRenderPipelineState failed");
            return {};
        }

        MTLDepthStencilDescriptor* ds_desc = [[MTLDepthStencilDescriptor alloc] init];
        ds_desc.depthCompareFunction = desc.depth_test
            ? to_mtl_compare_op(desc.depth_compare)
            : MTLCompareFunctionAlways;
        ds_desc.depthWriteEnabled = desc.depth_write ? YES : NO;
        if (desc.stencil_test) {
            MTLStencilDescriptor* stencil = [[MTLStencilDescriptor alloc] init];
            stencil.stencilCompareFunction = to_mtl_compare_op(desc.stencil_compare);
            stencil.stencilFailureOperation = to_mtl_stencil_op(desc.stencil_fail);
            stencil.depthFailureOperation = to_mtl_stencil_op(desc.stencil_depth_fail);
            stencil.depthStencilPassOperation = to_mtl_stencil_op(desc.stencil_pass);
            stencil.readMask = desc.stencil_mask;
            stencil.writeMask = desc.stencil_mask;
            ds_desc.frontFaceStencil = stencil;
            ds_desc.backFaceStencil = stencil;
        }
        id<MTLDepthStencilState> ds_state = [ctx->core->device newDepthStencilStateWithDescriptor:ds_desc];
        if (!ds_state) {
            return {};
        }

        MetalPipeline pipeline{};
        pipeline.core = ctx->core;
        pipeline.render_state = render_state;
        pipeline.depth_stencil_state = ds_state;
        pipeline.cull_mode = cull_mode;
        pipeline.stencil_ref = desc.stencil_ref;
        pipeline.stencil_test = desc.stencil_test;
        pipeline.is_compute = false;
        pipeline.uses_draw_uniforms = desc.uses_draw_uniforms;
        pipeline.uses_draw_uniforms2 = desc.uses_draw_uniforms2;
        pipeline.storage_buffer_count = std::min<uint8_t>(desc.storage_buffer_count, 5);
        if (pipeline.storage_buffer_count == 0 && desc.uses_storage_buffer) {
            pipeline.storage_buffer_count = 1;
        }
        pipeline.uses_storage_buffer = pipeline.storage_buffer_count != 0;
        pipeline.uses_single_texture = desc.uses_single_texture;
        pipeline.uses_bindless_sampled_textures = desc.uses_bindless_sampled_textures;

        return g_metal_pipeline_pool.insert(std::move(pipeline));
    }
}

Handle<Pipeline> create_compute_pipeline(Context* ctx_ptr, const ComputePipelineDesc& desc)
{
    @autoreleasepool {
        auto* ctx = as_metal(ctx_ptr);
        auto* cs = g_metal_shader_pool.get(desc.cs);
        if (!ctx || !ctx->core || !cs || !cs->function) {
            return {};
        }

        NSError* error = nil;
        id<MTLComputePipelineState> compute_state =
            [ctx->core->device newComputePipelineStateWithFunction:cs->function error:&error];
        if (!compute_state) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Metal newComputePipelineState failed: %s",
                error ? error.localizedDescription.UTF8String : "unknown error");
            record_metal_validation_error(ctx->core,
                error ? error.localizedDescription.UTF8String : "newComputePipelineState failed");
            return {};
        }

        MetalPipeline pipeline{};
        pipeline.core = ctx->core;
        pipeline.compute_state = compute_state;
        pipeline.is_compute = true;
        pipeline.threadgroup_size[0] = cs->threadgroup_size[0];
        pipeline.threadgroup_size[1] = cs->threadgroup_size[1];
        pipeline.threadgroup_size[2] = cs->threadgroup_size[2];
        pipeline.uses_draw_uniforms = desc.uses_uniforms;
        pipeline.uses_single_texture = false;
        pipeline.uses_bindless_sampled_textures = false;
        pipeline.storage_buffer_count = std::min<uint8_t>(desc.storage_buffer_count, 5);
        if (pipeline.storage_buffer_count == 0 && desc.uses_storage_buffer) {
            pipeline.storage_buffer_count = 1;
        }
        pipeline.uses_storage_buffer = pipeline.storage_buffer_count != 0;

        return g_metal_pipeline_pool.insert(std::move(pipeline));
    }
}

void destroy_pipeline(Context* ctx, Handle<Pipeline> handle)
{
    @autoreleasepool {
        (void)ctx;
        if (!g_metal_pipeline_pool.get(handle)) {
            return;
        }
        g_metal_pipeline_pool.destroy(handle);
    }
}

// ============================================================================
// == Textures & Render Targets ===============================================
// ============================================================================

Handle<Texture> create_texture(Context* ctx_ptr, const TextureDesc& desc)
{
    @autoreleasepool {
        auto* ctx = as_metal(ctx_ptr);
        const MTLPixelFormat format = to_mtl_format(desc.format);
        if (!ctx || !ctx->core || !ctx->core->device
            || desc.width == 0 || desc.height == 0 || desc.layers == 0 || desc.mip_levels == 0
            || format == MTLPixelFormatInvalid
            || desc.width > kMaxTextureDimension2D
            || desc.height > kMaxTextureDimension2D
            || desc.layers > kMaxTextureArrayLayers
            || desc.mip_levels > static_cast<uint32_t>(std::bit_width(std::max(desc.width, desc.height)))) {
            return {};
        }
        if (desc.sampled && ctx->bindless_texture_capacity != 0
            && ctx->free_bindless_texture_slots.empty()
            && ctx->bindless_texture_count >= ctx->bindless_texture_capacity) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "bindless sampled texture table exhausted: capacity=%u", ctx->bindless_texture_capacity);
            return {};
        }

        MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
        td.textureType = (desc.layers > 1) ? MTLTextureType2DArray : MTLTextureType2D;
        td.pixelFormat = format;
        td.width = desc.width;
        td.height = desc.height;
        td.mipmapLevelCount = desc.mip_levels;
        td.arrayLength = desc.layers;
        td.storageMode = MTLStorageModePrivate;

        MTLTextureUsage usage = 0;
        if (desc.sampled) {
            usage |= MTLTextureUsageShaderRead;
        }
        if (desc.storage) {
            usage |= MTLTextureUsageShaderWrite;
        }
        if (desc.render_target) {
            usage |= MTLTextureUsageRenderTarget;
        }
        if (usage == 0) {
            usage = MTLTextureUsageShaderRead;
        }
        td.usage = usage;

        id<MTLTexture> mtl_tex = [ctx->core->device newTextureWithDescriptor:td];
        if (!mtl_tex) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Metal newTextureWithDescriptor failed");
            return {};
        }

        MetalTexture tex{};
        tex.texture = mtl_tex;
        tex.format = format;
        tex.width = desc.width;
        tex.height = desc.height;
        tex.layers = desc.layers;
        tex.mip_levels = desc.mip_levels;
        tex.owned = true;
        tex.render_target = desc.render_target;
        tex.uploaded = false;

        if (desc.sampled) {
            if (!ctx->free_bindless_texture_slots.empty()) {
                tex.bindless_slot = ctx->free_bindless_texture_slots.back();
                ctx->free_bindless_texture_slots.pop_back();
            } else if (ctx->bindless_texture_count < ctx->bindless_texture_capacity) {
                tex.bindless_slot = ctx->bindless_texture_count++;
            }
        }

        auto handle = ctx->texture_pool_.insert(std::move(tex));
        if (!handle.valid()) {
            if (bindless_texture_index_valid(tex.bindless_slot)) {
                ctx->free_bindless_texture_slots.push_back(tex.bindless_slot);
            }
            return {};
        }

        if (auto* stored = ctx->texture_pool_.get(handle);
            stored && bindless_texture_index_valid(stored->bindless_slot)) {
            write_bindless_texture_entry(ctx, stored->bindless_slot, stored->texture);
            stored->resident_index = static_cast<uint32_t>(ctx->bindless_resident_textures.size());
            ctx->bindless_resident_textures.push_back(stored->texture);
            ctx->bindless_resident_handles.push_back(handle);
        }
        return handle;
    }
}

void destroy_texture(Context* ctx_ptr, Handle<Texture> handle)
{
    @autoreleasepool {
        auto* ctx = as_metal(ctx_ptr);
        if (!ctx) {
            return;
        }
        auto* tex = ctx->texture_pool_.get(handle);
        if (!tex) {
            return;
        }
        if (bindless_texture_index_valid(tex->bindless_slot)) {
            write_bindless_texture_entry(ctx, tex->bindless_slot, nil);
            remove_bindless_resident(ctx, tex);
            ctx->free_bindless_texture_slots.push_back(tex->bindless_slot);
        }
        ctx->texture_pool_.destroy(handle);
    }
}

bool upload_textures(Context* ctx_ptr, std::span<const TextureUpload> uploads)
{
    @autoreleasepool {
        NW_PROFILE_SCOPE_N("nw::gfx::upload_textures");
        auto* ctx = as_metal(ctx_ptr);
        if (!ctx || !ctx->core || !ctx->core->device || !ctx->core->command_queue) {
            return false;
        }
        if (uploads.empty()) {
            return true;
        }
        if (!uploads.data()) {
            return false;
        }

        UploadObservation observation{ctx};
        observation.textures = uploads.size();

        size_t staging_bytes = 0;
        std::vector<MetalUploadRegion> regions;
        try {
            if (!prepare_texture_uploads(ctx, uploads, regions, staging_bytes, observation)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "gfx: rejected texture upload batch before submission");
                return false;
            }
        } catch (const std::bad_alloc&) {
            return false;
        }

        id<MTLBuffer> staging = [ctx->core->device newBufferWithLength:staging_bytes
                                                              options:MTLResourceStorageModeShared];
        if (!staging) {
            return false;
        }
        auto& stats = ctx->resource_stats;
        ++stats.staging_allocation_count;
        stats.staging_allocation_bytes += staging_bytes;
        stats.staging_high_water_bytes = std::max<uint64_t>(stats.staging_high_water_bytes, staging_bytes);

        auto* dst_bytes = static_cast<uint8_t*>([staging contents]);
        size_t region_idx = 0;
        for (const auto& upload : uploads) {
            for (const auto& mip : upload.mips) {
                std::memcpy(dst_bytes + regions[region_idx++].buffer_offset, mip.data, mip.size);
            }
        }

        id<MTLCommandBuffer> cb = [ctx->core->command_queue commandBuffer];
        if (!cb) {
            return false;
        }
        ++stats.upload_command_pool_count;
        ++stats.upload_command_buffer_count;

        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        if (!blit) {
            return false;
        }

        size_t first_region = 0;
        for (const auto& upload : uploads) {
            const auto& tex = *ctx->texture_pool_.get(upload.texture);
            for (size_t i = 0; i < upload.mips.size(); ++i) {
                const auto& r = regions[first_region + i];
                [blit copyFromBuffer:staging
                        sourceOffset:r.buffer_offset
                   sourceBytesPerRow:r.bytes_per_row
                 sourceBytesPerImage:r.bytes_per_image
                          sourceSize:MTLSizeMake(r.width, r.height, 1)
                           toTexture:tex.texture
                    destinationSlice:0
                    destinationLevel:r.mip_level
                   destinationOrigin:MTLOriginMake(0, 0, 0)];
            }
            first_region += upload.mips.size();

            if (upload.mips.size() == 1 && tex.mip_levels > 1) {
                [blit generateMipmapsForTexture:tex.texture];
            }
        }
        [blit endEncoding];

        [cb commit];
        ++stats.upload_submission_count;
        const uint64_t wait_start = SDL_GetTicksNS();
        [cb waitUntilCompleted];
        ++stats.upload_wait_count;
        stats.upload_wait_seconds += static_cast<double>(SDL_GetTicksNS() - wait_start) * kNanosecondsToSeconds;

        check_command_buffer_error(ctx->core, cb, "upload_textures");
        if (cb.status == MTLCommandBufferStatusError) {
            return false;
        }

        for (const auto& upload : uploads) {
            auto* tex = ctx->texture_pool_.get(upload.texture);
            tex->uploaded = true;
            if (bindless_texture_index_valid(tex->bindless_slot)) {
                write_bindless_texture_entry(ctx, tex->bindless_slot, tex->texture);
            }
        }
        observation.success = true;
        return true;
    }
}

bool upload_texture_rgba8(Context* ctx, Handle<Texture> handle, const void* data, size_t size)
{
    return upload_texture_base(ctx, handle, data, size, false);
}

bool upload_texture_rgba16f(Context* ctx, Handle<Texture> handle, const void* data, size_t size)
{
    return upload_texture_base(ctx, handle, data, size, true);
}

bool upload_texture_rgba16f_mips(Context* ctx, Handle<Texture> handle, const TextureMipData* levels, size_t count)
{
    auto* context = as_metal(ctx);
    const auto* tex = context ? context->texture_pool_.get(handle) : nullptr;
    if (!tex || tex->format != MTLPixelFormatRGBA16Float || !levels || count != tex->mip_levels) {
        return false;
    }
    const TextureUpload upload{handle, {levels, count}};
    return upload_textures(ctx, {&upload, 1});
}

Handle<RenderTarget> create_render_target(Context* ctx_ptr, const RenderTargetDesc& desc)
{
    auto* ctx = as_metal(ctx_ptr);
    if (!ctx || !render_target_desc_supported(ctx, desc)) {
        return {};
    }
    MetalRenderTarget rt{desc};
    return ctx->render_target_pool_.insert(std::move(rt));
}

void destroy_render_target(Context* ctx_ptr, Handle<RenderTarget> handle)
{
    auto* ctx = as_metal(ctx_ptr);
    if (!ctx) {
        return;
    }
    auto* rt = ctx->render_target_pool_.get(handle);
    if (!rt) {
        return;
    }
    if (rt->desc.color.texture.valid()) {
        destroy_texture(ctx_ptr, rt->desc.color.texture);
    }
    if (rt->desc.depth.texture.valid()) {
        destroy_texture(ctx_ptr, rt->desc.depth.texture);
    }
    ctx->render_target_pool_.destroy(handle);
}

// ============================================================================
// == Context Lifecycle =======================================================
// ============================================================================

Context* create_context(Core* core_ptr, const ContextDesc& desc)
{
    @autoreleasepool {
        auto* core = as_metal(core_ptr);
        if (!core || core->context || desc.width == 0 || desc.height == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "create_context requires an unused core and positive extent");
            return nullptr;
        }

        auto* ctx = new MetalContext();
        ctx->core = core;
        core->context = ctx;
        ctx->trace_resources = SDL_getenv("ROLLNW_GFX_TRACE_RESOURCES") != nullptr;
        if (ctx->trace_resources) {
            SDL_Log("gfx-resource device name=%s backend=metal argBufTier=%lu",
                core->device.name.UTF8String,
                static_cast<unsigned long>(core->device.argumentBuffersSupport));
        }

        ctx->window = desc.window;
        ctx->headless = (desc.window == nullptr);
        ctx->width = desc.width;
        ctx->height = desc.height;
        ctx->frame_index = 0;
        ctx->frame_id = 0;
        ctx->bindless_texture_capacity = kMetalBindlessTextureCapacity;
        ctx->bindless_texture_count = 1; // slot 0 reserved as kInvalidBindlessTextureIndex

        // Create bindless argument buffer BEFORE creating textures so create_texture can populate slots.
        const size_t bindless_table_bytes =
            static_cast<size_t>(ctx->bindless_texture_capacity + 1) * sizeof(MTLResourceID);
        ctx->bindless_texture_table = [core->device newBufferWithLength:bindless_table_bytes
                                                                options:MTLResourceStorageModeShared];
        GFX_CHECK(ctx->bindless_texture_table != nil, "failed to create Metal bindless texture table");
        ctx->bindless_texture_table_mapped = static_cast<MTLResourceID*>([ctx->bindless_texture_table contents]);
        std::memset(ctx->bindless_texture_table_mapped, 0, bindless_table_bytes);

        // Create linear and nearest samplers.
        MTLSamplerDescriptor* linear_desc = [[MTLSamplerDescriptor alloc] init];
        linear_desc.minFilter = MTLSamplerMinMagFilterLinear;
        linear_desc.magFilter = MTLSamplerMinMagFilterLinear;
        linear_desc.mipFilter = MTLSamplerMipFilterLinear;
        linear_desc.sAddressMode = MTLSamplerAddressModeRepeat;
        linear_desc.tAddressMode = MTLSamplerAddressModeRepeat;
        linear_desc.rAddressMode = MTLSamplerAddressModeRepeat;
        linear_desc.lodMinClamp = 0.0f;
        linear_desc.lodMaxClamp = FLT_MAX;
        linear_desc.supportArgumentBuffers = YES;
        ctx->linear_sampler = [core->device newSamplerStateWithDescriptor:linear_desc];
        GFX_CHECK(ctx->linear_sampler != nil, "failed to create Metal linear sampler");

        MTLSamplerDescriptor* nearest_desc = [[MTLSamplerDescriptor alloc] init];
        nearest_desc.minFilter = MTLSamplerMinMagFilterNearest;
        nearest_desc.magFilter = MTLSamplerMinMagFilterNearest;
        nearest_desc.mipFilter = MTLSamplerMipFilterNearest;
        nearest_desc.sAddressMode = MTLSamplerAddressModeRepeat;
        nearest_desc.tAddressMode = MTLSamplerAddressModeRepeat;
        nearest_desc.rAddressMode = MTLSamplerAddressModeRepeat;
        nearest_desc.lodMinClamp = 0.0f;
        nearest_desc.lodMaxClamp = FLT_MAX;
        nearest_desc.supportArgumentBuffers = YES;
        ctx->nearest_sampler = [core->device newSamplerStateWithDescriptor:nearest_desc];
        GFX_CHECK(ctx->nearest_sampler != nil, "failed to create Metal nearest sampler");
        write_bindless_sampler_entry(ctx);

        // GPU timestamp sample buffers (stage-boundary sampling on Apple Silicon).
        ctx->gpu_timers_supported = (core->timestamp_counter_set != nil && core->counter_sampling_at_stage_boundary);
        ctx->completed_gpu_timer_results.reserve(kMetalMaxGpuTimersPerFrame);
        if (ctx->gpu_timers_supported) {
            ctx->timestamp_scratch_buffer = [core->device newBufferWithLength:16 options:MTLResourceStorageModePrivate];
            MTLCounterSampleBufferDescriptor* csb_desc = [[MTLCounterSampleBufferDescriptor alloc] init];
            csb_desc.counterSet = core->timestamp_counter_set;
            csb_desc.storageMode = MTLStorageModeShared;
            csb_desc.sampleCount = kMetalMaxGpuTimestampSamplesPerFrame;
            for (uint32_t i = 0; i < kMetalFramesInFlight; ++i) {
                NSError* csb_err = nil;
                ctx->frames[i].timestamp_sample_buffer =
                    [core->device newCounterSampleBufferWithDescriptor:csb_desc error:&csb_err];
                if (!ctx->frames[i].timestamp_sample_buffer) {
                    ctx->gpu_timers_supported = false;
                    break;
                }
            }
        }

        // Per-frame uniform and vertex ring buffers.
        for (uint32_t i = 0; i < kMetalFramesInFlight; ++i) {
            BufferDesc uniform_desc{};
            uniform_desc.size = kUniformRingSize;
            uniform_desc.usage = BufferUsage::Uniform;
            uniform_desc.cpu_visible = true;
            ctx->uniform_ring_handle[i] = create_buffer(as_gfx(ctx), uniform_desc);
            auto* uniform = g_metal_buffer_pool.get(ctx->uniform_ring_handle[i]);
            GFX_CHECK(uniform != nullptr, "failed to create Metal uniform ring buffer");
            ctx->uniform_ring[i] = uniform->buffer;
            ctx->uniform_ring_mapped[i] = uniform->mapped;
            ctx->uniform_ring_offset[i] = 0;

            BufferDesc vertex_desc{};
            vertex_desc.size = kVertexRingSize;
            vertex_desc.usage = BufferUsage::Vertex;
            vertex_desc.cpu_visible = true;
            ctx->vertex_ring_handle[i] = create_buffer(as_gfx(ctx), vertex_desc);
            auto* vertex = g_metal_buffer_pool.get(ctx->vertex_ring_handle[i]);
            GFX_CHECK(vertex != nullptr, "failed to create Metal vertex ring buffer");
            ctx->vertex_ring[i] = vertex->buffer;
            ctx->vertex_ring_mapped[i] = vertex->mapped;
            ctx->vertex_ring_offset[i] = 0;

            ctx->descriptor_ring_offset[i] = 0;
        }

        if (ctx->headless) {
            TextureDesc color_desc = {
                .width = ctx->width,
                .height = ctx->height,
                .format = Fmt::RGBA8,
                .sampled = true,
                .render_target = true,
            };
            TextureDesc depth_desc = {
                .width = ctx->width,
                .height = ctx->height,
                .format = Fmt::D32FS8,
                .sampled = false,
                .render_target = true,
            };
            RenderTargetDesc rt_desc{};
            rt_desc.color.texture = create_texture(as_gfx(ctx), color_desc);
            rt_desc.depth.texture = create_texture(as_gfx(ctx), depth_desc);
            ctx->default_render_target = create_render_target(as_gfx(ctx), rt_desc);
        } else {
            ctx->metal_view = SDL_Metal_CreateView(desc.window);
            GFX_CHECK(ctx->metal_view != nullptr, "SDL_Metal_CreateView failed: %s", SDL_GetError());
            ctx->metal_layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(ctx->metal_view);
            GFX_CHECK(ctx->metal_layer != nil, "SDL_Metal_GetLayer returned nil");

            int pixel_w = 0;
            int pixel_h = 0;
            SDL_GetWindowSizeInPixels(desc.window, &pixel_w, &pixel_h);
            ctx->width = static_cast<uint32_t>(std::max(1, pixel_w));
            ctx->height = static_cast<uint32_t>(std::max(1, pixel_h));
            ctx->swapchain_format = MTLPixelFormatBGRA8Unorm;

            ctx->metal_layer.device = core->device;
            ctx->metal_layer.pixelFormat = ctx->swapchain_format;
            ctx->metal_layer.framebufferOnly = NO; // Required for capture_screenshot readback
            ctx->metal_layer.drawableSize = CGSizeMake(ctx->width, ctx->height);

            create_swapchain_depth_target(ctx);
        }

        return as_gfx(ctx);
    }
}

void destroy_context(Context* ctx_ptr)
{
    @autoreleasepool {
        auto* ctx = as_metal(ctx_ptr);
        if (!ctx) {
            return;
        }

        wait_idle(ctx_ptr);

        if (ctx->headless) {
            destroy_render_target(ctx_ptr, ctx->default_render_target);
            ctx->default_render_target = {};
        } else {
            destroy_render_target(ctx_ptr, ctx->swapchain_render_target);
            ctx->swapchain_render_target = {};
            ctx->swapchain_depth_texture = {};
            ctx->current_drawable = nil;
            ctx->metal_layer = nil;
            if (ctx->metal_view) {
                SDL_Metal_DestroyView(ctx->metal_view);
                ctx->metal_view = nullptr;
            }
        }

        for (uint32_t i = 0; i < kMetalFramesInFlight; ++i) {
            if (ctx->uniform_ring_handle[i].valid()) {
                destroy_buffer(ctx->uniform_ring_handle[i]);
                ctx->uniform_ring_handle[i] = {};
            }
            if (ctx->vertex_ring_handle[i].valid()) {
                destroy_buffer(ctx->vertex_ring_handle[i]);
                ctx->vertex_ring_handle[i] = {};
            }
            ctx->uniform_ring[i] = nil;
            ctx->vertex_ring[i] = nil;
            ctx->frames[i].cmds.render_encoder = nil;
            ctx->frames[i].cmds.compute_encoder = nil;
            ctx->frames[i].cmds.command_buffer = nil;
            ctx->frames[i].cmds.active_render_pass_descriptor = nil;
            ctx->frames[i].last_command_buffer = nil;
            ctx->frames[i].timestamp_sample_buffer = nil;
        }

        ctx->bindless_resident_textures.clear();
        ctx->bindless_resident_handles.clear();
        ctx->bindless_texture_table = nil;
        ctx->bindless_texture_table_mapped = nullptr;
        ctx->timestamp_scratch_buffer = nil;
        ctx->linear_sampler = nil;
        ctx->nearest_sampler = nil;

        ctx->core->context = nullptr;
        delete ctx;
    }
}

bool resize_context(Context* ctx_ptr, uint32_t width, uint32_t height)
{
    @autoreleasepool {
        auto* ctx = as_metal(ctx_ptr);
        if (!ctx) {
            return false;
        }

        if (!ctx->headless && ctx->window) {
            int pixel_w = 0;
            int pixel_h = 0;
            SDL_GetWindowSizeInPixels(ctx->window, &pixel_w, &pixel_h);
            if (pixel_w > 0 && pixel_h > 0) {
                width = static_cast<uint32_t>(pixel_w);
                height = static_cast<uint32_t>(pixel_h);
            }
        }
        width = std::max(1u, width);
        height = std::max(1u, height);

        if (ctx->width == width && ctx->height == height) {
            return true;
        }

        wait_idle(ctx_ptr);
        ctx->width = width;
        ctx->height = height;

        if (ctx->headless) {
            destroy_render_target(ctx_ptr, ctx->default_render_target);
            ctx->default_render_target = {};

            TextureDesc color_desc = {
                .width = ctx->width,
                .height = ctx->height,
                .format = Fmt::RGBA8,
                .sampled = true,
                .render_target = true,
            };
            TextureDesc depth_desc = {
                .width = ctx->width,
                .height = ctx->height,
                .format = Fmt::D32FS8,
                .sampled = false,
                .render_target = true,
            };
            RenderTargetDesc rt_desc{};
            rt_desc.color.texture = create_texture(ctx_ptr, color_desc);
            rt_desc.depth.texture = create_texture(ctx_ptr, depth_desc);
            ctx->default_render_target = create_render_target(ctx_ptr, rt_desc);
            return ctx->default_render_target.valid();
        }

        destroy_render_target(ctx_ptr, ctx->swapchain_render_target);
        ctx->swapchain_render_target = {};
        ctx->swapchain_depth_texture = {};
        ctx->current_drawable = nil;

        if (ctx->metal_layer) {
            ctx->metal_layer.drawableSize = CGSizeMake(ctx->width, ctx->height);
        }
        create_swapchain_depth_target(ctx);
        return ctx->swapchain_render_target.valid();
    }
}

void wait_idle(Context* ctx_ptr, std::source_location source)
{
    @autoreleasepool {
        auto* ctx = as_metal(ctx_ptr);
        if (!ctx) {
            return;
        }
        const uint64_t start = SDL_GetTicksNS();
        for (uint32_t i = 0; i < kMetalFramesInFlight; ++i) {
            if (ctx->frames[i].last_command_buffer) {
                [ctx->frames[i].last_command_buffer waitUntilCompleted];
                check_command_buffer_error(ctx->core, ctx->frames[i].last_command_buffer, "wait_idle");
                ctx->frames[i].last_command_buffer = nil;
            }
        }
        const double seconds = static_cast<double>(SDL_GetTicksNS() - start) * kNanosecondsToSeconds;
        ++ctx->resource_stats.idle_wait_count;
        ctx->resource_stats.idle_wait_seconds += seconds;
        if (ctx->trace_resources) {
            SDL_Log("gfx-resource idle start_ns=%llu seconds=%.9f source=%s:%u reason=%s",
                static_cast<unsigned long long>(start), seconds, source.file_name(), source.line(), source.function_name());
        }
    }
}

ResourceStats resource_stats(Context* ctx_ptr) noexcept
{
    const auto* ctx = as_metal(ctx_ptr);
    return ctx ? ctx->resource_stats : ResourceStats{};
}

bool get_frame_info(Context* ctx_ptr, FrameInfo& out) noexcept
{
    const auto* ctx = as_metal(ctx_ptr);
    if (!ctx) {
        out = FrameInfo{};
        return false;
    }
    out.frame_index = ctx->frame_index;
    out.frame_id = ctx->frame_id;
    out.width = ctx->width;
    out.height = ctx->height;
    out.headless = ctx->headless;
    out.drawable = ctx->headless || ctx->metal_layer != nil;
    return true;
}

bool get_command_stats(CommandList* cmd_ptr, CommandStats& out) noexcept
{
    const auto* cmd = reinterpret_cast<const MetalCommandList*>(cmd_ptr);
    const auto* ctx = cmd ? cmd->context : nullptr;
    if (!cmd || !ctx) {
        out = CommandStats{};
        return false;
    }
    out = cmd->stats;
    out.descriptor_ring_capacity_bytes = kDescriptorRingSize;
    out.uniform_allocation_count += ctx->frame_stats.uniform_allocation_count;
    out.uniform_allocation_bytes += ctx->frame_stats.uniform_allocation_bytes;
    out.uniform_allocation_failure_count += ctx->frame_stats.uniform_allocation_failure_count;
    out.uniform_ring_capacity_bytes = kUniformRingSize;
    out.uniform_ring_required_bytes = ctx->frame_stats.uniform_ring_required_bytes;
    out.vertex_allocation_count += ctx->frame_stats.vertex_allocation_count;
    out.vertex_allocation_bytes += ctx->frame_stats.vertex_allocation_bytes;
    return true;
}

bool get_completed_gpu_timer_results(Context* ctx_ptr, std::vector<GpuTimerResult>& out) noexcept
{
    const auto* ctx = as_metal(ctx_ptr);
    if (!ctx || !ctx->gpu_timers_supported) {
        out.clear();
        return false;
    }
    out = ctx->completed_gpu_timer_results;
    return true;
}

// ============================================================================
// == Frame & Command Recording ===============================================
// ============================================================================

CommandList* begin_frame(Context* ctx_ptr)
{
    @autoreleasepool {
        NW_PROFILE_SCOPE_N("nw::gfx::begin_frame");
        auto* ctx = as_metal(ctx_ptr);
        if (!ctx || !ctx->core || !ctx->core->command_queue) {
            return nullptr;
        }

        const uint32_t frame_index = ctx->frame_index;
        MetalPerFrame& frame = ctx->frames[frame_index];

        if (frame.last_command_buffer) {
            [frame.last_command_buffer waitUntilCompleted];
            check_command_buffer_error(ctx->core, frame.last_command_buffer, "begin_frame");
            frame.last_command_buffer = nil;
        }
        if (frame.has_completed_submission) {
            collect_completed_gpu_timer_results(ctx, frame);
            frame.has_completed_submission = false;
        } else {
            ctx->completed_gpu_timer_results.clear();
        }

        if (!ctx->headless) {
            if (!ctx->metal_layer) {
                return nullptr;
            }
            int pixel_w = 0;
            int pixel_h = 0;
            SDL_GetWindowSizeInPixels(ctx->window, &pixel_w, &pixel_h);
            if (pixel_w > 0 && pixel_h > 0
                && (static_cast<uint32_t>(pixel_w) != ctx->width || static_cast<uint32_t>(pixel_h) != ctx->height)) {
                resize_context(ctx_ptr, static_cast<uint32_t>(pixel_w), static_cast<uint32_t>(pixel_h));
            }
            ctx->current_drawable = [ctx->metal_layer nextDrawable];
            if (!ctx->current_drawable) {
                return nullptr;
            }
        }

        id<MTLCommandBuffer> cb = [ctx->core->command_queue commandBuffer];
        if (!cb) {
            ctx->current_drawable = nil;
            return nullptr;
        }

        ctx->descriptor_ring_offset[frame_index] = 0;
        ctx->uniform_ring_offset[frame_index] = 0;
        ctx->vertex_ring_offset[frame_index] = 0;
        ctx->frame_stats = {};

        reset_command_list_for_frame(ctx, frame_index, cb);
        return reinterpret_cast<CommandList*>(&frame.cmds);
    }
}

void end_frame(Context* ctx_ptr)
{
    @autoreleasepool {
        NW_PROFILE_SCOPE_N("nw::gfx::end_frame");
        auto* ctx = as_metal(ctx_ptr);
        if (!ctx) {
            return;
        }

        MetalPerFrame& frame = ctx->frames[ctx->frame_index];
        MetalCommandList& cmds = frame.cmds;
        if (!cmds.command_buffer) {
            return;
        }

        if (cmds.in_render_pass && cmds.render_encoder) {
            [cmds.render_encoder endEncoding];
            cmds.render_encoder = nil;
            cmds.active_render_pass_descriptor = nil;
            cmds.in_render_pass = false;
        }
        end_compute_encoder_if_active(&cmds);

        cmds.recording = false;
        if (!ctx->headless && ctx->current_drawable) {
            [cmds.command_buffer presentDrawable:ctx->current_drawable];
            ctx->current_drawable = nil;
        }

        frame.last_command_buffer = cmds.command_buffer;
        frame.has_completed_submission = true;
        [cmds.command_buffer commit];
        cmds.command_buffer = nil;

        ctx->frame_index = (ctx->frame_index + 1) % kMetalFramesInFlight;
        ctx->frame_id += 1;
    }
}

void cmd_begin_render(CommandList* cmd_ptr, Handle<RenderTarget> target, RenderLoadOp color_load_op)
{
    @autoreleasepool {
        auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
        auto* ctx = cmd ? cmd->context : nullptr;
        if (!cmd || !ctx || !cmd->command_buffer || cmd->in_render_pass) {
            return;
        }

        end_compute_encoder_if_active(cmd);

        Handle<RenderTarget> render_target = target;
        if (!render_target.valid()) {
            render_target = ctx->headless ? ctx->default_render_target : ctx->swapchain_render_target;
        }
        auto* rt = ctx->render_target_pool_.get(render_target);
        if (!rt) {
            return;
        }

        MetalTexture* color_tex = nullptr;
        id<MTLTexture> color_mtl = nil;
        bool use_swapchain_color = false;
        const bool has_color_attachment = rt->desc.color.texture.valid();
        if (has_color_attachment) {
            color_tex = ctx->texture_pool_.get(rt->desc.color.texture);
            if (!color_tex) {
                return;
            }
            use_swapchain_color = !ctx->headless && color_tex->texture == nil;
            color_mtl = use_swapchain_color
                ? (ctx->current_drawable ? ctx->current_drawable.texture : nil)
                : color_tex->texture;
            if (!color_mtl) {
                return;
            }
        }

        MetalTexture* depth_tex = nullptr;
        id<MTLTexture> depth_mtl = nil;
        if (rt->desc.depth.texture.valid()) {
            depth_tex = ctx->texture_pool_.get(rt->desc.depth.texture);
            if (depth_tex) {
                depth_mtl = depth_tex->texture;
            }
        }

        if (!color_mtl && !depth_mtl) {
            return;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        if (color_mtl) {
            pass.colorAttachments[0].texture = color_mtl;
            pass.colorAttachments[0].loadAction =
                (color_load_op == RenderLoadOp::load || !rt->desc.color.clear)
                ? MTLLoadActionLoad
                : MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0.24, 0.30, 0.42, 1.0);
        }

        if (depth_mtl) {
            pass.depthAttachment.texture = depth_mtl;
            pass.depthAttachment.loadAction = rt->desc.depth.clear ? MTLLoadActionClear : MTLLoadActionLoad;
            pass.depthAttachment.storeAction = MTLStoreActionStore;
            pass.depthAttachment.clearDepth = 1.0;

            if (depth_tex && has_mtl_stencil_component(depth_tex->format)) {
                pass.stencilAttachment.texture = depth_mtl;
                pass.stencilAttachment.loadAction = rt->desc.depth.clear ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.stencilAttachment.storeAction = MTLStoreActionStore;
                pass.stencilAttachment.clearStencil = 0;
            }
        }

        id<MTLRenderCommandEncoder> encoder = [cmd->command_buffer renderCommandEncoderWithDescriptor:pass];
        if (!encoder) {
            return;
        }
        [encoder setFrontFacingWinding:MTLWindingCounterClockwise];

        cmd->render_encoder = encoder;
        cmd->active_render_pass_descriptor = pass;
        cmd->current_render_target = render_target;
        cmd->render_target_width = color_mtl
            ? static_cast<uint32_t>(color_mtl.width)
            : static_cast<uint32_t>(depth_mtl.width);
        cmd->render_target_height = color_mtl
            ? static_cast<uint32_t>(color_mtl.height)
            : static_cast<uint32_t>(depth_mtl.height);
        cmd->in_render_pass = true;
        cmd->rendered = true;
        ++cmd->stats.render_pass_count;

        cmd->bound_pipeline = {};
        cmd->bound_vertex_buffer = {};
        cmd->bound_vertex_stride = 0;
        cmd->bound_vertex_offset = 0;
        cmd->bound_index_buffer = {};
        cmd->bound_index_size = 0;
        cmd->stencil_ref_set = false;
        cmd->dynamic_stencil_ref = 0;
        cmd->bindless_bound_in_pass = false;
        cmd->bound_resources = {};
        cmd->pipeline_bind_failed = false;
        cmd->vertex_bind_failed = false;
        cmd->index_bind_failed = false;
        cmd->resource_bind_failed = false;
        ++cmd->stats.descriptor_buffer_bind_count;
    }
}

void cmd_end_render(CommandList* cmd_ptr)
{
    @autoreleasepool {
        auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
        if (!cmd || !cmd->in_render_pass) {
            return;
        }
        if (cmd->render_encoder) {
            [cmd->render_encoder endEncoding];
            cmd->render_encoder = nil;
        }
        cmd->active_render_pass_descriptor = nil;
        cmd->current_render_target = {};
        cmd->render_target_width = 0;
        cmd->render_target_height = 0;
        cmd->in_render_pass = false;
        cmd->bindless_bound_in_pass = false;
    }
}

GpuTimerScope cmd_begin_gpu_timer(CommandList* cmd_ptr, const char* label)
{
    @autoreleasepool {
        auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
        auto* ctx = cmd ? cmd->context : nullptr;
        if (!cmd || !ctx || !cmd->recording || !ctx->gpu_timers_supported
            || !ctx->frames[cmd->pool_index].timestamp_sample_buffer
            || cmd->gpu_timer_count >= kMetalMaxGpuTimersPerFrame
            || cmd->timestamp_sample_count + 2u > kMetalMaxGpuTimestampSamplesPerFrame) {
            return {};
        }

        // Apple Silicon TBDR GPUs support stage-boundary counter sampling only
        // (AtDrawBoundary == false). Skip mid-render-pass timer scopes cleanly
        // rather than splitting tile passes.
        if (cmd->in_render_pass && !ctx->core->counter_sampling_at_draw_boundary) {
            return {};
        }

        const uint32_t timer_index = cmd->gpu_timer_count++;
        const uint32_t start_sample = cmd->timestamp_sample_count++;
        const uint32_t end_sample = cmd->timestamp_sample_count++;
        cmd->gpu_timers[timer_index] = MetalGpuTimerRecord{
            .label = label,
            .start_sample = start_sample,
            .end_sample = end_sample,
            .ended = false,
        };

        if (cmd->in_render_pass) {
            [cmd->render_encoder sampleCountersInBuffer:ctx->frames[cmd->pool_index].timestamp_sample_buffer
                                          atSampleIndex:start_sample
                                            withBarrier:YES];
        } else {
            sample_stage_boundary_timestamp(cmd, start_sample, false);
        }
        return GpuTimerScope{timer_index};
    }
}

void cmd_end_gpu_timer(CommandList* cmd_ptr, GpuTimerScope scope)
{
    @autoreleasepool {
        auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
        auto* ctx = cmd ? cmd->context : nullptr;
        if (!cmd || !ctx || !cmd->recording || !ctx->gpu_timers_supported || !scope.valid()
            || scope.index >= cmd->gpu_timer_count
            || !ctx->frames[cmd->pool_index].timestamp_sample_buffer) {
            return;
        }

        auto& timer = cmd->gpu_timers[scope.index];
        if (timer.ended) {
            return;
        }

        if (cmd->in_render_pass) {
            if (!ctx->core->counter_sampling_at_draw_boundary) {
                return;
            }
            [cmd->render_encoder sampleCountersInBuffer:ctx->frames[cmd->pool_index].timestamp_sample_buffer
                                          atSampleIndex:timer.end_sample
                                            withBarrier:YES];
        } else {
            sample_stage_boundary_timestamp(cmd, timer.end_sample, true);
        }
        timer.ended = true;
    }
}

void cmd_bind_pipeline(CommandList* cmd_ptr, Handle<Pipeline> handle)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    auto* pipeline = g_metal_pipeline_pool.get(handle);
    if (!cmd) {
        return;
    }
    if (!pipeline || !cmd->context || pipeline->core != cmd->context->core) {
        cmd->bound_pipeline = {};
        cmd->pipeline_bind_failed = true;
        ++cmd->stats.pipeline_bind_failure_count;
        report_failed_bind(cmd, "pipeline");
        return;
    }
    cmd->pipeline_bind_failed = false;
    if (cmd->bound_pipeline == handle) {
        ++cmd->stats.pipeline_bind_skipped_count;
        return;
    }

    if (!pipeline->is_compute && cmd->render_encoder) {
        [cmd->render_encoder setRenderPipelineState:pipeline->render_state];
        [cmd->render_encoder setDepthStencilState:pipeline->depth_stencil_state];
        [cmd->render_encoder setCullMode:pipeline->cull_mode];
        if (pipeline->stencil_test && !cmd->stencil_ref_set) {
            [cmd->render_encoder setStencilReferenceValue:pipeline->stencil_ref];
        }
        if (pipeline->uses_bindless_sampled_textures) {
            ensure_bindless_bound_in_pass(cmd);
        }
    }

    cmd->bound_pipeline = handle;
    ++cmd->stats.pipeline_bind_count;
}

void cmd_bind_vertex_buffer(CommandList* cmd_ptr, Handle<Buffer> handle, uint32_t stride)
{
    cmd_bind_vertex_buffer(cmd_ptr, handle, stride, 0);
}

void cmd_bind_vertex_buffer(CommandList* cmd_ptr, Handle<Buffer> handle, uint32_t stride, uint32_t offset)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    auto* buffer = g_metal_buffer_pool.get(handle);
    if (!cmd) {
        return;
    }
    if (!buffer || !buffer->buffer || !cmd->context || buffer->core != cmd->context->core
        || stride == 0 || offset >= buffer->size) {
        cmd->bound_vertex_buffer = {};
        cmd->vertex_bind_failed = true;
        ++cmd->stats.vertex_buffer_bind_failure_count;
        report_failed_bind(cmd, "vertex buffer");
        return;
    }
    cmd->vertex_bind_failed = false;
    if (cmd->bound_vertex_buffer == handle
        && cmd->bound_vertex_stride == stride
        && cmd->bound_vertex_offset == offset) {
        ++cmd->stats.vertex_buffer_bind_skipped_count;
        return;
    }
    if (cmd->render_encoder) {
        [cmd->render_encoder setVertexBuffer:buffer->buffer offset:offset atIndex:0];
    }
    cmd->bound_vertex_buffer = handle;
    cmd->bound_vertex_stride = stride;
    cmd->bound_vertex_offset = offset;
    ++cmd->stats.vertex_buffer_bind_count;
}

void cmd_bind_index_buffer(CommandList* cmd_ptr, Handle<Buffer> handle, uint32_t index_size)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    auto* buffer = g_metal_buffer_pool.get(handle);
    if (!cmd) {
        return;
    }
    if (!buffer || !buffer->buffer || !cmd->context || buffer->core != cmd->context->core
        || (index_size != 2 && index_size != 4)) {
        cmd->bound_index_buffer = {};
        cmd->index_bind_failed = true;
        ++cmd->stats.index_buffer_bind_failure_count;
        report_failed_bind(cmd, "index buffer");
        return;
    }
    cmd->index_bind_failed = false;
    if (cmd->bound_index_buffer == handle && cmd->bound_index_size == index_size) {
        ++cmd->stats.index_buffer_bind_skipped_count;
        return;
    }
    cmd->bound_index_buffer = handle;
    cmd->bound_index_size = index_size;
    ++cmd->stats.index_buffer_bind_count;
}

void cmd_set_viewport(CommandList* cmd_ptr, float x, float y, float width, float height, float min_depth, float max_depth)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    if (!cmd || !cmd->render_encoder) {
        return;
    }
    MTLViewport vp{};
    vp.originX = static_cast<double>(x);
    vp.originY = static_cast<double>(y);
    vp.width = static_cast<double>(std::max(0.0f, width));
    vp.height = static_cast<double>(std::max(0.0f, height));
    vp.znear = static_cast<double>(min_depth);
    vp.zfar = static_cast<double>(max_depth);
    [cmd->render_encoder setViewport:vp];
}

void cmd_set_scissor(CommandList* cmd_ptr, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    if (!cmd || !cmd->render_encoder) {
        return;
    }
    // Metal validates that scissor rect lies strictly inside the render target bounds.
    const int64_t rt_w = static_cast<int64_t>(cmd->render_target_width);
    const int64_t rt_h = static_cast<int64_t>(cmd->render_target_height);
    const int64_t x0 = std::clamp<int64_t>(x, 0, rt_w);
    const int64_t y0 = std::clamp<int64_t>(y, 0, rt_h);
    const int64_t x1 = std::clamp<int64_t>(static_cast<int64_t>(x) + static_cast<int64_t>(width), x0, rt_w);
    const int64_t y1 = std::clamp<int64_t>(static_cast<int64_t>(y) + static_cast<int64_t>(height), y0, rt_h);

    MTLScissorRect rect{};
    rect.x = static_cast<NSUInteger>(x0);
    rect.y = static_cast<NSUInteger>(y0);
    rect.width = static_cast<NSUInteger>(x1 - x0);
    rect.height = static_cast<NSUInteger>(y1 - y0);
    [cmd->render_encoder setScissorRect:rect];
}

void cmd_set_stencil_ref(CommandList* cmd_ptr, uint8_t ref)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }
    cmd->stencil_ref_set = true;
    cmd->dynamic_stencil_ref = ref;
    if (cmd->render_encoder) {
        [cmd->render_encoder setStencilReferenceValue:ref];
    }
}

void cmd_bind_uniform_texture(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, Handle<Buffer> uniform_h,
    Handle<Texture> texture_h, TextureFilter filter)
{
    NW_PROFILE_SCOPE_N("nw::gfx::cmd_bind_uniform_texture");
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    auto* pipeline = g_metal_pipeline_pool.get(pipeline_h);
    auto* uniform = g_metal_buffer_pool.get(uniform_h);
    if (!cmd || !pipeline || !uniform) {
        fail_resource_bind(cmd);
        return;
    }

    MetalCommandList::BoundResources next_resources{};
    next_resources.pipeline = pipeline_h;
    next_resources.uniforms = UniformSpan{
        .buffer = uniform_h,
        .offset = 0,
        .size = static_cast<uint32_t>(uniform->size),
        .data = nullptr,
    };
    next_resources.texture = texture_h;
    next_resources.texture_filter = filter;
    next_resources.valid = true;
    if (resource_bind_matches(cmd->bound_resources, next_resources)) {
        cmd->resource_bind_failed = false;
        ++cmd->stats.resource_bind_skipped_count;
        return;
    }

    auto* tex = cmd->context->texture_pool_.get(texture_h);
    if (bind_pipeline_resources(cmd, pipeline, uniform, 0, static_cast<uint32_t>(uniform->size),
            {}, {}, {}, {}, {}, tex, filter, nullptr, 0, 0)) {
        cmd->bound_resources = next_resources;
        cmd->resource_bind_failed = false;
        ++cmd->stats.resource_bind_count;
    } else {
        fail_resource_bind(cmd);
    }
}

void cmd_bind_uniform_texture(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, const UniformSpan& uniforms,
    Handle<Texture> texture_h, TextureFilter filter)
{
    NW_PROFILE_SCOPE_N("nw::gfx::cmd_bind_uniform_texture(span)");
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    auto* pipeline = g_metal_pipeline_pool.get(pipeline_h);
    auto* uniform = g_metal_buffer_pool.get(uniforms.buffer);
    if (!cmd || !pipeline || !uniform) {
        fail_resource_bind(cmd);
        return;
    }

    MetalCommandList::BoundResources next_resources{};
    next_resources.pipeline = pipeline_h;
    next_resources.uniforms = uniforms;
    next_resources.texture = texture_h;
    next_resources.texture_filter = filter;
    next_resources.valid = true;
    if (resource_bind_matches(cmd->bound_resources, next_resources)) {
        cmd->resource_bind_failed = false;
        ++cmd->stats.resource_bind_skipped_count;
        return;
    }

    auto* tex = cmd->context->texture_pool_.get(texture_h);
    if (bind_pipeline_resources(cmd, pipeline, uniform, uniforms.offset, uniforms.size,
            {}, {}, {}, {}, {}, tex, filter, nullptr, 0, 0)) {
        cmd->bound_resources = next_resources;
        cmd->resource_bind_failed = false;
        ++cmd->stats.resource_bind_count;
    } else {
        fail_resource_bind(cmd);
    }
}

void cmd_bind_resources(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, const UniformSpan& uniforms,
    StorageSpan storage0, StorageSpan storage1, const UniformSpan& uniforms2)
{
    cmd_bind_resources(cmd_ptr, pipeline_h, uniforms, storage0, storage1, {}, {}, {}, uniforms2);
}

void cmd_bind_resources(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, const UniformSpan& uniforms,
    StorageSpan storage0, StorageSpan storage1, StorageSpan storage2, StorageSpan storage3, StorageSpan storage4,
    const UniformSpan& uniforms2)
{
    NW_PROFILE_SCOPE_N("nw::gfx::cmd_bind_resources");
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    auto* pipeline = g_metal_pipeline_pool.get(pipeline_h);
    auto* uniform = g_metal_buffer_pool.get(uniforms.buffer);
    auto* uniform2 = uniforms2.buffer.valid() ? g_metal_buffer_pool.get(uniforms2.buffer) : nullptr;
    if (!cmd || !pipeline || !uniform) {
        fail_resource_bind(cmd);
        return;
    }

    MetalCommandList::BoundResources next_resources{};
    next_resources.pipeline = pipeline_h;
    next_resources.uniforms = uniforms;
    next_resources.storages[0] = storage0;
    next_resources.storages[1] = storage1;
    next_resources.storages[2] = storage2;
    next_resources.storages[3] = storage3;
    next_resources.storages[4] = storage4;
    next_resources.uniforms2 = uniforms2;
    next_resources.valid = true;
    if (resource_bind_matches(cmd->bound_resources, next_resources)) {
        cmd->resource_bind_failed = false;
        ++cmd->stats.resource_bind_skipped_count;
        return;
    }

    if (bind_pipeline_resources(cmd, pipeline, uniform, uniforms.offset, uniforms.size,
            storage0, storage1, storage2, storage3, storage4, nullptr,
            TextureFilter::Linear, uniform2, uniforms2.offset, uniforms2.size)) {
        cmd->bound_resources = next_resources;
        cmd->resource_bind_failed = false;
        ++cmd->stats.resource_bind_count;
    } else {
        fail_resource_bind(cmd);
    }
}

void cmd_bind_compute_resources(CommandList* cmd_ptr, Handle<Pipeline> pipeline_h, const UniformSpan& uniforms,
    StorageSpan storage0, StorageSpan storage1, StorageSpan storage2, StorageSpan storage3, StorageSpan storage4)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    auto* pipeline = g_metal_pipeline_pool.get(pipeline_h);
    auto* uniform = uniforms.buffer.valid() ? g_metal_buffer_pool.get(uniforms.buffer) : nullptr;
    if (!cmd || !pipeline) {
        fail_resource_bind(cmd);
        return;
    }

    MetalCommandList::BoundResources next_resources{};
    next_resources.pipeline = pipeline_h;
    next_resources.uniforms = uniforms;
    next_resources.storages[0] = storage0;
    next_resources.storages[1] = storage1;
    next_resources.storages[2] = storage2;
    next_resources.storages[3] = storage3;
    next_resources.storages[4] = storage4;
    next_resources.compute = true;
    next_resources.valid = true;
    if (resource_bind_matches(cmd->bound_resources, next_resources)) {
        cmd->resource_bind_failed = false;
        ++cmd->stats.resource_bind_skipped_count;
        return;
    }

    if (bind_pipeline_resources(cmd, pipeline, uniform, uniforms.offset, uniforms.size,
            storage0, storage1, storage2, storage3, storage4, nullptr, TextureFilter::Linear, nullptr, 0, 0)) {
        cmd->bound_resources = next_resources;
        cmd->resource_bind_failed = false;
        ++cmd->stats.resource_bind_count;
    } else {
        fail_resource_bind(cmd);
    }
}

void cmd_barrier_compute_to_graphics(CommandList* cmd_ptr)
{
    @autoreleasepool {
        auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
        if (!cmd) {
            return;
        }
        // Ending the compute encoder synchronizes tracked Metal buffers before
        // subsequent render passes and indirect draw reads on the same command buffer.
        end_compute_encoder_if_active(cmd);
    }
}

void cmd_draw(CommandList* cmd_ptr, uint32_t vertex_count, uint32_t instance_count)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }
    if (cmd->pipeline_bind_failed || cmd->vertex_bind_failed || cmd->index_bind_failed || cmd->resource_bind_failed) {
        ++cmd->stats.dropped_draw_count;
        return;
    }
    ++cmd->stats.draw_count;
    ++cmd->stats.nonindexed_draw_count;
    cmd->stats.draw_vertex_count += vertex_count;
    cmd->stats.draw_instance_count += instance_count;
    if (cmd->render_encoder && vertex_count > 0 && instance_count > 0) {
        [cmd->render_encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                vertexStart:0
                                vertexCount:vertex_count
                              instanceCount:instance_count
                               baseInstance:0];
    }
}

void cmd_draw_indexed(CommandList* cmd_ptr, uint32_t index_count, uint32_t instance_count)
{
    cmd_draw_indexed_base_instance(cmd_ptr, index_count, 0, instance_count);
}

void cmd_draw_indexed_base_instance(CommandList* cmd_ptr, uint32_t index_count, uint32_t first_instance,
    uint32_t instance_count)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    if (!cmd) {
        return;
    }
    if (cmd->pipeline_bind_failed || cmd->vertex_bind_failed || cmd->index_bind_failed || cmd->resource_bind_failed) {
        ++cmd->stats.dropped_draw_count;
        return;
    }
    auto* idx_buf = g_metal_buffer_pool.get(cmd->bound_index_buffer);
    if (!idx_buf || !idx_buf->buffer) {
        ++cmd->stats.dropped_draw_count;
        return;
    }
    ++cmd->stats.draw_count;
    ++cmd->stats.indexed_draw_count;
    cmd->stats.draw_index_count += index_count;
    cmd->stats.draw_instance_count += instance_count;
    if (cmd->render_encoder && index_count > 0 && instance_count > 0) {
        const MTLIndexType idx_type = (cmd->bound_index_size == 2) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        [cmd->render_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexCount:index_count
                                         indexType:idx_type
                                       indexBuffer:idx_buf->buffer
                                 indexBufferOffset:0
                                     instanceCount:instance_count
                                        baseVertex:0
                                      baseInstance:first_instance];
    }
}

void cmd_draw_indexed_indirect(CommandList* cmd_ptr, IndirectDrawSpan commands, uint32_t draw_count,
    uint32_t stride, IndexedIndirectDrawStats stats)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    auto* command_buffer = g_metal_buffer_pool.get(commands.buffer);
    if (!cmd || !command_buffer || !command_buffer->buffer || draw_count == 0) {
        return;
    }
    if (cmd->pipeline_bind_failed || cmd->vertex_bind_failed || cmd->index_bind_failed || cmd->resource_bind_failed) {
        cmd->stats.dropped_draw_count += draw_count;
        return;
    }
    if (stride < sizeof(IndexedIndirectDrawCommand)) {
        return;
    }
    const uint64_t required_size = sizeof(IndexedIndirectDrawCommand)
        + (static_cast<uint64_t>(draw_count) - 1u) * stride;
    if (commands.size != 0 && required_size > commands.size) {
        return;
    }
    if (static_cast<uint64_t>(commands.offset) + required_size > command_buffer->size) {
        return;
    }
    auto* idx_buf = g_metal_buffer_pool.get(cmd->bound_index_buffer);
    if (!idx_buf || !idx_buf->buffer) {
        cmd->stats.dropped_draw_count += draw_count;
        return;
    }

    ++cmd->stats.indirect_draw_call_count;
    cmd->stats.draw_count += draw_count;
    cmd->stats.indexed_draw_count += draw_count;
    cmd->stats.draw_index_count += stats.index_count;
    cmd->stats.draw_instance_count += stats.instance_count;

    if (cmd->render_encoder) {
        // IndexedIndirectDrawCommand matches MTLDrawIndexedPrimitivesIndirectArguments byte-for-byte.
        const MTLIndexType idx_type = (cmd->bound_index_size == 2) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        for (uint32_t i = 0; i < draw_count; ++i) {
            const NSUInteger offset = static_cast<NSUInteger>(commands.offset)
                + static_cast<NSUInteger>(i) * static_cast<NSUInteger>(stride);
            [cmd->render_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                             indexType:idx_type
                                           indexBuffer:idx_buf->buffer
                                     indexBufferOffset:0
                                        indirectBuffer:command_buffer->buffer
                                  indirectBufferOffset:offset];
        }
    }
}

void cmd_draw_indexed_indirect_count(CommandList* cmd_ptr, IndirectDrawSpan commands, StorageSpan count_buffer,
    uint32_t max_draw_count, uint32_t stride)
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    auto* command_buffer = g_metal_buffer_pool.get(commands.buffer);
    auto* count = g_metal_buffer_pool.get(count_buffer.buffer);
    if (!cmd || !command_buffer || !command_buffer->buffer || !count || !count->buffer || max_draw_count == 0) {
        return;
    }
    if (cmd->pipeline_bind_failed || cmd->vertex_bind_failed || cmd->index_bind_failed || cmd->resource_bind_failed) {
        cmd->stats.dropped_draw_count += max_draw_count;
        return;
    }
    if (stride < sizeof(IndexedIndirectDrawCommand)) {
        return;
    }
    if (static_cast<uint64_t>(count_buffer.offset) + sizeof(uint32_t) > count->size) {
        return;
    }
    auto* idx_buf = g_metal_buffer_pool.get(cmd->bound_index_buffer);
    if (!idx_buf || !idx_buf->buffer) {
        cmd->stats.dropped_draw_count += max_draw_count;
        return;
    }

    ++cmd->stats.indirect_draw_call_count;
    if (!cmd->render_encoder) {
        return;
    }

    uint32_t draw_count = max_draw_count;
    if (count->cpu_visible && count->mapped) {
        uint32_t host_count = 0;
        std::memcpy(&host_count,
            static_cast<const uint8_t*>(count->mapped) + count_buffer.offset,
            sizeof(uint32_t));
        draw_count = std::min(host_count, max_draw_count);
    }
    // Clamp to available buffer capacity to avoid out-of-bounds indirect reads.
    if (commands.offset < command_buffer->size) {
        const uint64_t avail = command_buffer->size - commands.offset;
        if (avail < sizeof(IndexedIndirectDrawCommand)) {
            draw_count = 0;
        } else {
            const uint64_t fit = 1u + (avail - sizeof(IndexedIndirectDrawCommand)) / stride;
            draw_count = static_cast<uint32_t>(std::min<uint64_t>(draw_count, fit));
        }
    } else {
        draw_count = 0;
    }

    const MTLIndexType idx_type = (cmd->bound_index_size == 2) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
    for (uint32_t i = 0; i < draw_count; ++i) {
        const NSUInteger offset = static_cast<NSUInteger>(commands.offset)
            + static_cast<NSUInteger>(i) * static_cast<NSUInteger>(stride);
        [cmd->render_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                         indexType:idx_type
                                       indexBuffer:idx_buf->buffer
                                 indexBufferOffset:0
                                    indirectBuffer:command_buffer->buffer
                              indirectBufferOffset:offset];
    }
}

void cmd_dispatch(CommandList* cmd_ptr, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
{
    @autoreleasepool {
        auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
        if (!cmd) {
            return;
        }
        if (cmd->pipeline_bind_failed || cmd->resource_bind_failed) {
            ++cmd->stats.dropped_dispatch_count;
            return;
        }
        auto* pipeline = g_metal_pipeline_pool.get(cmd->bound_pipeline);
        if (!pipeline || !pipeline->is_compute || !pipeline->compute_state || !cmd->command_buffer || cmd->in_render_pass) {
            ++cmd->stats.dropped_dispatch_count;
            return;
        }

        if (!cmd->compute_encoder) {
            cmd->compute_encoder = [cmd->command_buffer computeCommandEncoder];
            if (!cmd->compute_encoder) {
                ++cmd->stats.dropped_dispatch_count;
                return;
            }
        }

        [cmd->compute_encoder setComputePipelineState:pipeline->compute_state];

        if (cmd->bound_resources.valid && cmd->bound_resources.compute) {
            if (pipeline->uses_draw_uniforms) {
                auto* uniform = g_metal_buffer_pool.get(cmd->bound_resources.uniforms.buffer);
                if (!uniform || !uniform->buffer) {
                    ++cmd->stats.dropped_dispatch_count;
                    return;
                }
                [cmd->compute_encoder setBuffer:uniform->buffer
                                         offset:cmd->bound_resources.uniforms.offset
                                        atIndex:1];
            }
            if (pipeline->uses_storage_buffer) {
                for (uint32_t i = 0; i < pipeline->storage_buffer_count; ++i) {
                    const auto& span = cmd->bound_resources.storages[i];
                    auto* buf = g_metal_buffer_pool.get(span.buffer);
                    if (!buf || !buf->buffer) {
                        ++cmd->stats.dropped_dispatch_count;
                        return;
                    }
                    [cmd->compute_encoder setBuffer:buf->buffer
                                             offset:span.offset
                                            atIndex:graphics_storage_binding_for_slot(i)];
                }
            }
        }

        ++cmd->stats.dispatch_count;
        if (group_count_x > 0 && group_count_y > 0 && group_count_z > 0) {
            const MTLSize groups = MTLSizeMake(group_count_x, group_count_y, group_count_z);
            const MTLSize threads = MTLSizeMake(
                pipeline->threadgroup_size[0],
                pipeline->threadgroup_size[1],
                pipeline->threadgroup_size[2]);
            [cmd->compute_encoder dispatchThreadgroups:groups threadsPerThreadgroup:threads];
        }
    }
}

// ============================================================================
// == Ring Allocators & Bindless ==============================================
// ============================================================================

UniformSpan allocate_uniform_span(Context* ctx_ptr, uint32_t size, uint32_t alignment)
{
    auto* ctx = as_metal(ctx_ptr);
    if (!ctx || size == 0) {
        return {};
    }

    const uint64_t actual_alignment = std::max<uint64_t>(alignment, kMetalMinBufferOffsetAlignment);
    if (!std::has_single_bit(actual_alignment)) {
        ++ctx->frame_stats.uniform_allocation_failure_count;
        return {};
    }
    auto& required = ctx->frame_stats.uniform_ring_required_bytes;
    const auto max_required = std::numeric_limits<uint64_t>::max();
    const uint64_t padding = -required & (actual_alignment - 1);
    required = required > max_required - padding - size
        ? max_required
        : required + padding + size;

    uint64_t& offset = ctx->uniform_ring_offset[ctx->frame_index];
    const uint64_t aligned_offset = align_up(offset, actual_alignment);
    if (aligned_offset + size > kUniformRingSize) {
        ++ctx->frame_stats.uniform_allocation_failure_count;
        return {};
    }

    offset = aligned_offset + size;
    ++ctx->frame_stats.uniform_allocation_count;
    ctx->frame_stats.uniform_allocation_bytes += size;

    UniformSpan span{};
    span.buffer = ctx->uniform_ring_handle[ctx->frame_index];
    span.offset = static_cast<uint32_t>(aligned_offset);
    span.size = size;
    span.data = static_cast<uint8_t*>(ctx->uniform_ring_mapped[ctx->frame_index]) + aligned_offset;
    return span;
}

VertexSpan allocate_vertex_span(Context* ctx_ptr, uint32_t size, uint32_t alignment)
{
    auto* ctx = as_metal(ctx_ptr);
    if (!ctx || size == 0) {
        return {};
    }

    const uint64_t actual_alignment = std::max<uint64_t>(alignment, 1);
    uint64_t& offset = ctx->vertex_ring_offset[ctx->frame_index];
    const uint64_t aligned_offset = align_up(offset, actual_alignment);
    if (aligned_offset + size > kVertexRingSize) {
        return {};
    }

    offset = aligned_offset + size;
    ++ctx->frame_stats.vertex_allocation_count;
    ctx->frame_stats.vertex_allocation_bytes += size;

    VertexSpan span{};
    span.buffer = ctx->vertex_ring_handle[ctx->frame_index];
    span.offset = static_cast<uint32_t>(aligned_offset);
    span.size = size;
    span.data = static_cast<uint8_t*>(ctx->vertex_ring_mapped[ctx->frame_index]) + aligned_offset;
    return span;
}

BindlessTextureIndex get_bindless_texture_index(Context* ctx_ptr, Handle<Texture> texture)
{
    auto* ctx = as_metal(ctx_ptr);
    auto* tex = ctx ? ctx->texture_pool_.get(texture) : nullptr;
    return tex ? tex->bindless_slot : kInvalidBindlessTextureIndex;
}

// ============================================================================
// == Screenshot Readback =====================================================
// ============================================================================

bool capture_screenshot(Context* ctx_ptr, CommandList* cmd_ptr, const char* path)
{
    @autoreleasepool {
        auto* ctx = as_metal(ctx_ptr);
        auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
        if (!ctx || !cmd || !cmd->command_buffer || !path) {
            return false;
        }

        if (cmd->in_render_pass) {
            cmd_end_render(cmd_ptr);
        }
        end_compute_encoder_if_active(cmd);

        id<MTLTexture> source_tex = nil;
        bool is_bgra = false;
        if (ctx->headless) {
            auto* rt = ctx->render_target_pool_.get(ctx->default_render_target);
            auto* color_tex = rt ? ctx->texture_pool_.get(rt->desc.color.texture) : nullptr;
            if (!color_tex || !color_tex->texture) {
                return false;
            }
            source_tex = color_tex->texture;
        } else {
            if (!ctx->current_drawable || !ctx->current_drawable.texture) {
                return false;
            }
            source_tex = ctx->current_drawable.texture;
            is_bgra = (ctx->swapchain_format == MTLPixelFormatBGRA8Unorm
                || ctx->swapchain_format == MTLPixelFormatBGRA8Unorm_sRGB);
        }

        const uint32_t width = static_cast<uint32_t>(source_tex.width);
        const uint32_t height = static_cast<uint32_t>(source_tex.height);
        const size_t bytes_per_row = static_cast<size_t>(width) * 4u;
        const size_t buffer_size = bytes_per_row * height;

        id<MTLBuffer> readback = [ctx->core->device newBufferWithLength:buffer_size
                                                               options:MTLResourceStorageModeShared];
        if (!readback) {
            return false;
        }

        id<MTLBlitCommandEncoder> blit = [cmd->command_buffer blitCommandEncoder];
        if (!blit) {
            return false;
        }
        [blit copyFromTexture:source_tex
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(width, height, 1)
                     toBuffer:readback
            destinationOffset:0
       destinationBytesPerRow:bytes_per_row
     destinationBytesPerImage:buffer_size];
        [blit endEncoding];

        end_frame(ctx_ptr);
        wait_idle(ctx_ptr);

        std::vector<uint8_t> rgba(buffer_size);
        const auto* src = static_cast<const uint8_t*>([readback contents]);
        if (is_bgra) {
            for (size_t i = 0; i < buffer_size; i += 4) {
                rgba[i + 0] = src[i + 2];
                rgba[i + 1] = src[i + 1];
                rgba[i + 2] = src[i + 0];
                rgba[i + 3] = src[i + 3];
            }
        } else {
            std::memcpy(rgba.data(), src, buffer_size);
        }

        std::filesystem::path output_path{path};
        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        return stbi_write_png(path, static_cast<int>(width), static_cast<int>(height), 4,
                   rgba.data(), static_cast<int>(bytes_per_row))
            != 0;
    }
}

// ============================================================================
// == White-Box Test Hooks ====================================================
// ============================================================================

namespace metal_test {

std::vector<uint8_t> read_texture_mips(Context* ctx_ptr, Handle<Texture> handle, size_t bytes_per_pixel, bool depth_aspect)
{
    @autoreleasepool {
        (void)depth_aspect;
        auto* ctx = as_metal(ctx_ptr);
        auto* image = ctx ? ctx->texture_pool_.get(handle) : nullptr;
        if (!image || !image->texture) {
            return {};
        }

        struct MipCopy {
            size_t offset = 0;
            uint32_t level = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            size_t bytes_per_row = 0;
            size_t bytes_per_image = 0;
        };

        std::vector<MipCopy> copies;
        size_t total_size = 0;
        uint32_t w = image->width;
        uint32_t h = image->height;
        for (uint32_t level = 0; level < image->mip_levels; ++level) {
            const size_t row_bytes = static_cast<size_t>(w) * bytes_per_pixel;
            const size_t img_bytes = row_bytes * h;
            copies.push_back({total_size, level, w, h, row_bytes, img_bytes});
            total_size += img_bytes;
            w = std::max(1u, w / 2u);
            h = std::max(1u, h / 2u);
        }

        id<MTLBuffer> readback = [ctx->core->device newBufferWithLength:total_size
                                                               options:MTLResourceStorageModeShared];
        if (!readback) {
            return {};
        }

        id<MTLCommandBuffer> cb = [ctx->core->command_queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        for (const auto& c : copies) {
            [blit copyFromTexture:image->texture
                      sourceSlice:0
                      sourceLevel:c.level
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(c.width, c.height, 1)
                         toBuffer:readback
                destinationOffset:c.offset
           destinationBytesPerRow:c.bytes_per_row
         destinationBytesPerImage:c.bytes_per_image];
        }
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        check_command_buffer_error(ctx->core, cb, "metal_test::read_texture_mips");

        std::vector<uint8_t> out(total_size);
        std::memcpy(out.data(), [readback contents], total_size);
        return out;
    }
}

bool texture_is_uploaded(Context* ctx_ptr, Handle<Texture> handle) noexcept
{
    auto* ctx = as_metal(ctx_ptr);
    const auto* tex = ctx ? ctx->texture_pool_.get(handle) : nullptr;
    return tex && tex->uploaded;
}

void inject_texture_extent(Context* ctx_ptr, Handle<Texture> handle, uint32_t width, uint32_t height) noexcept
{
    auto* ctx = as_metal(ctx_ptr);
    if (auto* tex = ctx ? ctx->texture_pool_.get(handle) : nullptr) {
        tex->width = width;
        tex->height = height;
    }
}

uint32_t min_uniform_buffer_alignment(Context* ctx) noexcept
{
    (void)ctx;
    return kMetalMinBufferOffsetAlignment;
}

uint32_t min_storage_buffer_alignment(Context* ctx) noexcept
{
    (void)ctx;
    return kMetalMinBufferOffsetAlignment;
}

void set_uniform_ring_required_bytes(Context* ctx_ptr, uint64_t value) noexcept
{
    if (auto* ctx = as_metal(ctx_ptr)) {
        ctx->frame_stats.uniform_ring_required_bytes = value;
    }
}

void set_descriptor_ring_offset(Context* ctx_ptr, CommandList* cmd_ptr, uint64_t value) noexcept
{
    auto* ctx = as_metal(ctx_ptr);
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    if (ctx && cmd) {
        ctx->descriptor_ring_offset[cmd->pool_index] = value;
    }
}

uint64_t get_descriptor_ring_offset(Context* ctx_ptr, CommandList* cmd_ptr) noexcept
{
    auto* ctx = as_metal(ctx_ptr);
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    return (ctx && cmd) ? ctx->descriptor_ring_offset[cmd->pool_index] : 0;
}

bool command_list_resource_bind_failed(CommandList* cmd_ptr) noexcept
{
    auto* cmd = reinterpret_cast<MetalCommandList*>(cmd_ptr);
    return cmd && cmd->resource_bind_failed;
}

void set_buffer_core_null(Handle<Buffer> handle, bool clear_to_null, Context* restore_ctx) noexcept
{
    if (auto* buf = g_metal_buffer_pool.get(handle)) {
        auto* ctx = as_metal(restore_ctx);
        buf->core = clear_to_null ? nullptr : (ctx ? ctx->core : nullptr);
    }
}

size_t reserve_all_but_one_buffer_handles(std::vector<Handle<Buffer>>& out)
{
    out.clear();
    out.reserve(g_metal_buffer_pool.max_capacity());
    for (;;) {
        const auto handle = g_metal_buffer_pool.insert(MetalBuffer{});
        if (!handle.valid()) {
            break;
        }
        out.push_back(handle);
    }
    if (!out.empty()) {
        g_metal_buffer_pool.destroy(out.back());
        out.pop_back();
    }
    return out.size();
}

void release_reserved_buffer_handles(const std::vector<Handle<Buffer>>& handles)
{
    for (const auto handle : handles) {
        g_metal_buffer_pool.destroy(handle);
    }
}

void get_live_buffer_allocations(Context* ctx_ptr, uint64_t& count, uint64_t& bytes) noexcept
{
    auto* ctx = as_metal(ctx_ptr);
    if (!ctx || !ctx->core) {
        count = 0;
        bytes = 0;
        return;
    }
    count = ctx->core->live_buffer_allocation_count;
    bytes = ctx->core->live_buffer_allocation_bytes;
}

} // namespace metal_test

} // namespace nw::gfx
