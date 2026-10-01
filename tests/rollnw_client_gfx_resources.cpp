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

TEST(ClientGfxResources, ItemAppearanceActionsAlignAtEditorWidths)
{
    using namespace nw::gfx;
    const bool owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (owns_video) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    }
    const auto video = create_scope_exit([&] { if (owns_video) { SDL_QuitSubSystem(SDL_INIT_VIDEO); } });
    auto* core = create_core({.app_name = "item-appearance-actions", .enable_validation = true});
    ASSERT_NE(core, nullptr);
    auto* ctx = create_context(core, {.width = 1000, .height = 760});
    const auto graphics = create_scope_exit([&] {
        if (ctx) { destroy_context(ctx); }
        const auto report = validation_report(core);
        EXPECT_EQ(report.error_count, 0u) << report.first_error;
        EXPECT_EQ(report.warning_count, 0u) << report.first_warning;
        destroy_core(core);
    });
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod", false));
    auto* item = nw::kernel::objects().load_file<nw::Item>("test_data/user/development/cloth028.uti");
    ASSERT_NE(item, nullptr);
    const auto item_cleanup = create_scope_exit([&] { nw::kernel::objects().destroy(item->handle()); });
    nw::toolset::VirtualListHost lists;
    nw::toolset::ItemEditor editor;
    ASSERT_TRUE(editor.refresh(nw::kernel::runtime(), item->handle(), lists));
    ASSERT_EQ(editor.appearance_input().parts.size(), 19u);
    const std::filesystem::path root{ROLLNW_TEST_SOURCE_DIR};
    RmlNwgfxRenderer renderer;
    ASSERT_TRUE(renderer.initialize(core, ctx));
    Rml::SystemInterface system;
    nw::toolset::ClientRmlRuntime ui{root / "tools/client/ui", nw::kernel::resman()};
    ASSERT_TRUE(ui.initialize(system, renderer, std::filesystem::path{ROLLNW_TEST_CLIENT_EXECUTABLE}.parent_path(), {1000, 760}));
    auto* context = ui.contexts().toolset;
    renderer.on_resize(1000, 760, context);
    nw::toolset::ItemEditorDataModel model;
    ASSERT_TRUE(model.initialize(*context, [](auto, auto, auto&) { return true; }));
    model.refresh(editor.appearance_input());
    // Render the actual editor pane; scene preview compositing is tested separately.
    auto* document = context->LoadDocumentFromMemory(
        "<rml><head><link type='text/css' href='ui/panel.rcss'/>"
        "<link type='text/css' href='ui/item_editor.rcss'/>"
        "<link type='text/template' href='ui/object_locstring_editor.rml'/>"
        "<link type='text/template' href='ui/item_editor.rml'/>"
        "<style>.object_workbench.item_workbench {width:100%; height:100%;} #item_preview {display:none;}</style>"
        "</head><body><template src='item-workbench'/></body></rml>");
    ASSERT_NE(document, nullptr);
    document->Show();
    context->Update();
    auto* appearance = document->GetElementById("item_surface_appearance");
    ASSERT_NE(appearance, nullptr);
    appearance->SetClass("active", true);
    document->GetElementById("item_tab_appearance")->SetClass("active", true);
    std::filesystem::create_directories("tmp");
    const auto capture = [&](const std::string& name) {
        EXPECT_TRUE(renderer.begin_frame());
        context->Update();
        context->Render();
        renderer.finish_render_pass();
        EXPECT_TRUE(capture_screenshot(ctx, renderer.command_list(), name.c_str()));
        // Screenshot submits its frame; resume renderer ownership before teardown.
        EXPECT_TRUE(renderer.begin_frame());
        context->Render();
        renderer.end_frame();
    };
    const auto check_header_action = [&](const char* id) {
        auto* button = document->GetElementById(id);
        EXPECT_NE(button, nullptr);
        if (!button) { return; }
        EXPECT_TRUE(button->IsVisible(true));
        auto* header = button->GetParentNode();
        EXPECT_NEAR(button->GetOffsetHeight(), 27.0f, 0.1f);
        EXPECT_NEAR(button->GetAbsoluteTop() + button->GetOffsetHeight() / 2,
            header->GetAbsoluteTop() + (header->GetOffsetHeight() - 1) / 2, 1.0f);
        EXPECT_NEAR(button->GetAbsoluteLeft() + button->GetOffsetWidth(),
            header->GetAbsoluteLeft() + header->GetOffsetWidth() - 9, 1.0f);
        auto* label = button->GetChild(0);
        ASSERT_NE(label, nullptr);
        EXPECT_GT(label->GetOffsetWidth(), 0.0f);
        EXPECT_NEAR(label->GetAbsoluteLeft() + label->GetOffsetWidth() / 2,
            button->GetAbsoluteLeft() + button->GetOffsetWidth() / 2, 1.0f);
    };
    for (const int width : {560, 1000}) {
        SCOPED_TRACE(width);
        document->GetElementById("object_workbench")->SetProperty("width", std::to_string(width) + "px");
        ASSERT_TRUE(editor.close_appearance(lists));
        model.refresh(editor.appearance_input());
        context->Update();
        auto* global_reset = document->GetElementById("item_global_reset");
        auto* global_color = document->GetElementById("item_global_color");
        ASSERT_NE(global_reset, nullptr);
        ASSERT_NE(global_color, nullptr);
        auto* header = global_reset->GetParentNode()->GetParentNode();
        EXPECT_NEAR(global_reset->GetAbsoluteLeft() + global_reset->GetOffsetWidth(),
            header->GetAbsoluteLeft() + header->GetOffsetWidth() - 9, 1.0f);
        EXPECT_NEAR(global_reset->GetAbsoluteTop() + global_reset->GetOffsetHeight() / 2,
            header->GetAbsoluteTop() + (header->GetOffsetHeight() - 1) / 2, 1.0f);
        Rml::ElementList resets;
        document->GetElementsByClassName(resets, "item_model_reset");
        size_t visible_resets = 0;
        for (auto* reset : resets) {
            if (!reset->IsVisible(true)) { continue; }
            ++visible_resets;
            auto* color = reset->GetPreviousSibling();
            ASSERT_NE(color, nullptr);
            EXPECT_NEAR(reset->GetOffsetHeight(), 25.0f, 0.1f);
            EXPECT_NEAR(reset->GetOffsetWidth(), 27.0f, 0.1f);
            EXPECT_NEAR(reset->GetAbsoluteTop() + reset->GetOffsetHeight() / 2,
                color->GetAbsoluteTop() + color->GetOffsetHeight() / 2, 1.0f);
            EXPECT_NEAR(reset->GetAbsoluteLeft(), global_reset->GetAbsoluteLeft(), 1.0f);
            EXPECT_NEAR(color->GetAbsoluteLeft(), global_color->GetAbsoluteLeft(), 1.0f);
            auto* glyph = reset->GetChild(0);
            ASSERT_NE(glyph, nullptr);
            EXPECT_TRUE(glyph->IsClassSet("panel_close_glyph"));
            EXPECT_NEAR(glyph->GetAbsoluteTop() + glyph->GetOffsetHeight() / 2,
                reset->GetAbsoluteTop() + reset->GetOffsetHeight() / 2, 1.0f);
        }
        EXPECT_EQ(visible_resets, 20u);
        Rml::ElementList part_resets;
        document->GetElementsByClassName(part_resets, "item_part_model_reset");
        EXPECT_EQ(std::count_if(part_resets.begin(), part_resets.end(), [](Rml::Element* reset) {
            return reset->IsVisible(true) && reset->GetAttribute<Rml::String>("title", "") == "Clear variation (None)";
        }),
            1);
        capture("tmp/item-appearance-actions-" + std::to_string(width) + ".png");
        ASSERT_TRUE(editor.open_color(18, 0, lists));
        model.refresh(editor.appearance_input());
        context->Update();
        check_header_action("item_color_inherit");
        capture("tmp/item-inherit-action-" + std::to_string(width) + ".png");
    }
    model.shutdown();
    ui.release_render_resources();
    ui.shutdown();
    renderer.shutdown();
}

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
