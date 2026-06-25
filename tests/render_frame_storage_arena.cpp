#include <gtest/gtest.h>

#include <nw/gfx/gfx.hpp>
#include <nw/render/frame_storage_arena.hpp>

#include <limits>

namespace {

struct TestGfxRuntime {
    nw::gfx::Core* core = nullptr;
    nw::gfx::Context* context = nullptr;
    bool owns_sdl_video = false;

    ~TestGfxRuntime()
    {
        if (context) {
            nw::gfx::wait_idle(context);
            nw::gfx::destroy_context(context);
        }
        if (core) {
            nw::gfx::destroy_core(core);
        }
        if (owns_sdl_video) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }

    bool initialize()
    {
        if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0u) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                return false;
            }
            owns_sdl_video = true;
        }

        nw::gfx::CoreConfig core_config{};
        core_config.app_name = "rollnw_test";
        core_config.enable_validation = false;
        core = nw::gfx::create_core(core_config);
        if (!core) {
            return false;
        }

        nw::gfx::ContextDesc context_desc{};
        context_desc.width = 64;
        context_desc.height = 64;
        context = nw::gfx::create_context(core, context_desc);
        return context != nullptr;
    }
};

} // namespace

TEST(RenderFrameStorageArena, ResetDropsNonRetainedOverflowBlocks)
{
    TestGfxRuntime gfx;
    if (!gfx.initialize()) {
        GTEST_SKIP() << "headless graphics context unavailable";
    }

    nw::render::FrameStorageArena arena;
    ASSERT_TRUE(arena.reset(gfx.context, 1, 1));
    ASSERT_EQ(arena.debug_block_count(), 1u);
    const size_t initial_capacity = arena.debug_retained_capacity();
    ASSERT_GT(initial_capacity, 0u);
    ASSERT_LE(initial_capacity, static_cast<size_t>(std::numeric_limits<uint32_t>::max()));

    auto first = arena.allocate_mapped(gfx.context, static_cast<uint32_t>(initial_capacity), 64);
    ASSERT_TRUE(first.span.buffer.valid());
    auto overflow = arena.allocate_mapped(gfx.context, 1, 64);
    ASSERT_TRUE(overflow.span.buffer.valid());
    ASSERT_EQ(arena.debug_block_count(), 2u);

    ASSERT_TRUE(arena.reset(gfx.context, 2, 0));
    EXPECT_EQ(arena.debug_block_count(), 1u);
    EXPECT_GE(arena.debug_retained_capacity(), initial_capacity);

    auto reused = arena.allocate_mapped(gfx.context, 1024, 64);
    EXPECT_TRUE(reused.span.buffer.valid());
    EXPECT_EQ(arena.debug_block_count(), 1u);
}
