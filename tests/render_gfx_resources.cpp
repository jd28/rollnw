#include <nw/formats/Image.hpp>
#include <nw/gfx/backends/vulkan/vulkan_internal.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/render/local_shadow_renderer.hpp>
#include <nw/render/model_asset.hpp>
#include <nw/render/model_renderer.hpp>
#include <nw/render/render_service.hpp>
#include <nw/render/shader_provider.hpp>
#include <nw/render/shadow_renderer.hpp>
#include <nw/render/viewer/preview_model_draws.hpp>
#include <nw/render/viewer/preview_scene.hpp>
#include <nw/render/viewer/scene_shadow.hpp>
#include <nw/util/platform.hpp>
#include <nw/util/scope_exit.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace {
using namespace nw::gfx;

class RenderGfxResources : public ::testing::Test {
protected:
    void SetUp() override
    {
        owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
        if (owns_video) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
            ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
        }
        core = create_core({.app_name = "gfx-resource-tests", .enable_validation = true});
        ASSERT_NE(core, nullptr);
        context = create_context(core, {.width = 64, .height = 64});
        ASSERT_NE(context, nullptr);
    }

    void TearDown() override
    {
        if (context) {
            wait_idle(context);
            for (auto texture : textures) {
                destroy_texture(context, texture);
            }
            destroy_context(context);
        }
        if (core) {
            const auto report = validation_report(core);
            EXPECT_EQ(report.error_count, 0u) << report.first_error;
            EXPECT_EQ(report.warning_count, 0u) << report.first_warning;
            destroy_core(core);
        }
        if (owns_video) { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
    }

    Handle<Texture> texture(Fmt format, uint32_t levels = 1, uint32_t layers = 1, bool target = false)
    {
        auto result = create_texture(context, {.width = 4, .height = 4, .layers = layers, .mip_levels = levels, .format = format, .sampled = format != Fmt::D32FS8, .render_target = target});
        EXPECT_TRUE(result.valid());
        textures.push_back(result);
        return result;
    }

    std::vector<uint8_t> read_mips(Handle<Texture> handle, size_t bytes_per_pixel,
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
    {
        auto* vk = as_vulkan(context);
        auto* image = vk->texture_pool_.get(handle);
        if (!image) {
            ADD_FAILURE();
            return {};
        }
        std::vector<VkBufferImageCopy> regions;
        size_t size = 0;
        uint32_t width = image->width;
        uint32_t height = image->height;
        for (uint32_t level = 0; level < image->mip_levels; ++level) {
            VkBufferImageCopy region{};
            region.bufferOffset = size;
            region.imageSubresource = {aspect, level, 0, 1};
            region.imageExtent = {width, height, 1};
            regions.push_back(region);
            size += static_cast<size_t>(width) * height * bytes_per_pixel;
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        auto destination = create_buffer(context, {.size = size, .usage = BufferUsage::TransferDst, .cpu_visible = true});
        EXPECT_TRUE(destination.valid());
        auto* buffer = g_buffer_pool.get(destination);
        auto* commands = reinterpret_cast<VulkanCommandList*>(begin_frame(context));
        EXPECT_NE(commands, nullptr);
        if (!commands || !buffer) {
            destroy_buffer(destination);
            return {};
        }
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image->image;
        barrier.subresourceRange = {aspect, 0, image->mip_levels, 0, 1};
        vkCmdPipelineBarrier(commands->buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        vkCmdCopyImageToBuffer(commands->buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            buffer->buffer, static_cast<uint32_t>(regions.size()), regions.data());
        std::swap(barrier.oldLayout, barrier.newLayout);
        std::swap(barrier.srcAccessMask, barrier.dstAccessMask);
        vkCmdPipelineBarrier(commands->buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        end_frame(context);
        wait_idle(context);
        auto* mapped = map_buffer(destination);
        EXPECT_NE(mapped, nullptr);
        EXPECT_EQ(vmaInvalidateAllocation(vk->core->allocator, buffer->allocation, 0, VK_WHOLE_SIZE), VK_SUCCESS);
        std::vector<uint8_t> result(size);
        if (mapped) { std::memcpy(result.data(), mapped, size); }
        unmap_buffer(destination);
        destroy_buffer(destination);
        return result;
    }

    Core* core = nullptr;
    Context* context = nullptr;
    bool owns_video = false;
    std::vector<Handle<Texture>> textures;
};

TEST_F(RenderGfxResources, EmptyAndMixedBatchesGenerateAndCopyAllMips)
{
    const auto before = resource_stats(context);
    EXPECT_TRUE(upload_textures(context, {}));
    EXPECT_EQ(resource_stats(context).upload_batch_count, before.upload_batch_count);
    const auto rgba = texture(Fmt::RGBA8Srgb, 3);
    const auto half = texture(Fmt::RGBA16F, 3);
    std::array<uint8_t, 64> rgba_pixels{};
    rgba_pixels.fill(255);
    std::array<uint16_t, 64> half_base{};
    half_base.fill(0x3c00); // 1.0, exact half-float
    std::array<uint16_t, 16> half_mid{};
    half_mid.fill(0x3800); // 0.5
    std::array<uint16_t, 4> half_last{};
    half_last.fill(0x3400); // 0.25
    const TextureMipData base{rgba_pixels.data(), sizeof(rgba_pixels), 4, 4};
    const std::array half_mips{TextureMipData{half_base.data(), sizeof(half_base), 4, 4},
        TextureMipData{half_mid.data(), sizeof(half_mid), 2, 2},
        TextureMipData{half_last.data(), sizeof(half_last), 1, 1}};
    const std::array uploads{TextureUpload{rgba, {&base, 1}}, TextureUpload{half, half_mips}};
    ASSERT_TRUE(upload_textures(context, uploads));
    const auto after = resource_stats(context);
    EXPECT_EQ(after.upload_texture_count - before.upload_texture_count, 2u);
    EXPECT_EQ(after.upload_mip_count - before.upload_mip_count, 6u);
    EXPECT_EQ(after.upload_bytes - before.upload_bytes, 232u);
    EXPECT_EQ(after.staging_allocation_count - before.staging_allocation_count, 1u);
    EXPECT_EQ(after.upload_command_pool_count - before.upload_command_pool_count, 1u);
    EXPECT_EQ(after.upload_command_buffer_count - before.upload_command_buffer_count, 1u);
    EXPECT_EQ(after.upload_submission_count - before.upload_submission_count, 1u);
    EXPECT_EQ(after.upload_wait_count - before.upload_wait_count, 1u);
    EXPECT_EQ(read_mips(rgba, 4), std::vector<uint8_t>(84, 255));
    const auto bytes = read_mips(half, 8);
    ASSERT_EQ(bytes.size(), 168u);
    EXPECT_EQ(std::memcmp(bytes.data(), half_base.data(), sizeof(half_base)), 0);
    EXPECT_EQ(std::memcmp(bytes.data() + sizeof(half_base), half_mid.data(), sizeof(half_mid)), 0);
    EXPECT_EQ(std::memcmp(bytes.data() + sizeof(half_base) + sizeof(half_mid), half_last.data(), sizeof(half_last)), 0);
    ASSERT_TRUE(upload_texture_rgba16f(context, half, half_base.data(), sizeof(half_base)));
    const auto generated = read_mips(half, 8);
    for (size_t i = 0; i < generated.size(); i += 2) {
        EXPECT_EQ(generated[i], 0u);
        EXPECT_EQ(generated[i + 1], 0x3cu);
    }
}

TEST_F(RenderGfxResources, RejectedBatchesPreserveEveryTextureAndSubmitNothing)
{
    const auto first = texture(Fmt::RGBA8);
    const auto second = texture(Fmt::RGBA8);
    std::array<uint8_t, 64> pixels{};
    pixels.fill(37);
    ASSERT_TRUE(upload_texture_rgba8(context, first, pixels.data(), pixels.size()));
    ASSERT_TRUE(upload_texture_rgba8(context, second, pixels.data(), pixels.size()));
    pixels.fill(98);
    TextureMipData mip{pixels.data(), pixels.size(), 4, 4};
    std::array uploads{TextureUpload{first, {&mip, 1}}, TextureUpload{second, {&mip, 1}}};
    const auto before = resource_stats(context);
    const auto reject = [&](std::span<const TextureUpload> rows) {
        EXPECT_FALSE(upload_textures(context, rows));
        EXPECT_EQ(resource_stats(context).upload_submission_count, before.upload_submission_count);
        EXPECT_EQ(resource_stats(context).staging_allocation_count, before.staging_allocation_count);
        EXPECT_EQ(as_vulkan(context)->texture_pool_.get(first)->layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        EXPECT_EQ(as_vulkan(context)->texture_pool_.get(second)->layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    };
    uploads[1].texture = {};
    reject(uploads);
    auto stale = texture(Fmt::RGBA8);
    destroy_texture(context, stale);
    uploads[1].texture = stale;
    reject(uploads);
    uploads[1].texture = first;
    reject(uploads);
    uploads[1].texture = second;
    uploads[1].mips = {};
    reject(uploads);
    uploads[1].mips = {&mip, 1};
    mip.data = nullptr;
    reject(uploads);
    mip.data = pixels.data();
    --mip.size;
    reject(uploads);
    ++mip.size;
    mip.width = 3;
    reject(uploads);
    mip.width = 4;
    uploads[1].texture = texture(Fmt::D32F);
    reject(uploads);
    uploads[1].texture = texture(Fmt::RGBA8, 1, 2);
    reject(uploads);
    uploads[1].texture = texture(Fmt::RGBA8, 3);
    std::array bad_mips{mip, TextureMipData{pixels.data(), 16, 3, 2}, TextureMipData{pixels.data(), 4, 1, 1}};
    uploads[1].mips = std::span{bad_mips}.first(2);
    reject(uploads);
    uploads[1].mips = bad_mips;
    reject(uploads);
    EXPECT_EQ(read_mips(first, 4), std::vector<uint8_t>(64, 37));
    EXPECT_EQ(read_mips(second, 4), std::vector<uint8_t>(64, 37));
}

TEST_F(RenderGfxResources, RejectsByteProductAndAlignedTotalOverflowBeforeReadingPixels)
{
    // Inject impossible metadata at the backend boundary to exercise checked
    // arithmetic without allocating an impossible GPU image or CPU payload.
    const auto first = texture(Fmt::RGBA16F);
    const auto second = texture(Fmt::RGBA16F);
    auto* vk = as_vulkan(context);
    auto* a = vk->texture_pool_.get(first);
    auto* b = vk->texture_pool_.get(second);
    if (!a || !b) { FAIL() << "missing test textures"; }
    a->width = b->width = 1u << 31;
    a->height = b->height = 1u << 30;
    uint8_t byte = 0;
    TextureMipData mip{&byte, 0, a->width, a->height};
    std::array uploads{TextureUpload{first, {&mip, 1}}, TextureUpload{second, {&mip, 1}}};
    EXPECT_FALSE(upload_textures(context, uploads));
    a->height = b->height = mip.height = 1u << 29;
    mip.size = size_t{1} << 63;
    EXPECT_FALSE(upload_textures(context, uploads));
    EXPECT_EQ(resource_stats(context).upload_submission_count, 0u);
    EXPECT_EQ(resource_stats(context).staging_allocation_count, 0u);
    EXPECT_EQ(a->layout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(b->layout, VK_IMAGE_LAYOUT_UNDEFINED);
    a->width = a->height = b->width = b->height = 4;
}

TEST_F(RenderGfxResources, ContextCardinalityAndRenderTargetContractsAreExplicit)
{
    EXPECT_EQ(create_context(core, {.width = 64, .height = 64}), nullptr);
    EXPECT_EQ(create_context(nullptr, {.width = 64, .height = 64}), nullptr);
    const auto color = texture(Fmt::RGBA8, 1, 1, true);
    const auto depth = texture(Fmt::D32FS8, 1, 1, true);
    EXPECT_FALSE(create_render_target(context, {}).valid());
    EXPECT_FALSE(create_render_target(context, {.color = {depth}}).valid());
    EXPECT_FALSE(create_render_target(context, {.depth = {color}}).valid());
    EXPECT_FALSE(create_render_target(context, {.color = {texture(Fmt::RGBA8)}}).valid());
    EXPECT_FALSE(create_render_target(context, {.color = {texture(Fmt::RGBA8, 2, 1, true)}}).valid());
    EXPECT_FALSE(create_render_target(context, {.color = {texture(Fmt::RGBA8, 1, 2, true)}}).valid());
    const auto mismatched = create_texture(context, {.width = 8, .height = 4, .format = Fmt::D32F, .render_target = true});
    ASSERT_TRUE(mismatched.valid());
    textures.push_back(mismatched);
    EXPECT_FALSE(create_render_target(context, {.color = {color}, .depth = {mismatched}}).valid());
    const auto target = create_render_target(context, {.color = {color}, .depth = {depth}});
    ASSERT_TRUE(target.valid());
    destroy_render_target(context, target);
    EXPECT_FALSE(create_render_target(context, {.color = {color}}).valid());
    for (bool depth_only : {false, true}) {
        RenderTargetDesc desc{};
        if (depth_only) {
            desc.depth.texture = texture(Fmt::D32F, 1, 1, true);
        } else {
            desc.color.texture = texture(Fmt::RGBA8, 1, 1, true);
        }
        auto single = create_render_target(context, desc);
        ASSERT_TRUE(single.valid());
        auto* cmd = begin_frame(context);
        ASSERT_NE(cmd, nullptr);
        cmd_begin_render(cmd, single);
        cmd_end_render(cmd);
        end_frame(context);
        wait_idle(context);
        destroy_render_target(context, single);
    }
    wait_idle(context);
    for (auto handle : textures) {
        destroy_texture(context, handle);
    }
    textures.clear();
    destroy_context(context);
    context = create_context(core, {.width = 64, .height = 64});
    ASSERT_NE(context, nullptr);
    // Old texture handles belong to the destroyed context, not its successor.
    textures.clear();
}

TEST_F(RenderGfxResources, UniformDemandIncludesFailedRequestsAndResetsEachFrame)
{
    auto* cmd = begin_frame(context);
    ASSERT_NE(cmd, nullptr);
    CommandStats stats;
    ASSERT_TRUE(get_command_stats(cmd, stats));
    ASSERT_GT(stats.uniform_ring_capacity_bytes, 0u);
    ASSERT_LT(stats.uniform_ring_capacity_bytes, std::numeric_limits<uint32_t>::max());
    const auto capacity = static_cast<uint32_t>(stats.uniform_ring_capacity_bytes);
    const auto alignment = static_cast<uint32_t>(std::max<VkDeviceSize>(4096,
        as_vulkan(context)->core->properties.limits.minUniformBufferOffsetAlignment));
    ASSERT_GT(capacity, 2u * alignment);
    ASSERT_EQ(capacity % alignment, 0u);
    EXPECT_FALSE(allocate_uniform_span(nullptr, 1).buffer.valid());
    EXPECT_FALSE(allocate_uniform_span(context, 0).buffer.valid());

    const auto first = allocate_uniform_span(context, 1, alignment);
    const auto second = allocate_uniform_span(context, 3, alignment);
    ASSERT_NE(first.data, nullptr);
    ASSERT_NE(second.data, nullptr);
    EXPECT_EQ(first.offset, 0u);
    EXPECT_EQ(second.offset, alignment);
    ASSERT_TRUE(get_command_stats(cmd, stats));
    EXPECT_EQ(stats.uniform_allocation_count, 2u);
    EXPECT_EQ(stats.uniform_allocation_bytes, 4u);
    EXPECT_EQ(stats.uniform_ring_required_bytes, alignment + 3u);
    EXPECT_EQ(stats.uniform_allocation_failure_count, 0u);

    // Rejection must not consume physical space. A smaller request still fits.
    EXPECT_FALSE(allocate_uniform_span(context, capacity + 1, alignment).buffer.valid());
    const auto remaining = allocate_uniform_span(context, capacity - 2 * alignment, alignment);
    ASSERT_NE(remaining.data, nullptr);
    EXPECT_EQ(remaining.offset, 2u * alignment);
    EXPECT_EQ(remaining.offset + remaining.size, capacity);
    EXPECT_FALSE(allocate_uniform_span(context, 1, alignment).buffer.valid());
    EXPECT_FALSE(allocate_uniform_span(context, 7, alignment).buffer.valid());
    ASSERT_TRUE(get_command_stats(cmd, stats));
    EXPECT_EQ(stats.uniform_allocation_count, 3u);
    EXPECT_EQ(stats.uniform_allocation_bytes, capacity - 2u * alignment + 4u);
    EXPECT_EQ(stats.uniform_allocation_failure_count, 3u);
    EXPECT_EQ(stats.uniform_ring_required_bytes, 2ull * capacity + 2ull * alignment + 7);
    EXPECT_EQ(stats.draw_count, 0u);
    EXPECT_EQ(stats.dropped_draw_count, 0u); // No draw was issued to count as dropped.

    EXPECT_FALSE(allocate_uniform_span(context, 1, alignment + 1).buffer.valid());
    EXPECT_FALSE(allocate_uniform_span(context, std::numeric_limits<uint32_t>::max(), alignment).buffer.valid());
    ASSERT_TRUE(get_command_stats(cmd, stats));
    EXPECT_EQ(stats.uniform_allocation_failure_count, 5u);
    EXPECT_EQ(stats.uniform_ring_required_bytes,
        2ull * capacity + 3ull * alignment + std::numeric_limits<uint32_t>::max());
    as_vulkan(context)->frame_stats.uniform_ring_required_bytes = std::numeric_limits<uint64_t>::max() - alignment + 1;
    EXPECT_FALSE(allocate_uniform_span(context, 1, alignment).buffer.valid());
    ASSERT_TRUE(get_command_stats(cmd, stats));
    EXPECT_EQ(stats.uniform_ring_required_bytes, std::numeric_limits<uint64_t>::max() - alignment + 2);
    EXPECT_FALSE(allocate_uniform_span(context, 1, alignment).buffer.valid());
    ASSERT_TRUE(get_command_stats(cmd, stats));
    EXPECT_EQ(stats.uniform_ring_required_bytes, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(stats.uniform_allocation_failure_count, 7u);
    end_frame(context);

    // Includes reuse of the original in-flight slot, not just a fresh slot.
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        cmd = begin_frame(context);
        ASSERT_NE(cmd, nullptr);
        ASSERT_TRUE(get_command_stats(cmd, stats));
        EXPECT_EQ(stats.uniform_ring_capacity_bytes, capacity);
        EXPECT_EQ(stats.uniform_ring_required_bytes, 0u);
        EXPECT_EQ(stats.uniform_allocation_count, 0u);
        EXPECT_EQ(stats.uniform_allocation_bytes, 0u);
        EXPECT_EQ(stats.uniform_allocation_failure_count, 0u);
        const auto full = allocate_uniform_span(context, capacity, alignment);
        ASSERT_NE(full.data, nullptr);
        EXPECT_EQ(full.offset, 0u);
        ASSERT_TRUE(get_command_stats(cmd, stats));
        EXPECT_EQ(stats.uniform_ring_required_bytes, capacity);
        EXPECT_EQ(stats.uniform_allocation_failure_count, 0u);
        end_frame(context);
    }
}

TEST_F(RenderGfxResources, FailedBindsCannotReuseCachedStateAndRebindsRecoverIndependently)
{
    nw::ResourceManager resman{nw::kernel::global_allocator()};
    const auto root = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/render";
    ASSERT_TRUE(resman.add_base_container(root, "shaders", nw::ResourceType::hlsl));
    resman.build_registry();
    nw::render::ShaderProvider shaders{context, &resman};
    ASSERT_TRUE(shaders.initialize());
    PipelineDesc desc{};
    desc.vs = shaders.get_shader("render_debug_shape.vs.hlsl");
    desc.fs = shaders.get_shader("render_debug_shape.ps.hlsl");
    desc.uses_single_texture = false;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.use_swapchain_color_format = false;
    desc.vertex_stride = 28;
    desc.vertex_attributes = {{0, 0, VertexFormat::Float3}, {1, 12, VertexFormat::Float4}};
    auto pipeline = create_pipeline(context, desc);
    ASSERT_TRUE(pipeline.valid());
    auto vertices = create_buffer(context, {.size = 84, .usage = BufferUsage::Vertex, .cpu_visible = true});
    auto indices = create_buffer(context, {.size = 12, .usage = BufferUsage::Index, .cpu_visible = true});
    std::memset(map_buffer(vertices), 0, 84);
    unmap_buffer(vertices);
    const std::array<uint32_t, 3> index_data{0, 1, 2};
    std::memcpy(map_buffer(indices), index_data.data(), 12);
    unmap_buffer(indices);
    auto stale_pipeline = create_pipeline(context, desc);
    destroy_pipeline(context, stale_pipeline);
    auto stale_buffer = create_buffer(context, {.size = 84, .usage = BufferUsage::Vertex, .cpu_visible = true});
    destroy_buffer(stale_buffer);
    auto indirect = create_buffer(context, {.size = sizeof(IndexedIndirectDrawCommand), .usage = BufferUsage::Indirect, .cpu_visible = true});
    auto count_buffer = create_buffer(context, {.size = 4, .usage = BufferUsage::Indirect, .cpu_visible = true});
    auto* cmd = begin_frame(context);
    ASSERT_NE(cmd, nullptr);
    cmd_begin_render(cmd, {});
    cmd_set_viewport(cmd, 0, 0, 64, 64, 0, 1);
    cmd_set_scissor(cmd, 0, 0, 64, 64);
    auto uniforms = allocate_uniform_span(context, 128);
    ASSERT_NE(uniforms.data, nullptr);
    std::memset(uniforms.data, 0, 128);
    const auto bind_resources = [&] { cmd_bind_uniform_texture(cmd, pipeline, uniforms, {}); };
    cmd_bind_pipeline(cmd, pipeline);
    cmd_bind_vertex_buffer(cmd, vertices, 28);
    cmd_bind_index_buffer(cmd, indices, 4);
    bind_resources();
    cmd_draw_indexed(cmd, 3, 1);
    cmd_bind_pipeline(cmd, stale_pipeline);
    bind_resources(); // Must not clear the pipeline failure.
    cmd_draw_indexed(cmd, 3, 1);
    cmd_bind_pipeline(cmd, pipeline);
    cmd_bind_vertex_buffer(cmd, stale_buffer, 28);
    bind_resources();
    cmd_draw_indexed(cmd, 3, 1);
    cmd_bind_vertex_buffer(cmd, vertices, 28);
    cmd_bind_index_buffer(cmd, indices, 3);
    bind_resources();
    cmd_draw_indexed_base_instance(cmd, 3, 0, 1);
    cmd_bind_index_buffer(cmd, indices, 4);
    cmd_bind_uniform_texture(cmd, {}, uniforms, {});
    cmd_bind_pipeline(cmd, pipeline); // Must not clear the resource failure.
    cmd_draw(cmd, 3, 1);
    bind_resources();
    cmd_draw_indexed(cmd, 3, 1);
    CommandStats stats;
    ASSERT_TRUE(get_command_stats(cmd, stats));
    EXPECT_EQ(stats.draw_count, 2u);
    EXPECT_EQ(stats.dropped_draw_count, 4u);
    EXPECT_EQ(stats.pipeline_bind_failure_count, 1u);
    EXPECT_EQ(stats.vertex_buffer_bind_failure_count, 1u);
    EXPECT_EQ(stats.index_buffer_bind_failure_count, 1u);
    EXPECT_EQ(stats.resource_bind_failure_count, 1u);
    cmd_bind_pipeline(cmd, stale_pipeline);
    const IndirectDrawSpan indirect_rows{indirect, 0, sizeof(IndexedIndirectDrawCommand)};
    cmd_draw_indexed_indirect(cmd, indirect_rows, 1, sizeof(IndexedIndirectDrawCommand));
    cmd_draw_indexed_indirect_count(cmd, indirect_rows, {count_buffer, 0, 4}, 1, sizeof(IndexedIndirectDrawCommand));
    cmd_dispatch(cmd, 1, 1, 1);
    ASSERT_TRUE(get_command_stats(cmd, stats));
    EXPECT_EQ(stats.dropped_draw_count, 6u);
    EXPECT_EQ(stats.dropped_dispatch_count, 1u);
    EXPECT_EQ(stats.dispatch_count, 0u);
    cmd_end_render(cmd);
    end_frame(context);
    wait_idle(context);
    destroy_buffer(indirect);
    destroy_buffer(count_buffer);
    destroy_buffer(vertices);
    destroy_buffer(indices);
    destroy_pipeline(context, pipeline);
}

TEST_F(RenderGfxResources, ShadowFiltersPreserveNonUniformTextureSelection)
{
    const auto temp = nw::create_unique_tmp_path();
    const auto cleanup = create_scope_exit([&] { std::filesystem::remove_all(temp); });
    const auto shader_dir = temp / "shaders";
    std::filesystem::create_directory(shader_dir);
    std::ofstream{shader_dir / "shadow_filter.vs.hlsl"} << R"(
float4 main(float2 position : POSITION) : SV_Position {
    return float4(position, 0.0, 1.0);
}
)";
    std::ofstream{shader_dir / "shadow_filter.ps.hlsl"} << R"(
Texture2D<float4> g_textures[] : register(t2, space1);
#include "scene_constants.inc.hlsl"
#include "scene_shadow.inc.hlsl"
float4 main(float4 position : SV_Position) : SV_Target {
    const uint index = scene_shadow_texture_indices[uint(position.x) & 1u];
    const float box = scene_shadow_sample_map(index, float2(0.5, 0.5), 0.5, 0.0);
    const float tent = scene_shadow_sample_map_tent(index, float2(0.5, 0.5), 0.5, 0.0, 1.0);
    return float4(box, tent, 0.0, 1.0);
}
)";
    nw::ResourceManager resman{nw::kernel::global_allocator()};
    ASSERT_TRUE(resman.add_base_container(temp, "shaders", nw::ResourceType::hlsl));
    ASSERT_TRUE(resman.add_base_container(
        std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/render", "shaders", nw::ResourceType::hlsl));
    resman.build_registry();
    nw::render::ShaderProvider shaders{context, &resman};
    ASSERT_TRUE(shaders.initialize());
    PipelineDesc desc{};
    desc.vs = shaders.get_shader("shadow_filter.vs.hlsl");
    desc.fs = shaders.get_shader("shadow_filter.ps.hlsl");
    desc.uses_single_texture = false;
    desc.uses_bindless_sampled_textures = true;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.blend_mode = BlendMode::disabled;
    desc.use_swapchain_color_format = false;
    desc.vertex_stride = sizeof(glm::vec2);
    desc.vertex_attributes = {{0, 0, VertexFormat::Float2}};
    const auto pipeline = create_pipeline(context, desc);
    ASSERT_TRUE(pipeline.valid());
    const auto release_pipeline = create_scope_exit([&] { destroy_pipeline(context, pipeline); });
    const std::array<glm::vec2, 3> positions{{{-1, -1}, {3, -1}, {-1, 3}}};
    const auto vertices = create_buffer(context, {.size = sizeof(positions), .usage = BufferUsage::Vertex, .cpu_visible = true});
    ASSERT_TRUE(vertices.valid());
    const auto release_vertices = create_scope_exit([&] { destroy_buffer(vertices); });
    auto* mapped = map_buffer(vertices);
    ASSERT_NE(mapped, nullptr);
    std::memcpy(mapped, positions.data(), sizeof(positions));
    unmap_buffer(vertices);

    // Every pixel in each map has the same depth. All taps must therefore agree,
    // even when adjacent fragment lanes select different bindless textures.
    nw::render::SceneConstants constants{};
    for (uint32_t i = 0; i < 2; ++i) {
        const auto map = texture(Fmt::RGBA8);
        std::array<uint8_t, 64> pixels{};
        pixels.fill(i == 0 ? 0 : 255);
        ASSERT_TRUE(upload_texture_rgba8(context, map, pixels.data(), pixels.size()));
        constants.scene_shadow_texture_indices[i] = get_bindless_texture_index(context, map);
        ASSERT_NE(constants.scene_shadow_texture_indices[i], kInvalidBindlessTextureIndex);
    }
    auto* cmd = begin_frame(context);
    ASSERT_NE(cmd, nullptr);
    cmd_begin_render(cmd, {}, RenderLoadOp::clear);
    cmd_set_viewport(cmd, 0, 0, 64, 64);
    cmd_set_scissor(cmd, 0, 0, 64, 64);
    const auto uniforms = allocate_uniform_span(context, sizeof(constants));
    ASSERT_NE(uniforms.data, nullptr);
    std::memcpy(uniforms.data, &constants, sizeof(constants));
    cmd_bind_pipeline(cmd, pipeline);
    cmd_bind_vertex_buffer(cmd, vertices, sizeof(glm::vec2));
    cmd_bind_resources(cmd, pipeline, uniforms);
    cmd_draw(cmd, 3);
    cmd_end_render(cmd);
    const auto path = temp / "shadow_filters.png";
    ASSERT_TRUE(capture_screenshot(context, cmd, path.c_str()));
    nw::Image image{path};
    ASSERT_TRUE(image.valid());
    ASSERT_EQ(image.width(), 64u);
    ASSERT_EQ(image.height(), 64u);
    ASSERT_EQ(image.channels(), 4u);
    size_t mismatched_pixels = 0;
    for (uint32_t y = 0; y < image.height(); ++y) {
        for (uint32_t x = 0; x < image.width(); ++x) {
            const auto* pixel = image.data() + (y * image.width() + x) * 4u;
            const uint8_t expected = (x & 1u) ? 255 : 0;
            mismatched_pixels += pixel[0] != expected || pixel[1] != expected
                || pixel[2] != 0 || pixel[3] != 255;
        }
    }
    EXPECT_EQ(mismatched_pixels, 0u);
}

TEST_F(RenderGfxResources, ModelCullingPreservesMirrorsAndDoubleSidedMaterialsAcrossBatchPaths)
{
    using namespace nw::render;
    namespace viewer = nw::render::viewer;
    nw::ResourceManager resman{nw::kernel::global_allocator()};
    const auto root = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/render";
    ASSERT_TRUE(resman.add_base_container(root, "shaders", nw::ResourceType::hlsl));
    resman.build_registry();
    ShaderProvider shaders{context, &resman};
    ASSERT_TRUE(shaders.initialize());
    RenderService service{nw::kernel::global_allocator()};
    service.configure(context, &shaders);
    ASSERT_TRUE(service.ensure_initialized());
    const auto pipeline = [&](CullMode cull, ModelPipelinePass pass) {
        return service.model_backend().pipeline({.pass = pass, .cull_mode = cull});
    };
    EXPECT_NE(pipeline(CullMode::none, ModelPipelinePass::color), pipeline(CullMode::back, ModelPipelinePass::color));
    EXPECT_NE(pipeline(CullMode::front, ModelPipelinePass::color), pipeline(CullMode::back, ModelPipelinePass::color));
    EXPECT_EQ(pipeline(CullMode::none, ModelPipelinePass::shadow), pipeline(CullMode::back, ModelPipelinePass::shadow));
    EXPECT_FALSE(pipeline(static_cast<CullMode>(255), ModelPipelinePass::color).valid());
    PipelineDesc invalid_desc;
    invalid_desc.cull_mode = static_cast<CullMode>(255);
    EXPECT_FALSE(create_pipeline(context, invalid_desc).valid());
    const auto temp = nw::create_unique_tmp_path();
    const auto cleanup = create_scope_exit([&] { std::filesystem::remove_all(temp); });
    viewer::PreviewScene scene;
    auto model = std::make_shared<RenderModel>(context);
    const std::array<Vertex, 3> vertices{{
        {{-0.18f, -0.4f, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
        {{0.18f, -0.4f, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
        {{0, 0.4f, 0}, {0, 0, 1}, {0.5f, 1}, {1, 0, 0, 1}},
    }};
    const std::array<uint16_t, 3> indices{0, 1, 2};
    Material material;
    material.albedo = {1, 0.2f, 0.1f, 1};
    material.albedo_index = service.model_backend().fallback_albedo_index();
    material.normal_index = service.model_backend().fallback_normal_index();
    material.surface_index = service.model_backend().fallback_surface_index();
    material.emissive_index = service.model_backend().fallback_emissive_index();
    model->materials.push_back(material);
    Primitive primitive;
    primitive.vertices = create_buffer(context, {.size = sizeof(vertices), .usage = BufferUsage::Vertex, .cpu_visible = true});
    primitive.indices = create_buffer(context, {.size = sizeof(indices), .usage = BufferUsage::Index, .cpu_visible = true});
    ASSERT_TRUE(primitive.vertices.valid());
    ASSERT_TRUE(primitive.indices.valid());
    std::memcpy(map_buffer(primitive.vertices), vertices.data(), sizeof(vertices));
    unmap_buffer(primitive.vertices);
    std::memcpy(map_buffer(primitive.indices), indices.data(), sizeof(indices));
    unmap_buffer(primitive.indices);
    primitive.vertex_count = 3;
    primitive.index_count = 3;
    primitive.material = 0;
    primitive.node = 0;
    primitive.bounds = {{-0.18f, -0.4f, 0}, {0.18f, 0.4f, 0}};
    model->primitives.push_back(primitive);
    std::array<PreparedModelSurfaceInstance, 4> rows;
    std::array<glm::mat4, 4> roots;
    for (uint32_t i = 0; i < rows.size(); ++i) {
        auto world = glm::translate(glm::mat4{1}, glm::vec3{-0.75f + 0.5f * static_cast<float>(i), 0, 0.5f});
        if (i >= 2) world = glm::rotate(world, glm::pi<float>(), glm::vec3{0, 1, 0});
        if (i % 2) world = glm::scale(world, glm::vec3{-1, 1, 1});
        rows[i] = {world, glm::transpose(glm::inverse(world))};
        roots[i] = world;
        scene.add(model);
        scene.static_model_instance(i)->root_transform = world;
    }
    RenderContext render{};
    render.view = render.projection = glm::mat4{1};
    render.camera_position = {0, 0, 2};
    render.camera_target = {0, 0, 0};
    render.lighting.ambient = {0.7f, 0.7f, 0.7f};
    render.lighting_space = LightingSpace::world_space;
    viewer::PreviewPreparedModelDraws prepared;
    PreparedModelSurfaceDrawList surfaces;
    for (const bool world_space : {false, true}) {
        for (uint32_t i = 0; i < rows.size(); ++i) {
            auto* instance = scene.static_model_instance(i);
            instance->scene_animation_enabled = world_space;
            instance->attachment_node_world_transforms = {rows[i].root};
            instance->attachment_node_transform_valid = {1};
        }
        for (const bool flip_y : {false, true}) {
            render.projection[1][1] = flip_y ? -1.0f : 1.0f;
            std::array<std::array<std::vector<uint8_t>, 3>, 2> material_pixels;
            for (const uint32_t coverage : {0u, 1u, 2u}) {
                const bool double_sided = coverage == 1u;
                const bool backface_culling = coverage != 2u;
                for (const auto lighting : {MaterialLightingModel::pbr, MaterialLightingModel::nwn_diffuse}) {
                    SCOPED_TRACE(::testing::Message() << world_space << flip_y << coverage << static_cast<int>(lighting));
                    auto override_material = material;
                    override_material.double_sided = double_sided;
                    override_material.backface_culling = backface_culling;
                    override_material.lighting_model = lighting;
                    const auto override_handle = scene.material_overrides.create({override_material});
                    for (uint32_t i = 0; i < rows.size(); ++i) {
                        scene.static_model_instance(i)->material_override_handles = {override_handle};
                    }
                    viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);
                    ASSERT_EQ(surfaces.draws.size(), 4u);
                    std::array<std::vector<uint8_t>, 4> pixels;
                    for (uint32_t path = 0; path < pixels.size(); ++path) {
                        auto* cmd = begin_frame(context);
                        ASSERT_NE(cmd, nullptr);
                        cmd_begin_render(cmd, {}, RenderLoadOp::clear);
                        cmd_set_viewport(cmd, 0, 0, 64, 64, 0, 1);
                        cmd_set_scissor(cmd, 0, 0, 64, 64);
                        PreparedRenderModelSurfaceSubmissionStats submitted;
                        if (path == 0) {
                            render_prepared_render_model_surfaces(service.model_render_context(), cmd, *model,
                                surfaces.draws, render, RenderPassSelection::all, &surfaces.render_model_skins,
                                &scene.material_overrides, &submitted);
                        } else if (path == 1) {
                            submitted = viewer::render_prepared_render_model_surface_draws(
                                service.model_render_context(), cmd, scene, surfaces.draws, render,
                                RenderPassSelection::all, &surfaces.render_model_skins);
                        } else if (path == 2) {
                            render_prepared_render_model_surface_instances(service.model_render_context(), cmd,
                                *model, surfaces.draws.front(), rows, render, RenderPassSelection::all,
                                &scene.material_overrides, &submitted);
                        } else {
                            // Mesh particles use this transient-root submission path.
                            model->materials.front() = override_material;
                            render_render_model_instances(service.model_render_context(), cmd,
                                *model, roots, render, RenderPassSelection::all);
                            model->materials.front() = material;
                        }
                        if (path < 3) {
                            EXPECT_TRUE(submitted.valid());
                            EXPECT_EQ(submitted.submitted_surface_count, 4u);
                        }
                        CommandStats commands;
                        ASSERT_TRUE(get_command_stats(cmd, commands));
                        EXPECT_EQ(commands.dropped_draw_count, 0u);
                        EXPECT_EQ(commands.draw_count, path == 1 ? 2u : path == 2 && (double_sided || !backface_culling) ? 1u
                                                                                                                         : 4u);
                        cmd_end_render(cmd);
                        const auto file = temp / "culling.png";
                        ASSERT_TRUE(capture_screenshot(context, cmd, file.c_str()));
                        nw::Image image{file};
                        ASSERT_TRUE(image.valid());
                        ASSERT_EQ(image.channels(), 4u);
                        pixels[path].assign(image.data(), image.data() + image.width() * image.height() * 4);
                        std::array<size_t, 4> colored{};
                        for (size_t y = 0; y < 64; ++y) {
                            for (size_t x = 0; x < 64; ++x) {
                                const auto offset = (y * 64 + x) * 4;
                                colored[x / 16] += pixels[path][offset] != pixels[path][0]
                                    || pixels[path][offset + 1] != pixels[path][1]
                                    || pixels[path][offset + 2] != pixels[path][2];
                            }
                        }
                        EXPECT_GT(colored[0], 40u);
                        EXPECT_GT(colored[1], 40u);
                        if (double_sided || !backface_culling) {
                            EXPECT_GT(colored[2], 40u);
                            EXPECT_GT(colored[3], 40u);
                        } else {
                            EXPECT_EQ(colored[2], 0u);
                            EXPECT_EQ(colored[3], 0u);
                        }
                    }
                    EXPECT_EQ(pixels[0], pixels[1]);
                    EXPECT_EQ(pixels[0], pixels[2]);
                    EXPECT_EQ(pixels[0], pixels[3]);
                    material_pixels[lighting == MaterialLightingModel::pbr ? 0 : 1][coverage] = pixels[0];
                }
            }
            for (const auto& pixels : material_pixels) {
                // Coverage-only keeps the original front-face lighting. Its
                // visible backs retain authored normals, unlike double-sided lighting.
                for (size_t y = 0; y < 64; ++y) {
                    const size_t offset = y * 64 * 4;
                    EXPECT_EQ(std::memcmp(pixels[0].data() + offset,
                                  pixels[2].data() + offset, 32 * 4),
                        0);
                }
                EXPECT_NE(pixels[1], pixels[2]);
            }
        }
    }

    // Two independently uploaded views in one frame, followed by changes across
    // both frame slots. Compare shared views against standalone batch preparation.
    std::vector<uint8_t> previous_views;
    for (uint32_t frame = 0; frame < 3; ++frame) {
        std::array<RenderContext, 2> views{render, render};
        const float varying_light = 0.2f + 0.15f * static_cast<float>(frame);
        views[0].lighting.ambient = {varying_light, 0.3f, 0.4f};
        views[1].lighting.ambient = {0.6f, 0.4f, varying_light};
        views[1].projection[1][1] = -views[0].projection[1][1];
        views[1].view[3][0] = 0.1f;
        std::vector<uint8_t> reference;
        for (const bool shared : {false, true}) {
            auto* cmd = begin_frame(context);
            ASSERT_NE(cmd, nullptr);
            auto model_ctx = service.model_render_context();
            std::array<UniformSpan, 2> view_uniforms{};
            const std::array<SceneConstants, 2> constants{
                make_scene_constants(views[0]), make_scene_constants(views[1])};
            if (shared) {
                EXPECT_FALSE(upload_render_model_views(model_ctx, constants, {view_uniforms.data(), 1}));
                EXPECT_FALSE(view_uniforms[0].buffer.valid());
                ASSERT_TRUE(upload_render_model_views(model_ctx, constants, view_uniforms));
                ASSERT_TRUE(view_uniforms[0].buffer.valid());
                ASSERT_TRUE(view_uniforms[1].buffer.valid());
                EXPECT_NE(view_uniforms[0].offset, view_uniforms[1].offset);
            }
            cmd_begin_render(cmd, {}, RenderLoadOp::clear);
            for (uint32_t view = 0; view < views.size(); ++view) {
                model_ctx.view_uniforms = view_uniforms[view];
                cmd_set_viewport(cmd, static_cast<float>(view * 32), 0, 32, 64, 0, 1);
                cmd_set_scissor(cmd, view * 32, 0, 32, 64);
                PreparedRenderModelSurfaceSubmissionStats submitted;
                render_prepared_render_model_surfaces(model_ctx, cmd, *model,
                    surfaces.draws, views[view], RenderPassSelection::all,
                    &surfaces.render_model_skins, &scene.material_overrides, &submitted);
                EXPECT_TRUE(submitted.valid());
                EXPECT_EQ(submitted.submitted_surface_count, 4u);
            }
            cmd_end_render(cmd);
            const auto file = temp / "shared-views.png";
            ASSERT_TRUE(capture_screenshot(context, cmd, file.c_str()));
            nw::Image image{file};
            ASSERT_TRUE(image.valid());
            std::vector<uint8_t> pixels(image.data(), image.data() + image.width() * image.height() * 4);
            if (shared) {
                EXPECT_EQ(pixels, reference);
            } else {
                if (frame > 0) {
                    EXPECT_NE(pixels, previous_views);
                }
                previous_views = pixels;
                reference = std::move(pixels);
            }
        }
    }

    // A skin's inverse mesh matrix cancels its mesh bind transform. Exercise
    // reflected binds, reflected bone poses, and the mixed-orientation fallback.
    std::array<SkinnedVertex, 3> skinned_vertices;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = vertices[i];
        skinned_vertices[i] = {v.position, v.normal, v.texcoord, v.tangent, 0u, 255u};
    }
    auto& skin_primitive = model->primitives.front();
    destroy_buffer(skin_primitive.vertices);
    skin_primitive.vertices = create_buffer(context, {.size = sizeof(skinned_vertices), .usage = BufferUsage::Vertex, .cpu_visible = true});
    ASSERT_TRUE(skin_primitive.vertices.valid());
    std::memcpy(map_buffer(skin_primitive.vertices), skinned_vertices.data(), sizeof(skinned_vertices));
    unmap_buffer(skin_primitive.vertices);
    skin_primitive.skinned = true;
    skin_primitive.skin = 0;
    model->nodes.resize(2);
    model->skins.push_back({.joints = {0, 1}, .inverse_bind_matrices = {glm::mat4{1}, glm::mat4{1}}});
    for (uint32_t i = 0; i < rows.size(); ++i) {
        scene.static_model_instance(i)->scene_animation_enabled = false;
        scene.static_model_instance(i)->material_override_handles.clear();
    }
    for (const bool backface_culling : {false, true}) {
        model->materials.front().backface_culling = backface_culling;
        for (const bool mixed_bones : {false, true}) {
            for (const bool mirrored_pose : {false, true}) {
                model->nodes[0].world_transform = glm::scale(glm::mat4{1}, glm::vec3{mirrored_pose ? -1.0f : 1.0f, 1, 1});
                model->nodes[1].world_transform = glm::scale(glm::mat4{1}, glm::vec3{mirrored_pose != mixed_bones ? -1.0f : 1.0f, 1, 1});
                std::vector<uint8_t> reference;
                for (const bool mirrored_bind : {false, true}) {
                    SCOPED_TRACE(::testing::Message() << backface_culling << mixed_bones << mirrored_pose << mirrored_bind);
                    skin_primitive.transform = glm::scale(glm::mat4{1}, glm::vec3{mirrored_bind ? -1.0f : 1.0f, 1, 1});
                    skin_primitive.inverse_mesh_transform = glm::inverse(skin_primitive.transform);
                    viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);
                    auto* cmd = begin_frame(context);
                    ASSERT_NE(cmd, nullptr);
                    cmd_begin_render(cmd, {}, RenderLoadOp::clear);
                    cmd_set_viewport(cmd, 0, 0, 64, 64, 0, 1);
                    cmd_set_scissor(cmd, 0, 0, 64, 64);
                    PreparedRenderModelSurfaceSubmissionStats submitted;
                    render_prepared_render_model_surfaces(service.model_render_context(), cmd, *model,
                        surfaces.draws, render, RenderPassSelection::all, &surfaces.render_model_skins,
                        &scene.material_overrides, &submitted);
                    EXPECT_TRUE(submitted.valid());
                    EXPECT_EQ(submitted.submitted_surface_count, 4u);
                    cmd_end_render(cmd);
                    const auto file = temp / "skin-culling.png";
                    ASSERT_TRUE(capture_screenshot(context, cmd, file.c_str()));
                    nw::Image image{file};
                    ASSERT_TRUE(image.valid());
                    ASSERT_EQ(image.channels(), 4u);
                    std::vector<uint8_t> pixels(image.data(), image.data() + image.width() * image.height() * 4);
                    std::array<size_t, 4> colored{};
                    for (size_t y = 0; y < 64; ++y) {
                        for (size_t x = 0; x < 64; ++x) {
                            const auto offset = (y * 64 + x) * 4;
                            colored[x / 16] += pixels[offset] != pixels[0]
                                || pixels[offset + 1] != pixels[1] || pixels[offset + 2] != pixels[2];
                        }
                    }
                    EXPECT_GT(colored[0], 40u);
                    EXPECT_GT(colored[1], 40u);
                    if (!backface_culling || mixed_bones) {
                        EXPECT_GT(colored[2], 40u);
                        EXPECT_GT(colored[3], 40u);
                    } else {
                        EXPECT_EQ(colored[2], 0u);
                        EXPECT_EQ(colored[3], 0u);
                    }
                    if (!mirrored_bind)
                        reference = pixels;
                    else
                        EXPECT_EQ(reference, pixels);
                }
            }
        }
    }
}

TEST_F(RenderGfxResources, IndexedGroupsPreserveInterleavedAssetsMaterialsAndSpaces)
{
    using namespace nw::render;
    namespace viewer = nw::render::viewer;
    nw::ResourceManager resman{nw::kernel::global_allocator()};
    const auto root = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/render";
    ASSERT_TRUE(resman.add_base_container(root, "shaders", nw::ResourceType::hlsl));
    resman.build_registry();
    ShaderProvider shaders{context, &resman};
    ASSERT_TRUE(shaders.initialize());
    RenderService service{nw::kernel::global_allocator()};
    service.configure(context, &shaders);
    ASSERT_TRUE(service.ensure_initialized());
    const auto temp = nw::create_unique_tmp_path();
    const auto cleanup = create_scope_exit([&] { std::filesystem::remove_all(temp); });
    viewer::PreviewScene scene;
    const std::array<Vertex, 3> vertices{{
        {{-0.12f, -0.2f, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
        {{0.12f, -0.2f, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
        {{0, 0.2f, 0}, {0, 0, 1}, {0.5f, 1}, {1, 0, 0, 1}},
    }};
    const std::array<uint16_t, 3> indices{0, 1, 2};
    std::array<std::shared_ptr<RenderModel>, 2> models;
    for (uint32_t i = 0; i < models.size(); ++i) {
        models[i] = std::make_shared<RenderModel>(context);
        Material material;
        material.albedo = i == 0 ? glm::vec4{1, 0.2f, 0.1f, 1} : glm::vec4{0.1f, 0.2f, 1, 1};
        material.albedo_index = service.model_backend().fallback_albedo_index();
        material.normal_index = service.model_backend().fallback_normal_index();
        material.surface_index = service.model_backend().fallback_surface_index();
        material.emissive_index = service.model_backend().fallback_emissive_index();
        models[i]->materials.push_back(material);
        Primitive primitive;
        primitive.vertices = create_buffer(context, {.size = sizeof(vertices), .usage = BufferUsage::Vertex, .cpu_visible = true});
        primitive.indices = create_buffer(context, {.size = sizeof(indices), .usage = BufferUsage::Index, .cpu_visible = true});
        ASSERT_TRUE(primitive.vertices.valid());
        ASSERT_TRUE(primitive.indices.valid());
        std::memcpy(map_buffer(primitive.vertices), vertices.data(), sizeof(vertices));
        unmap_buffer(primitive.vertices);
        std::memcpy(map_buffer(primitive.indices), indices.data(), sizeof(indices));
        unmap_buffer(primitive.indices);
        primitive.vertex_count = primitive.index_count = 3;
        primitive.material = 0;
        primitive.node = 0;
        primitive.bounds = {{-0.12f, -0.2f, 0}, {0.12f, 0.2f, 0}};
        models[i]->primitives.push_back(primitive);
    }
    auto material = models[0]->materials[0];
    material.albedo = {0.1f, 1, 0.2f, 1};
    const auto override_handle = scene.material_overrides.create({material});
    // Five groups, interleaved and repeated: shared asset/root, other asset,
    // shared asset/world, material override, and reflected winding.
    for (uint32_t i = 0; i < 12; ++i) {
        const auto column = i % 6;
        scene.add(models[column == 1 ? 1 : 0]);
        auto* instance = scene.static_model_instance(i);
        ASSERT_NE(instance, nullptr);
        auto world = glm::translate(glm::mat4{1}, glm::vec3{-0.8f + static_cast<float>(column) * 0.32f, i < 6 ? -0.45f : 0.45f, 0.5f});
        if (column == 4) world = glm::scale(world, glm::vec3{-1, 1, 1});
        instance->root_transform = world;
        if (column == 2) {
            instance->scene_animation_enabled = true;
            instance->attachment_node_world_transforms = {world};
            instance->attachment_node_transform_valid = {1};
            instance->root_transform = glm::translate(glm::mat4{1}, glm::vec3{100, 100, 0});
        }
        if (column == 3) instance->material_override_handles = {override_handle};
    }
    RenderContext render{};
    render.view = render.projection = glm::mat4{1};
    render.camera_position = {0, 0, 2};
    render.camera_target = {0, 0, 0};
    render.lighting.ambient = {0.7f, 0.7f, 0.7f};
    render.lighting_space = LightingSpace::world_space;
    viewer::PreviewPreparedModelDraws prepared;
    PreparedModelSurfaceDrawList surfaces;
    viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);
    ASSERT_EQ(surfaces.draws.size(), 12u);
    std::vector<uint32_t> selected_indices;
    for (uint32_t source = 0; source < 12; ++source) {
        const auto surface = std::find_if(surfaces.draws.begin(), surfaces.draws.end(),
            [&](const auto& row) { return row.instance_source_index == source; });
        ASSERT_NE(surface, surfaces.draws.end());
        selected_indices.push_back(static_cast<uint32_t>(surface - surfaces.draws.begin()));
    }
    surfaces.draws.push_back(surfaces.draws.front());
    surfaces.draws.back().material_mode = MaterialMode::transparent;
    surfaces.draws.push_back(surfaces.draws.front());
    surfaces.draws.back().instance_source_index = kInvalidPreparedModelDrawIndex;
    selected_indices.insert(selected_indices.begin() + 3, {12u, UINT32_MAX, 13u});
    std::array<std::vector<uint8_t>, 2> pixels;
    for (uint32_t batched = 0; batched < 2; ++batched) {
        auto* cmd = begin_frame(context);
        ASSERT_NE(cmd, nullptr);
        cmd_begin_render(cmd, {}, RenderLoadOp::clear);
        cmd_set_viewport(cmd, 0, 0, 64, 64, 0, 1);
        cmd_set_scissor(cmd, 0, 0, 64, 64);
        PreparedRenderModelSurfaceSubmissionStats submitted;
        if (batched) {
            submitted = viewer::render_prepared_render_model_surface_draw_indices(
                service.model_render_context(), cmd, scene, surfaces.draws, selected_indices,
                render, RenderPassSelection::opaque_cutout, nullptr, &surfaces.render_model_skins);
        } else {
            for (size_t i = 0; i < 12; ++i) {
                const auto& surface = surfaces.draws[i];
                render_prepared_render_model_surfaces(service.model_render_context(), cmd,
                    *scene.static_models[surface.instance_source_index], std::span{&surface, 1},
                    render, RenderPassSelection::opaque_cutout, &surfaces.render_model_skins,
                    &scene.material_overrides, &submitted);
            }
        }
        EXPECT_EQ(submitted.dropped_invalid_surface_count, batched ? 2u : 0u);
        EXPECT_EQ(submitted.submitted_surface_count, 12u);
        CommandStats commands;
        ASSERT_TRUE(get_command_stats(cmd, commands));
        EXPECT_EQ(commands.dropped_draw_count, 0u);
        EXPECT_EQ(commands.draw_count, batched ? 5u : 12u);
        cmd_end_render(cmd);
        const auto file = temp / "indexed-groups.png";
        ASSERT_TRUE(capture_screenshot(context, cmd, file.c_str()));
        nw::Image image{file};
        ASSERT_TRUE(image.valid());
        ASSERT_EQ(image.channels(), 4u);
        pixels[batched].assign(image.data(), image.data() + image.width() * image.height() * 4);
    }
    EXPECT_EQ(pixels[0], pixels[1]);
    size_t colored = 0;
    for (size_t i = 0; i < pixels[0].size(); i += 4) {
        colored += pixels[0][i] != pixels[0][i + 1] || pixels[0][i + 1] != pixels[0][i + 2];
    }
    EXPECT_GT(colored, 100u);

    // Exercise the area-owned result against fresh preparation, including
    // visibility changes that must never reuse the complete-view membership.
    scene.static_area_model_info.resize(scene.static_models.size());
    for (auto& info : scene.static_area_model_info) {
        info.static_candidate = true;
    }
    scene.area_render_scene = std::make_unique<viewer::AreaRenderScene>();
    auto& area = *scene.area_render_scene;
    area.rebuild(scene);
    viewer::AreaRenderFrame frame;
    auto& cache = area.surface_batch_cache();
    const auto first_handle = scene.static_model_instance_handles[0];
    const std::array<uint64_t, 16> expected_builds{1, 1, 1, 1, 1, 1, 2, 3, 3, 3, 4, 4, 5, 6, 6, 7};
    for (uint32_t stage = 0; stage < expected_builds.size(); ++stage) {
        SCOPED_TRACE(stage);
        auto* first = scene.model_instances.get(first_handle);
        ASSERT_NE(first, nullptr);
        if (stage == 2) first->visible = false;
        if (stage == 3) first->visible = true;
        if (stage == 4) {
            const std::array suppressed{0u};
            ASSERT_TRUE(area.set_tile_preview_suppressed_models(suppressed));
        }
        if (stage == 5) {
            ASSERT_TRUE(area.set_tile_preview_suppressed_models({}));
        }
        if (stage == 6) {
            scene.static_model_instance(1)->material_override_handles = {override_handle};
            area.rebuild(scene);
        }
        if (stage == 7) {
            first->root_transform = glm::scale(first->root_transform, glm::vec3{-1, 1, 1});
            area.rebuild(scene);
        }
        if (stage == 8) first->render_model_index = UINT32_MAX;
        if (stage == 9) first->render_model_index = 0u;
        if (stage == 10) {
            first->root_transform = glm::translate(glm::mat4{1}, glm::vec3{-0.7f, -0.4f, 0.5f});
            first->root_transform = glm::rotate(first->root_transform, 0.3f, glm::vec3{0, 0, 1});
            first->root_transform = glm::scale(first->root_transform, glm::vec3{1.3f, 0.7f, 1.1f});
            first->root_transform[1][0] += 0.15f;
            const std::array changed{0u};
            viewer::sync_model_instance_runtime_state(scene, changed);
            EXPECT_FALSE(cache.valid);
        }
        if (stage == 12) {
            scene.static_models[1] = models[0];
            area.rebuild(scene);
        }
        if (stage == 13) {
            area.clear();
            EXPECT_TRUE(cache.batches.groups.empty());
            area.rebuild(scene);
        }
        if (stage == 14) {
            const auto group = std::find_if(cache.batches.groups.begin(), cache.batches.groups.end(),
                [](const auto& row) { return !row.root_instances.empty(); });
            ASSERT_NE(group, cache.batches.groups.end());
            // An incomplete retained payload must use current live rows.
            group->root_instances.pop_back();
        }
        if (stage == 15) {
            viewer::sync_model_instance_runtime_state(scene);
            EXPECT_FALSE(cache.valid);
        }
        area.refresh_runtime_records(scene);
        viewer::prepare_area_frame(area, frame);
        const auto& cached_surfaces = area.prepared_model_surface_draws();
        const bool partial = stage == 2 || stage == 4 || stage == 8;
        EXPECT_EQ(frame.stats().visible_static_record_count, partial ? 9u : 10u);
        EXPECT_EQ(frame.uses_cached_draw_lists() && !frame.filters_cached_draw_records(), !partial);
        uint64_t fresh_draw_count = 0u;
        for (uint32_t retained = 0; retained < 2; ++retained) {
            auto* cmd = begin_frame(context);
            ASSERT_NE(cmd, nullptr);
            cmd_begin_render(cmd, {}, RenderLoadOp::clear);
            cmd_set_viewport(cmd, 0, 0, 64, 64, 0, 1);
            cmd_set_scissor(cmd, 0, 0, 64, 64);
            PreparedRenderModelSurfaceSubmissionStats submitted;
            if (retained) {
                submitted = viewer::render_area_model_surface_draws(
                    service.model_render_context(), cmd, scene, area, frame,
                    render, RenderPassSelection::opaque_cutout);
            } else {
                submitted = viewer::render_prepared_render_model_surface_draw_indices(
                    service.model_render_context(), cmd, scene, cached_surfaces.draws,
                    frame.visible_opaque_cutout_prepared_surface_indices(), render,
                    RenderPassSelection::opaque_cutout,
                    frame.filters_cached_draw_records() ? &frame : nullptr,
                    &cached_surfaces.render_model_skins);
            }
            EXPECT_EQ(submitted.dropped_invalid_surface_count, 0u);
            EXPECT_EQ(submitted.submitted_surface_count, partial ? 9u : 10u);
            CommandStats commands;
            ASSERT_TRUE(get_command_stats(cmd, commands));
            EXPECT_EQ(commands.dropped_draw_count, 0u);
            if (retained)
                EXPECT_EQ(commands.draw_count, fresh_draw_count);
            else
                fresh_draw_count = commands.draw_count;
            cmd_end_render(cmd);
            const auto file = temp / "retained-groups.png";
            ASSERT_TRUE(capture_screenshot(context, cmd, file.c_str()));
            nw::Image image{file};
            ASSERT_TRUE(image.valid());
            pixels[retained].assign(image.data(), image.data() + image.width() * image.height() * 4);
        }
        EXPECT_EQ(pixels[0], pixels[1]);
        EXPECT_TRUE(cache.valid);
        EXPECT_EQ(cache.rebuild_count, expected_builds[stage]);
        if (stage != 14) {
            size_t retained_rows = 0;
            for (const auto& group : cache.batches.groups) {
                if (group.space != PreparedModelSurfaceInstanceSpace::model_root
                    || group.run_indices.size() < 2u) {
                    EXPECT_TRUE(group.root_instances.empty());
                    continue;
                }
                ASSERT_EQ(group.root_instances.size(), group.run_indices.size());
                retained_rows += group.root_instances.size();
                for (size_t i = 0; i < group.run_indices.size(); ++i) {
                    const auto source = cache.batches.runs[group.run_indices[i]].instance_source_index;
                    const auto* instance = scene.model_instances.get(scene.static_model_instance_handles[source]);
                    ASSERT_NE(instance, nullptr);
                    const auto normal = glm::mat4{glm::mat3{glm::transpose(glm::inverse(instance->root_transform))}};
                    EXPECT_EQ(std::memcmp(&group.root_instances[i].root, &instance->root_transform, sizeof(glm::mat4)), 0);
                    EXPECT_EQ(std::memcmp(&group.root_instances[i].root_normal_matrix, &normal, sizeof(glm::mat4)), 0);
                }
            }
            EXPECT_GT(retained_rows, 0u);
        }
    }
    area.clear();
    EXPECT_FALSE(cache.valid);
    EXPECT_TRUE(cache.batches.surface_indices.empty());
    EXPECT_TRUE(cache.batches.groups.empty());
}

TEST_F(RenderGfxResources, AnimatedRigidGroupsMatchScalarPixelsAcrossPosesAndEdits)
{
    using namespace nw::render;
    namespace viewer = nw::render::viewer;
    nw::ResourceManager resman{nw::kernel::global_allocator()};
    const auto root = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/render";
    ASSERT_TRUE(resman.add_base_container(root, "shaders", nw::ResourceType::hlsl));
    resman.build_registry();
    ShaderProvider shaders{context, &resman};
    ASSERT_TRUE(shaders.initialize());
    RenderService service{nw::kernel::global_allocator()};
    service.configure(context, &shaders);
    ASSERT_TRUE(service.ensure_initialized());
    const auto temp = nw::create_unique_tmp_path();
    const auto cleanup = create_scope_exit([&] { std::filesystem::remove_all(temp); });
    viewer::PreviewScene scene;
    auto model = std::make_shared<RenderModel>(context);
    const std::array<Vertex, 3> vertices{{
        {{-0.15f, -0.13f, 0}, {0.2f, 0.3f, 1}, {0, 0}, {1, 0, 0, 1}},
        {{0.15f, -0.13f, 0}, {0.2f, 0.3f, 1}, {1, 0}, {1, 0, 0, 1}},
        {{0, 0.17f, 0}, {0.2f, 0.3f, 1}, {0.5f, 1}, {1, 0, 0, 1}},
    }};
    const std::array<uint16_t, 3> indices{0, 1, 2};
    for (uint32_t i = 0; i < 2; ++i) {
        Material material;
        material.lighting_model = i == 0 ? MaterialLightingModel::nwn_diffuse : MaterialLightingModel::pbr;
        material.albedo = i == 0 ? glm::vec4{1, 0.2f, 0.1f, 1} : glm::vec4{0.1f, 0.8f, 1, 1};
        material.albedo_index = service.model_backend().fallback_albedo_index();
        material.normal_index = service.model_backend().fallback_normal_index();
        material.surface_index = service.model_backend().fallback_surface_index();
        material.emissive_index = service.model_backend().fallback_emissive_index();
        material.double_sided = true;
        model->materials.push_back(material);
        Primitive primitive;
        primitive.vertices = create_buffer(context, {.size = sizeof(vertices), .usage = BufferUsage::Vertex, .cpu_visible = true});
        primitive.indices = create_buffer(context, {.size = sizeof(indices), .usage = BufferUsage::Index, .cpu_visible = true});
        ASSERT_TRUE(primitive.vertices.valid());
        ASSERT_TRUE(primitive.indices.valid());
        std::memcpy(map_buffer(primitive.vertices), vertices.data(), sizeof(vertices));
        unmap_buffer(primitive.vertices);
        std::memcpy(map_buffer(primitive.indices), indices.data(), sizeof(indices));
        unmap_buffer(primitive.indices);
        primitive.vertex_count = 3;
        primitive.index_count = 3;
        primitive.material = i;
        primitive.node = static_cast<int32_t>(i);
        // Applying this bind transform to a prepared world pose would lose the mesh.
        primitive.transform = glm::translate(glm::mat4{1}, glm::vec3{100, 100, 0});
        primitive.bounds = {{-0.15f, -0.13f, 0}, {0.15f, 0.17f, 0}};
        model->primitives.push_back(primitive);
    }
    for (uint32_t i = 0; i < 3; ++i) {
        scene.add(model);
        auto* instance = scene.static_model_instance(i);
        ASSERT_NE(instance, nullptr);
        instance->scene_animation_enabled = true;
        instance->root_transform = glm::translate(glm::mat4{1}, glm::vec3{20, 20, 0});
        instance->attachment_node_world_transforms.resize(2);
        instance->attachment_node_transform_valid.assign(2, 1u);
    }
    RenderContext render{};
    render.view = render.projection = glm::mat4{1};
    render.camera_position = {0, 0, 2};
    render.camera_target = {0, 0, 0};
    render.lighting.ambient = {0.7f, 0.7f, 0.7f};
    render.lighting_space = LightingSpace::world_space;
    viewer::PreviewPreparedModelDraws prepared;
    PreparedModelSurfaceDrawList surfaces;
    std::vector<uint8_t> first_pixels;
    const std::array<uint64_t, 7> expected_draws{2, 2, 4, 2, 4, 3, 2};
    for (uint32_t stage = 0; stage < expected_draws.size(); ++stage) {
        SCOPED_TRACE(stage);
        render.view = glm::translate(glm::mat4{1}, glm::vec3{stage == 0 ? 0.0f : 0.05f, 0, 0});
        for (uint32_t i = 0; i < 3; ++i) {
            auto* instance = scene.static_model_instance(i);
            const auto column = static_cast<float>(i);
            for (uint32_t primitive = 0; primitive < 2; ++primitive) {
                const auto row = static_cast<float>(primitive);
                auto transform = glm::translate(glm::mat4{1}, glm::vec3{-0.55f + column * 0.55f, -0.35f + row * 0.7f, 0.3f});
                transform = glm::rotate(transform, stage == 0 ? 0.0f : 0.35f + column * 0.1f, glm::vec3{0, 0, 1});
                instance->attachment_node_world_transforms[primitive]
                    = glm::scale(transform, glm::vec3{0.7f + column * 0.1f, 1.0f + row * 0.15f, 1});
            }
        }
        if (stage == 2) {
            auto material = model->materials[0];
            material.albedo = {0.2f, 1, 0.1f, 1};
            scene.static_model_instance(2)->material_override_handles = {
                scene.material_overrides.create({material})};
        }
        if (stage == 3) { scene.static_model_instance(2)->visible = false; }
        if (stage == 4) { model->primitives[1].deformer = 0; }
        if (stage == 5) {
            model->primitives[1].deformer = kInvalidModelDeformerIndex;
            model->materials[1].alpha_mode = MaterialMode::transparent;
            model->materials[1].albedo.a = 0.6f;
        }
        if (stage == 6) {
            ASSERT_TRUE(service.reload_shaders());
            for (auto& material : model->materials) {
                material.albedo_index = service.model_backend().fallback_albedo_index();
                material.normal_index = service.model_backend().fallback_normal_index();
                material.surface_index = service.model_backend().fallback_surface_index();
                material.emissive_index = service.model_backend().fallback_emissive_index();
            }
            model->materials[1].alpha_mode = MaterialMode::opaque;
            model->materials[1].albedo.a = 1.0f;
            scene.static_model_instance(2)->visible = true;
            scene.static_model_instance(2)->material_override_handles.clear();
        }
        viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);
        ASSERT_EQ(surfaces.draws.size(), stage < 3 || stage == 6 ? 6u : 4u);
        std::array<std::vector<uint8_t>, 2> pixels;
        for (uint32_t batched = 0; batched < 2; ++batched) {
            auto* cmd = begin_frame(context);
            ASSERT_NE(cmd, nullptr);
            cmd_begin_render(cmd, {}, RenderLoadOp::clear);
            cmd_set_viewport(cmd, 0, 0, 64, 64, 0, 1);
            cmd_set_scissor(cmd, 0, 0, 64, 64);
            PreparedRenderModelSurfaceSubmissionStats submitted;
            if (batched) {
                submitted = viewer::render_prepared_render_model_surface_draws(
                    service.model_render_context(), cmd, scene, surfaces.draws, render,
                    RenderPassSelection::all, &surfaces.render_model_skins);
            } else {
                for (const auto& surface : surfaces.draws) {
                    render_prepared_render_model_surfaces(service.model_render_context(), cmd,
                        *model, std::span{&surface, 1}, render, RenderPassSelection::all,
                        &surfaces.render_model_skins, &scene.material_overrides, &submitted);
                }
            }
            EXPECT_TRUE(submitted.valid());
            EXPECT_EQ(submitted.submitted_surface_count, surfaces.draws.size());
            CommandStats commands;
            ASSERT_TRUE(get_command_stats(cmd, commands));
            EXPECT_EQ(commands.draw_count, batched ? expected_draws[stage] : surfaces.draws.size());
            EXPECT_EQ(commands.dropped_draw_count, 0u);
            cmd_end_render(cmd);
            const auto path = temp / (batched ? "batched.png" : "scalar.png");
            ASSERT_TRUE(capture_screenshot(context, cmd, path.c_str()));
            nw::Image image{path};
            ASSERT_TRUE(image.valid());
            ASSERT_EQ(image.channels(), 4u);
            pixels[batched].assign(image.data(), image.data() + image.width() * image.height() * image.channels());
        }
        EXPECT_EQ(pixels[0], pixels[1]);
        if (stage == 0) {
            first_pixels = pixels[0];
            size_t colored = 0;
            for (size_t i = 0; i < first_pixels.size(); i += 4) {
                colored += first_pixels[i] != first_pixels[i + 1] || first_pixels[i + 1] != first_pixels[i + 2];
            }
            EXPECT_GT(colored, 50u);
        } else if (stage == 1) {
            EXPECT_NE(first_pixels, pixels[0]);
        }
    }
}

TEST_F(RenderGfxResources, AnimatedRigidShadowBatchesMatchScalarDepthAcrossPassesAndEdits)
{
    using namespace nw::render;
    namespace viewer = nw::render::viewer;
    nw::ResourceManager resman{nw::kernel::global_allocator()};
    const auto root = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/render";
    ASSERT_TRUE(resman.add_base_container(root, "shaders", nw::ResourceType::hlsl));
    resman.build_registry();
    ShaderProvider shaders{context, &resman};
    ASSERT_TRUE(shaders.initialize());
    RenderService service{nw::kernel::global_allocator()};
    service.configure(context, &shaders);
    ASSERT_TRUE(service.ensure_initialized());
    viewer::PreviewScene scene;
    auto model = std::make_shared<RenderModel>(context);
    const std::array<Vertex, 3> vertices{{
        {{-0.12f, -0.12f, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
        {{0.12f, -0.12f, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
        {{0, 0.12f, 0}, {0, 0, 1}, {0.5f, 1}, {1, 0, 0, 1}},
    }};
    const std::array<uint16_t, 3> indices{0, 1, 2};
    for (uint32_t i = 0; i < 3; ++i) {
        Material material;
        material.alpha_mode = i == 0 ? MaterialMode::opaque : i == 1 ? MaterialMode::cutout
                                                                     : MaterialMode::transparent;
        material.albedo_index = service.model_backend().fallback_albedo_index();
        material.double_sided = true;
        model->materials.push_back(material);
        Primitive primitive;
        primitive.vertices = create_buffer(context, {.size = sizeof(vertices), .usage = BufferUsage::Vertex, .cpu_visible = true});
        primitive.indices = create_buffer(context, {.size = sizeof(indices), .usage = BufferUsage::Index, .cpu_visible = true});
        ASSERT_TRUE(primitive.vertices.valid());
        ASSERT_TRUE(primitive.indices.valid());
        std::memcpy(map_buffer(primitive.vertices), vertices.data(), sizeof(vertices));
        unmap_buffer(primitive.vertices);
        std::memcpy(map_buffer(primitive.indices), indices.data(), sizeof(indices));
        unmap_buffer(primitive.indices);
        primitive.vertex_count = primitive.index_count = 3;
        primitive.material = i;
        primitive.node = static_cast<int32_t>(i);
        primitive.casts_shadow = true;
        primitive.transform = glm::translate(glm::mat4{1}, glm::vec3{100, 100, 0});
        primitive.bounds = {{-0.12f, -0.12f, 0}, {0.12f, 0.12f, 0}};
        model->primitives.push_back(primitive);
    }
    model->shadow = summarize_render_model_shadows(*model);
    for (uint32_t i = 0; i < 3; ++i) {
        scene.add(model);
        auto* instance = scene.static_model_instance(i);
        ASSERT_NE(instance, nullptr);
        instance->scene_animation_enabled = true;
        instance->root_transform = glm::translate(glm::mat4{1}, glm::vec3{20, 20, 0});
        instance->attachment_node_world_transforms.resize(3);
        instance->attachment_node_transform_valid.assign(3, 1u);
        const float x = -0.55f + static_cast<float>(i) * 0.55f;
        instance->shadow.bounds = {{x - 0.18f, -0.7f, 0.2f}, {x + 0.18f, 0.7f, 0.7f}};
    }
    const std::array<glm::mat4, 2> matrices{
        glm::mat4{1},
        glm::scale(glm::translate(glm::mat4{1}, glm::vec3{1.5f, 0, 0}), glm::vec3{2, 1, 1}),
    };
    viewer::PreviewPreparedModelDraws prepared;
    PreparedModelSurfaceDrawList surfaces;
    const std::array<uint64_t, 7> expected_draws{4, 4, 6, 4, 6, 2, 4};
    std::vector<uint8_t> initial_depth;
    for (uint32_t stage = 0; stage < expected_draws.size(); ++stage) {
        SCOPED_TRACE(stage);
        for (uint32_t i = 0; i < 3; ++i) {
            auto* instance = scene.static_model_instance(i);
            for (uint32_t primitive = 0; primitive < 3; ++primitive) {
                auto transform = glm::translate(glm::mat4{1}, glm::vec3{-0.55f + static_cast<float>(i) * 0.55f, -0.35f + static_cast<float>(primitive) * 0.35f, stage == 0 ? 0.3f : 0.45f});
                transform = glm::rotate(transform, stage == 0 ? 0.0f : 0.35f, glm::vec3{0, 0, 1});
                instance->attachment_node_world_transforms[primitive] = glm::scale(transform, glm::vec3{0.8f, 1.1f, 1});
            }
        }
        if (stage == 2) {
            scene.static_model_instance(2)->material_override_handles = {
                scene.material_overrides.create({model->materials[0]})};
        }
        if (stage == 3) scene.static_model_instance(2)->visible = false;
        if (stage == 4) model->primitives[1].deformer = 0;
        if (stage == 5) {
            model->primitives[1].deformer = kInvalidModelDeformerIndex;
            model->materials[1].alpha_mode = MaterialMode::transparent;
        }
        if (stage == 6) {
            ASSERT_TRUE(service.reload_shaders());
            for (auto& material : model->materials)
                material.albedo_index = service.model_backend().fallback_albedo_index();
            model->materials[1].alpha_mode = MaterialMode::cutout;
            scene.static_model_instance(2)->visible = true;
            scene.static_model_instance(2)->material_override_handles.clear();
        }
        viewer::collect_prepared_model_surface_draws(prepared, surfaces, scene);
        if (stage == 0) {
            auto* cmd = begin_frame(context);
            ASSERT_NE(cmd, nullptr);
            const auto mapped = service.model_backend().allocate_mapped_frame_storage(cmd, sizeof(PreparedModelSurfaceInstance), 64);
            ASSERT_TRUE(mapped.span.buffer.valid());
            struct InvalidSpan {
                uint32_t bytes, first, count;
                PreparedModelSurfaceInstanceSpace space;
            };
            const auto bytes = static_cast<uint32_t>(sizeof(PreparedModelSurfaceInstance));
            const std::array invalid{
                InvalidSpan{bytes, 0, 2, PreparedModelSurfaceInstanceSpace::primitive_world},
                InvalidSpan{bytes, 1, 1, PreparedModelSurfaceInstanceSpace::primitive_world},
                InvalidSpan{bytes - 1, 0, 1, PreparedModelSurfaceInstanceSpace::primitive_world},
                InvalidSpan{bytes, 0, 1, static_cast<PreparedModelSurfaceInstanceSpace>(255)},
            };
            for (const auto& span : invalid) {
                EXPECT_FALSE(render_prepared_render_model_shadow_surface_instances(service.model_render_context(), cmd,
                    *model, surfaces.draws.front(), {mapped.span.buffer, mapped.span.offset, span.bytes}, span.first, span.count,
                    glm::mat4{1}, glm::mat4{1}, &scene.material_overrides, span.space));
            }
            CommandStats commands;
            ASSERT_TRUE(get_command_stats(cmd, commands));
            EXPECT_EQ(commands.draw_count, 0u);
            end_frame(context);
        }
        // Opaque, cutout and transparent passes interleave each candidate's rows.
        ASSERT_EQ(surfaces.draws.size(), stage < 3 || stage == 6 ? 9u : 6u);
        for (bool local : {false, true}) {
            SCOPED_TRACE(local);
            SceneShadow shadow{};
            shadow.enabled = true;
            shadow.cascade_count = 2;
            SceneLocalShadows local_shadows{};
            local_shadows.count = 2;
            for (uint32_t slot = 0; slot < 2; ++slot) {
                shadow.world_to_shadow[slot] = matrices[slot];
                local_shadows.world_to_shadow[slot] = matrices[slot];
            }
            auto* cmd = begin_frame(context);
            ASSERT_NE(cmd, nullptr);
            if (local) {
                viewer::LocalShadowRenderStats stats;
                ASSERT_TRUE(viewer::render_local_shadow_maps(service, service.model_render_context(), cmd,
                    scene, local_shadows, 256, &stats, nullptr, &prepared, &surfaces));
                EXPECT_EQ(stats.submitted_model_count, stage < 3 || stage == 6 ? 4u : 3u);
            } else {
                viewer::ShadowRenderStats stats;
                ASSERT_TRUE(viewer::render_scene_shadow_maps(service, service.model_render_context(), cmd,
                    scene, shadow, 256, &stats, nullptr, &prepared, &surfaces));
                EXPECT_EQ(stats.submitted_model_count, stage < 3 || stage == 6 ? 4u : 3u);
            }
            CommandStats commands;
            ASSERT_TRUE(get_command_stats(cmd, commands));
            EXPECT_EQ(commands.draw_count, expected_draws[stage]);
            EXPECT_EQ(commands.dropped_draw_count, 0u);
            end_frame(context);
            for (uint32_t slot = 0; slot < 2; ++slot) {
                SCOPED_TRACE(slot);
                const auto target = local ? service.local_shadow_renderer().render_target(slot) : service.shadow_renderer().render_target(slot);
                const auto texture = local ? local_shadows.depth_textures[slot] : shadow.depth_textures[slot];
                const auto batched = read_mips(texture, sizeof(float), VK_IMAGE_ASPECT_DEPTH_BIT);
                cmd = begin_frame(context);
                ASSERT_NE(cmd, nullptr);
                cmd_begin_render(cmd, target);
                cmd_set_viewport(cmd, 0, 0, 256, 256, 0, 1);
                cmd_set_scissor(cmd, 0, 0, 256, 256);
                for (const auto& surface : surfaces.draws) {
                    if (slot == 1 && surface.instance_source_index != 0) continue;
                    render_prepared_render_model_shadow_surfaces(service.model_render_context(), cmd, *model,
                        std::span{&surface, 1}, surface.range_index, glm::mat4{1}, matrices[slot],
                        &surfaces.render_model_skins, &scene.material_overrides);
                }
                cmd_end_render(cmd);
                end_frame(context);
                const auto scalar = read_mips(texture, sizeof(float), VK_IMAGE_ASPECT_DEPTH_BIT);
                EXPECT_EQ(batched, scalar);
                if (!local && slot == 0 && stage == 0) {
                    initial_depth = batched;
                    size_t written = 0;
                    for (size_t offset = 0; offset < batched.size(); offset += sizeof(float)) {
                        float depth;
                        std::memcpy(&depth, batched.data() + offset, sizeof(float));
                        written += depth < 1.0f;
                    }
                    EXPECT_GT(written, 100u);
                } else if (!local && slot == 0 && stage == 1) {
                    EXPECT_NE(batched, initial_depth);
                }
            }
        }
    }
}

TEST_F(RenderGfxResources, RequiredModelShadersFailInitializationAndReloadThenRecover)
{
    using namespace nw::render;
    const auto original = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/render/shaders";
    const auto temp = nw::create_unique_tmp_path();
    const auto cleanup = create_scope_exit([&] { std::filesystem::remove_all(temp); });
    const auto shader_dir = temp / "shaders";
    std::filesystem::copy(original, shader_dir, std::filesystem::copy_options::recursive);
    const std::array names{"render_pbr_static.vs.hlsl", "render_pbr_static.ps.hlsl",
        "render_pbr_static_instanced.vs.hlsl", "render_pbr_skinned.vs.hlsl",
        "render_nwn_static.ps.hlsl", "render_water.vs.hlsl", "render_water.ps.hlsl",
        "render_pbr_shadow.ps.hlsl"};
    for (const auto* name : names) {
        for (bool missing : {false, true}) {
            SCOPED_TRACE(name);
            SCOPED_TRACE(missing ? "missing" : "malformed");
            nw::ResourceManager resman{nw::kernel::global_allocator()};
            ASSERT_TRUE(resman.add_base_container(temp, "shaders", nw::ResourceType::hlsl));
            resman.build_registry();
            ShaderProvider shaders{context, &resman};
            ASSERT_TRUE(shaders.initialize());
            RenderService service{nw::kernel::global_allocator()};
            service.configure(context, &shaders);
            const auto break_shader = [&] {
                if (missing) {
                    std::filesystem::remove(shader_dir / name);
                } else {
                    std::ofstream{shader_dir / name} << "#error injected model shader failure\n";
                }
            };
            const auto restore_shader = [&] {
                std::filesystem::copy_file(original / name, shader_dir / name,
                    std::filesystem::copy_options::overwrite_existing);
            };
            break_shader();
            EXPECT_FALSE(service.ensure_initialized());
            EXPECT_EQ(service.model_render_context().gpu, nullptr);
            restore_shader();
            ASSERT_TRUE(service.reload_shaders());
            ASSERT_NE(service.model_render_context().gpu, nullptr);
            for (const auto mesh : {ModelPipelineMeshKind::pbr_static,
                     ModelPipelineMeshKind::pbr_static_instanced, ModelPipelineMeshKind::pbr_skinned}) {
                for (const auto lighting : {MaterialLightingModel::pbr, MaterialLightingModel::nwn_diffuse}) {
                    for (const auto mode : {MaterialMode::opaque, MaterialMode::cutout,
                             MaterialMode::transparent, MaterialMode::water}) {
                        const bool supported = (mesh != ModelPipelineMeshKind::pbr_static_instanced || mode != MaterialMode::transparent)
                            && (mode != MaterialMode::water || mesh == ModelPipelineMeshKind::pbr_static);
                        EXPECT_EQ(service.model_backend().pipeline({mesh, mode, ModelPipelinePass::color, lighting}).valid(), supported);
                    }
                }
                for (const auto mode : {MaterialMode::opaque, MaterialMode::cutout}) {
                    EXPECT_TRUE(service.model_backend().pipeline({mesh, mode, ModelPipelinePass::shadow}).valid());
                }
            }
            break_shader();
            EXPECT_FALSE(service.reload_shaders());
            EXPECT_EQ(service.model_render_context().gpu, nullptr);
            restore_shader();
            EXPECT_TRUE(service.reload_shaders());
            service.shutdown_renderer();
        }
    }
}

TEST_F(RenderGfxResources, StorageDescriptorsPreserveGpuWritesAcrossReuseChangesAndFailedBinds)
{
    nw::ResourceManager resman{nw::kernel::global_allocator()};
    const auto root = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/render";
    ASSERT_TRUE(resman.add_base_container(root, "shaders", nw::ResourceType::hlsl));
    resman.build_registry();
    nw::render::ShaderProvider shaders{context, &resman};
    ASSERT_TRUE(shaders.initialize());
    const auto shader = shaders.get_shader("render_compute_smoke.cs.hlsl");
    const auto pipeline = create_compute_pipeline(context, {.cs = shader, .storage_buffer_count = 5});
    const auto small_pipeline = create_compute_pipeline(context, {.cs = shader, .uses_uniforms = false, .storage_buffer_count = 2});
    ASSERT_TRUE(pipeline.valid());
    ASSERT_TRUE(small_pipeline.valid());
    auto* vk = as_vulkan(context);
    const auto stride = static_cast<uint32_t>(std::max<VkDeviceSize>(4, vk->core->properties.limits.minStorageBufferOffsetAlignment));
    const uint32_t size = stride * 4;
    std::array<Handle<Buffer>, 3> buffers;
    std::array<std::vector<uint32_t>, 3> expected;
    for (size_t i = 0; i < buffers.size(); ++i) {
        buffers[i] = create_buffer(context, {.size = size, .usage = BufferUsage::Storage, .cpu_visible = true});
        ASSERT_TRUE(buffers[i].valid());
        expected[i].resize(size / sizeof(uint32_t));
        auto* mapped = map_buffer(buffers[i]);
        ASSERT_NE(mapped, nullptr);
        std::memset(mapped, 0, size);
        unmap_buffer(buffers[i]);
    }
    const auto stale = create_buffer(context, {.size = size, .usage = BufferUsage::Storage});
    destroy_buffer(stale);

    // Revisit the same command-list slot to verify recording resets its keys.
    for (uint32_t frame = 0; frame <= kFramesInFlight; ++frame) {
        auto* cmd = begin_frame(context);
        ASSERT_NE(cmd, nullptr);
        auto* commands = reinterpret_cast<VulkanCommandList*>(cmd);
        std::array spans{StorageSpan{buffers[0]}, StorageSpan{buffers[1]}, StorageSpan{buffers[2]}, StorageSpan{buffers[0]}, StorageSpan{buffers[1]}};
        const auto bind = [&](Handle<Pipeline> current = {}) {
            if (!current.valid()) { current = pipeline; }
            auto uniforms = allocate_uniform_span(context, 16);
            EXPECT_NE(uniforms.data, nullptr);
            if (uniforms.data) { std::memset(uniforms.data, 0, 16); }
            cmd_bind_pipeline(cmd, current);
            cmd_bind_compute_resources(cmd, current, uniforms, spans[0], spans[1], spans[2], spans[3], spans[4]);
        };
        const auto dispatch = [&](uint64_t encodes, uint64_t reuses, Handle<Pipeline> current = {}) {
            const auto before = commands->stats;
            bind(current);
            EXPECT_FALSE(commands->resource_bind_failed);
            EXPECT_EQ(commands->stats.storage_descriptor_encode_count - before.storage_descriptor_encode_count, encodes);
            EXPECT_EQ(commands->stats.storage_descriptor_reuse_count - before.storage_descriptor_reuse_count, reuses);
            cmd_dispatch(cmd, 1, 1, 1);
            for (size_t slot = 0; slot < 2; ++slot) {
                for (size_t i = 0; i < buffers.size(); ++i) {
                    if (spans[slot].buffer == buffers[i]) {
                        expected[i][spans[slot].offset / sizeof(uint32_t)] = slot == 0 ? 0x1234u : 0x5678u;
                    }
                }
            }
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(commands->buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        };
        dispatch(5, 0);
        dispatch(0, 5); // New uniform span, identical storage inputs.
        spans[0].offset = stride;
        dispatch(1, 4);
        spans[0].size = size - stride; // Explicit range equals the previous remainder.
        dispatch(0, 5);
        spans[0].size = stride;
        dispatch(1, 4);
        spans[0].buffer = buffers[2];
        dispatch(1, 4);
        std::swap(spans[0], spans[1]);
        dispatch(2, 3);
        dispatch(0, 2, small_pipeline); // Different descriptor set layout, same storage bytes.
        dispatch(0, 5);

        const auto reject = [&] {
            const auto before = commands->stats;
            bind();
            EXPECT_TRUE(commands->resource_bind_failed);
            cmd_dispatch(cmd, 1, 1, 1);
            EXPECT_EQ(commands->stats.resource_bind_failure_count, before.resource_bind_failure_count + 1);
            EXPECT_EQ(commands->stats.dropped_dispatch_count, before.dropped_dispatch_count + 1);
            EXPECT_EQ(commands->stats.storage_descriptor_encode_count, before.storage_descriptor_encode_count);
            EXPECT_EQ(commands->stats.storage_descriptor_reuse_count, before.storage_descriptor_reuse_count);
        };
        const auto valid = spans[0];
        for (const auto invalid : {StorageSpan{}, StorageSpan{stale}, StorageSpan{buffers[1], size + 1, 0},
                 StorageSpan{buffers[1], size, 0}, StorageSpan{buffers[1], 0, size + 1}}) {
            spans[0] = invalid;
            reject();
            spans[0] = valid;
            dispatch(0, 5);
        }
        auto* buffer = g_buffer_pool.get(valid.buffer);
        buffer->core = nullptr;
        reject();
        buffer->core = vk->core;
        dispatch(0, 5);

        CommandStats stats;
        ASSERT_TRUE(get_command_stats(cmd, stats));
        auto& ring_offset = vk->descriptor_ring_offset[commands->pool_index];
        const auto saved_offset = ring_offset;
        ring_offset = stats.descriptor_ring_capacity_bytes;
        reject();
        EXPECT_EQ(commands->stats.descriptor_allocation_failure_count, 1u);
        ring_offset = saved_offset;
        dispatch(0, 5);
        RecordProperty("storage_descriptor_bytes", static_cast<int>(vk->core->descriptor_buffer_props.storageBufferDescriptorSize));
        RecordProperty("cache_bytes_per_command_list", static_cast<int>(sizeof(commands->storage_descriptor_keys) + sizeof(commands->storage_descriptors) + commands->storage_descriptors.capacity()));
        end_frame(context);
        wait_idle(context);
        for (size_t i = 0; i < buffers.size(); ++i) {
            auto* storage = g_buffer_pool.get(buffers[i]);
            ASSERT_EQ(vmaInvalidateAllocation(vk->core->allocator, storage->allocation, 0, VK_WHOLE_SIZE), VK_SUCCESS);
            const auto* values = static_cast<const uint32_t*>(map_buffer(buffers[i]));
            ASSERT_NE(values, nullptr);
            EXPECT_EQ(std::vector<uint32_t>(values, values + expected[i].size()), expected[i]);
            unmap_buffer(buffers[i]);
        }
    }
    for (auto buffer : buffers) {
        destroy_buffer(buffer);
    }
    destroy_pipeline(context, small_pipeline);
    destroy_pipeline(context, pipeline);
}

TEST_F(RenderGfxResources, ModelUploadCleansUpAfterPartialHandleExhaustion)
{
    std::vector<Handle<Buffer>> reservations;
    reservations.reserve(g_buffer_pool.max_capacity());
    const auto release_reservations = create_scope_exit([&] {
        for (const auto handle : reservations)
            g_buffer_pool.destroy(handle);
    });
    // Reserve handle slots without allocating GPU memory. Exactly one slot
    // remains for the first geometry buffer; the index buffer must fail.
    for (;;) {
        const auto handle = g_buffer_pool.insert(VulkanBuffer{});
        if (!handle.valid()) break;
        reservations.push_back(handle);
    }
    ASSERT_FALSE(reservations.empty());
    g_buffer_pool.destroy(reservations.back());
    reservations.pop_back();

    auto* allocator = as_vulkan(context)->core->allocator;
    VmaTotalStatistics before{};
    vmaCalculateStatistics(allocator, &before);
    nw::render::ModelAsset asset;
    asset.materials.emplace_back();
    nw::render::ModelAssetPrimitive source;
    source.vertices.resize(3);
    source.indices = {0, 1, 2};
    asset.primitives.push_back(std::move(source));
    const auto result = nw::render::upload_model_asset(asset, context);
    EXPECT_FALSE(result.model);
    EXPECT_EQ(result.stats.buffer_failure_count, 1u);
    VmaTotalStatistics after{};
    vmaCalculateStatistics(allocator, &after);
    EXPECT_EQ(after.total.statistics.allocationCount, before.total.statistics.allocationCount);
    EXPECT_EQ(after.total.statistics.allocationBytes, before.total.statistics.allocationBytes);
}

} // namespace
