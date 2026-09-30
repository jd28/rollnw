#include "../tools/client/client_rml_runtime.hpp"
#include "../tools/client/item_editor_data_model.hpp"
#include "../tools/client/rml_nwgfx_renderer.hpp"
#include "../tools/ui/ui_v1.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/viewer/device.hpp>
#include <nw/render/viewer/session.hpp>
#include <nw/util/scope_exit.hpp>

#include <RmlUi/Core.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>

TEST(ClientGfxResources, AreaPreviewAndUiTransitionsRetireSubmittedResourcesSafely)
{
    using namespace nw::gfx;
    namespace viewer = nw::render::viewer;
    const bool owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (owns_video) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    }
    const auto video = create_scope_exit([&] { if (owns_video) { SDL_QuitSubSystem(SDL_INIT_VIDEO); } });
    auto* core = create_core({.app_name = "client-resource-transitions", .enable_validation = true});
    ASSERT_NE(core, nullptr);
    auto* ctx = create_context(core, {.width = 512, .height = 256});
    const auto graphics = create_scope_exit([&] {
        if (ctx) { destroy_context(ctx); }
        const auto report = validation_report(core);
        EXPECT_EQ(report.error_count, 0u) << report.first_error;
        EXPECT_EQ(report.warning_count, 0u) << report.first_warning;
        destroy_core(core);
    });
    ASSERT_NE(ctx, nullptr);
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod", false), nullptr);
    if (nw::kernel::resman().demand({nw::Resref{"tic01_j01_01"}, nw::ResourceType::mdl}).bytes.size() == 0) {
        GTEST_SKIP() << "DockerDemo desktop tile assets unavailable";
    }
    const std::filesystem::path root{ROLLNW_TEST_SOURCE_DIR};
    RmlNwgfxRenderer renderer;
    ASSERT_TRUE(renderer.initialize(core, ctx));
    viewer::ViewerDevice device{ctx, nw::kernel::resman()};
    ASSERT_TRUE(device.initialize({.shader_roots = {root / "lib/nw/render/shaders"}}));
    auto session = device.make_session();
    Rml::SystemInterface system;
    nw::toolset::ClientRmlRuntime ui{root / "tools/client/ui", nw::kernel::resman()};
    ASSERT_TRUE(ui.initialize(system, renderer, std::filesystem::path{ROLLNW_TEST_CLIENT_EXECUTABLE}.parent_path(), {512, 256}));
    renderer.on_resize(512, 256, ui.contexts().toolset);
    auto* document = ui.contexts().toolset->LoadDocumentFromMemory(
        "<rml><head><style>body {font-family:RollnwSans; font-size:20px; color:white; margin-left:270px;}</style></head>"
        "<body><p>Area and item preview</p><p>Resource lifetime verification</p></body></rml>");
    ASSERT_NE(document, nullptr);
    document->Show();
    ASSERT_TRUE(session->load_area("start"));
    const viewer::ViewerViewport viewport{0, 0, 256, 256};
    const auto frame = [&] {
        if (!renderer.begin_frame()) { return false; }
        session->tick(16);
        CommandStats before_ui{}, after_ui{};
        EXPECT_TRUE(get_command_stats(renderer.command_list(), before_ui));
        ui.contexts().toolset->Update();
        ui.contexts().toolset->Render();
        EXPECT_TRUE(get_command_stats(renderer.command_list(), after_ui));
        EXPECT_GT(after_ui.draw_count, before_ui.draw_count);
        EXPECT_EQ(after_ui.dropped_draw_count, before_ui.dropped_draw_count);
        // Match the client: paint the layout, then composite the scene viewport.
        renderer.finish_render_pass();
        session->render(renderer.command_list(), viewport);
        renderer.end_frame();
        return true;
    };
    // Keep the normal two-frame queue live: there is deliberately no test-only
    // idle wait before switching scenes or releasing UI geometry/textures.
    for (int iteration = 0; iteration < 3; ++iteration) {
        ASSERT_TRUE(frame());
        ASSERT_TRUE(frame());
        ASSERT_TRUE(session->load_object_file("test_data/user/development/nw_chicken.utc"));
        ASSERT_TRUE(frame());
        document->SetInnerRML("<p>Changing preview</p><p>Reloading area</p>");
        ASSERT_TRUE(session->load_area("start"));
        ASSERT_TRUE(frame());
        std::array<Rml::byte, 64> pixels{};
        pixels.fill(255);
        auto texture = renderer.GenerateTexture({pixels.data(), pixels.size()}, {4, 4});
        ASSERT_NE(texture, 0u);
        renderer.ReleaseTexture(texture);
        EXPECT_EQ(renderer.GenerateTexture(Rml::Span<const Rml::byte>{pixels.data(), 3}, {4, 4}), 0u);
    }
    std::filesystem::create_directories("tmp");
    ASSERT_TRUE(renderer.begin_frame());
    ui.contexts().toolset->Update();
    ui.contexts().toolset->Render();
    renderer.finish_render_pass();
    session->render(renderer.command_list(), viewport);
    ASSERT_TRUE(capture_screenshot(ctx, renderer.command_list(), "tmp/client-resource-transitions.png"));
    // Capture submitted its frame. Resume normal frame ownership before teardown.
    ASSERT_TRUE(frame());
    session.reset();
    ui.release_render_resources();
    ui.shutdown();
    device.shutdown();
    renderer.shutdown();
    const auto stats = resource_stats(ctx);
    EXPECT_GT(stats.upload_texture_count, stats.upload_batch_count - stats.upload_failure_count);
    EXPECT_EQ(stats.upload_failure_count, 3u);
}
