#include <nw/gfx/backends/vulkan/vulkan_internal.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/render/shader_provider.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <filesystem>
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

    std::vector<uint8_t> read_mips(Handle<Texture> handle, size_t bytes_per_pixel)
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
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
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
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, image->mip_levels, 0, 1};
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

TEST_F(RenderGfxResources, FailedBindsCannotReuseCachedStateAndRebindsRecoverIndependently)
{
    auto& resman = nw::kernel::resman();
    if (resman.is_frozen()) { resman.unfreeze(); }
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
} // namespace
