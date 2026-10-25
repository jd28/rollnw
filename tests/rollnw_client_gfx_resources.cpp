#include "../tools/client/client_rml_runtime.hpp"
#include "../tools/client/item_editor_data_model.hpp"
#include "../tools/client/play_preview_inventory.hpp"
#include "../tools/client/rml_nwgfx_renderer.hpp"
#include "../tools/client/runtime_input.hpp"
#include "../tools/ui/ui_v1.hpp"

#include <nw/gfx/gfx.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/render/viewer/device.hpp>
#include <nw/render/viewer/session.hpp>
#include <nw/util/scope_exit.hpp>

#include <RmlUi/Core.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
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

TEST(ClientGfxResources, PlayPreviewItemLoopUsesQueuedControlsAndRestoresAuthoredVisuals)
{
    using namespace nw::gfx;
    using namespace nw::toolset;
    namespace viewer = nw::render::viewer;
    const bool owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (owns_video) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    }
    const auto video = create_scope_exit([&] { if (owns_video) { SDL_QuitSubSystem(SDL_INIT_VIDEO); } });
    auto* core = create_core({.app_name = "preview-item-loop", .enable_validation = true});
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
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);
    auto* source = nw::kernel::objects().load<nw::Item>("nw_wdbqs001");
    ASSERT_NE(source, nullptr);
    area->items.push_back(source);
    auto& components = nw::kernel::objects().components();
    ASSERT_TRUE(components.set_area(source->handle(), area->handle().id));
    ASSERT_TRUE(components.set_position(source->handle(), module->entry_position));
    const std::filesystem::path root{ROLLNW_TEST_SOURCE_DIR};
    RmlNwgfxRenderer renderer;
    ASSERT_TRUE(renderer.initialize(core, ctx));
    viewer::ViewerDevice device{ctx, nw::kernel::resman()};
    ASSERT_TRUE(device.initialize({.shader_roots = {root / "lib/nw/render/shaders"}}));
    auto scene = device.make_session();
    scene->set_preview_scene_load_options({.area_object_editing = true});
    ASSERT_TRUE(scene->load_live_object(area->handle(), "preview-item-loop"));
    const auto baseline_models = scene->scene()->static_models.size();
    const auto selection = scene->scene()->active_object;
    const viewer::ViewerViewport viewport{0, 0, 1000, 760};
    ASSERT_TRUE(scene->fit_to_scene(viewport));
    scene->camera().set_orbit_view(module->entry_position, 8.0f, 180.0f, 40.0f);
    Rml::SystemInterface system;
    ClientRmlRuntime ui{root / "tools/client/ui", nw::kernel::resman()};
    ASSERT_TRUE(ui.initialize(system, renderer, std::filesystem::path{ROLLNW_TEST_CLIENT_EXECUTABLE}.parent_path(), {1000, 760}));
    ASSERT_TRUE(ui.create_overlay_contexts({1000, 760}));
    auto* context = ui.contexts().palette;
    ASSERT_NE(context, nullptr);
    auto* backdrop = ui.contexts().toolset->LoadDocumentFromMemory(
        "<rml><head><style>body {margin:0; width:100%; height:100%; background:#171a1f;}</style></head><body></body></rml>");
    ASSERT_NE(backdrop, nullptr);
    backdrop->Show();
    renderer.on_resize(1000, 760, context);
    ItemIconTextureCache inventory_icons;
    renderer.set_generated_textures(&inventory_icons.textures, nullptr);
    PlayPreviewState preview;
    preview.inventory_icons = &inventory_icons;
    preview.inventory_document = ui.load_document(*context,
        nw::Resource{nw::Resref{"ui/preview_inventory"}, nw::ResourceType::rml});
    ASSERT_NE(preview.inventory_document, nullptr);
    PlayPreviewInventoryListener listener{preview};
    for (const auto* event : play_inventory_events) {
        context->AddEventListener(event, &listener);
    }
    const auto listener_cleanup = create_scope_exit([&] {
        for (const auto* event : play_inventory_events) {
            context->RemoveEventListener(event, &listener);
        }
    });
    std::filesystem::create_directories("tmp");
    const auto capture = [&](const char* name) {
        EXPECT_TRUE(renderer.begin_frame());
        ui.contexts().toolset->Update();
        ui.contexts().toolset->Render();
        renderer.finish_render_pass();
        scene->tick(0);
        scene->render(renderer.command_list(), viewport);
        context->Update();
        context->Render();
        renderer.finish_render_pass();
        if (name) {
            EXPECT_TRUE(capture_screenshot(ctx, renderer.command_list(), name));
            EXPECT_TRUE(renderer.begin_frame());
            context->Render();
        }
        renderer.end_frame();
    };
    capture("tmp/preview-items-before.png");

    // Hover is independent of editor selection and preserves existing palette
    // overrides. Find a visible pointer sample from the item's actual bounds.
    scene->set_area_object_selection_enabled(false);
    const auto source_row = std::ranges::find_if(scene->scene()->static_area_model_info,
        [&](const auto& row) { return row.object == source->handle(); });
    ASSERT_NE(source_row, scene->scene()->static_area_model_info.end());
    const auto source_model = static_cast<size_t>(source_row - scene->scene()->static_area_model_info.begin());
    auto* source_instance = scene->scene()->static_model_instance(source_model);
    ASSERT_NE(source_instance, nullptr);
    const auto original_overrides = source_instance->material_override_handles;
    const auto original_override_count = scene->scene()->material_overrides.size();
    const auto original_emissive = scene->scene()->static_models[source_model]->materials.front().emissive;
    const auto clip = scene->camera().get_projection_matrix() * scene->camera().get_view_matrix()
        * glm::vec4(source_instance->current_bounds.center(), 1.0f);
    ASSERT_GT(clip.w, 0.0f);
    const glm::vec2 center{(clip.x / clip.w + 1.0f) * 0.5f * static_cast<float>(viewport.width),
        (clip.y / clip.w + 1.0f) * 0.5f * static_cast<float>(viewport.height)};
    std::vector<glm::vec2> pixels;
    for (int y = -12; y <= 12; y += 2) {
        for (int x = -12; x <= 12; x += 2) {
            pixels.push_back(center + glm::vec2{x, y});
        }
    }
    std::vector<viewer::AreaObjectSelection> hits(pixels.size());
    viewer::select_area_pointer_objects(pixels, scene->camera(), viewport,
        *scene->scene()->area_render_scene, *scene->scene(), hits,
        {.triggers_enabled = false, .encounters_enabled = false});
    const auto hit = std::ranges::find_if(hits, [&](const auto& row) { return row.object == source->handle(); });
    ASSERT_NE(hit, hits.end());
    const auto hover_pixel = pixels[static_cast<size_t>(hit - hits.begin())];
    ASSERT_EQ(scene->update_area_item_hover(hover_pixel, viewport), source->handle());
    const auto highlighted = source_instance->material_override_handles;
    ASSERT_FALSE(highlighted.empty());
    EXPECT_NE(highlighted, original_overrides);
    EXPECT_EQ(scene->scene()->active_object, selection);
    EXPECT_EQ(scene->scene()->static_models[source_model]->materials.front().emissive, original_emissive);
    const auto highlighted_count = scene->scene()->material_overrides.size();
    for (int frame = 0; frame < 10; ++frame) {
        EXPECT_EQ(scene->update_area_item_hover(hover_pixel, viewport), source->handle());
        EXPECT_EQ(source_instance->material_override_handles, highlighted);
        EXPECT_EQ(scene->scene()->material_overrides.size(), highlighted_count);
    }
    capture("tmp/preview-items-hover.png");
    EXPECT_EQ(scene->update_area_item_hover(std::nullopt, viewport), nw::ObjectHandle{});
    EXPECT_EQ(source_instance->material_override_handles, original_overrides);
    EXPECT_EQ(scene->scene()->material_overrides.size(), original_override_count);
    capture("tmp/preview-items-hover-cleared.png");

    // A stationary pointer follows camera changes, and replacement removes
    // the temporary material handles even when the item was still hovered.
    ASSERT_EQ(scene->update_area_item_hover(hover_pixel, viewport), source->handle());
    const auto saved_camera = scene->camera();
    scene->camera().set_orbit_view(module->entry_position + glm::vec3{50.0f, 0.0f, 0.0f}, 8.0f, 180.0f, 40.0f);
    EXPECT_EQ(scene->update_area_item_hover(hover_pixel, viewport), nw::ObjectHandle{});
    EXPECT_EQ(scene->scene()->material_overrides.size(), original_override_count);
    scene->camera() = saved_camera;
    ASSERT_EQ(scene->update_area_item_hover(hover_pixel, viewport), source->handle());
    const auto replacement_highlighted = source_instance->material_override_handles;
    const auto started = start_toolset_preview(preview.session, {
                                                                    .area = area->handle(),
                                                                    .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
                                                                    .spawn_position = module->entry_position,
                                                                });
    ASSERT_TRUE(started.ok()) << started.diagnostic;
    auto world = toolset_preview_item_world(preview.session);
    std::vector<nw::ObjectHandle> authored{world.authored.begin(), world.authored.end()};
    std::vector<nw::ObjectHandle> additions{world.ground.begin(), world.ground.end()};
    additions.push_back(started.actor);
    ASSERT_TRUE(scene->replace_area_transient_visuals(authored, additions, world.area).ok());
    for (const auto handle : replacement_highlighted) {
        EXPECT_FALSE(scene->scene()->material_overrides.valid(handle));
    }
    const auto item = world.ground.back();
    auto* actor = nw::kernel::objects().get<nw::Creature>(started.actor);
    ASSERT_NE(actor, nullptr);
    const auto has_visual = [&](nw::ObjectHandle handle) {
        return std::ranges::any_of(scene->scene()->static_area_model_info,
            [&](const auto& row) { return row.object == handle; });
    };
    EXPECT_FALSE(has_visual(source->handle()));
    EXPECT_TRUE(has_visual(item));
    preview.inventory_visible = true;
    sync_play_preview_inventory(preview);
    context->Update();
    capture("tmp/preview-items-inventory.png");
    auto* panel = preview.inventory_document->GetElementById("preview_inventory");
    EXPECT_GT(panel->GetOffsetHeight(), 200);
    EXPECT_LT(panel->GetOffsetHeight(), 760);
    // The built-in handle moves this window without starting an item gesture.
    auto* move_handle = preview.inventory_document->GetElementById("preview_inventory_move");
    ASSERT_NE(move_handle, nullptr);
    const auto original_position = Rml::Vector2f{panel->GetAbsoluteLeft(), panel->GetAbsoluteTop()};
    const auto move_window = [&](int dx, int dy) {
        const int x = static_cast<int>(move_handle->GetAbsoluteLeft() + 30);
        const int y = static_cast<int>(move_handle->GetAbsoluteTop() + 10);
        context->ProcessMouseMove(x, y, 0);
        context->ProcessMouseButtonDown(0, 0);
        context->ProcessMouseMove(x + dx, y + dy, 0);
        context->ProcessMouseButtonUp(0, 0);
        context->Update();
    };
    move_window(110, -60);
    EXPECT_NEAR(panel->GetAbsoluteLeft(), original_position.x + 110, 1.0f);
    EXPECT_NEAR(panel->GetAbsoluteTop(), original_position.y - 60, 1.0f);
    EXPECT_FALSE(preview.inventory_dragging);
    EXPECT_FALSE(preview.pending_item);
    // Hover and item drag tests below run after moving the window.
    auto* slots = preview.inventory_document->GetElementById("preview_equipment_slots");
    ASSERT_NE(slots, nullptr);
    EXPECT_GT(slots->GetOffsetHeight(), 100.0f);
    EXPECT_GT(slots->GetAbsoluteTop(), panel->GetAbsoluteTop());
    Rml::ElementList equipment_controls;
    slots->QuerySelectorAll(equipment_controls, ".preview_equipment_slot");
    ASSERT_EQ(equipment_controls.size(), 14u);
    for (auto* slot : equipment_controls) {
        EXPECT_GE(slot->GetAbsoluteLeft(), slots->GetAbsoluteLeft());
        EXPECT_GE(slot->GetAbsoluteTop(), slots->GetAbsoluteTop());
        EXPECT_LE(slot->GetAbsoluteLeft() + slot->GetOffsetWidth(), slots->GetAbsoluteLeft() + slots->GetOffsetWidth());
        EXPECT_LE(slot->GetAbsoluteTop() + slot->GetOffsetHeight(), slots->GetAbsoluteTop() + slots->GetOffsetHeight());
    }
    const auto sample_equipment_pose = [&] {
        const auto action = toolset_preview_action_state(preview.session);
        viewer::AreaCreatureAnimationInput input{.owner = started.actor};
        if (action.phase == nw::gameplay::ActionPhase::drawing) {
            const auto duration = action.hand_timings[0].duration_ticks;
            const auto pose_tick = action.kind == nw::gameplay::ActionKind::unequip
                ? duration - std::min(action.elapsed_ticks, duration)
                : action.elapsed_ticks;
            input.action = viewer::AreaCreatureActionAnimation::draw_right;
            input.action_time = static_cast<float>(pose_tick) / nw::gameplay::action_ticks_per_second;
        }
        ASSERT_EQ(scene->update_area_creature_animations({&input, 1}).rejected_input_count, 0u);
        // Inspect a completed render so pose sampling and dependent attachment
        // updates run together, just as they do in the client.
        capture(nullptr);
        if (input.action != viewer::AreaCreatureActionAnimation::none) {
            size_t sampled = 0;
            for (size_t model = 0; model < scene->scene()->static_area_model_info.size(); ++model) {
                if (scene->scene()->static_area_model_info[model].object != started.actor) { continue; }
                const auto* instance = scene->scene()->static_model_instance(model);
                if (!instance || !instance->animation.enabled) { continue; }
                const auto& clips = scene->scene()->static_models[model]->animations;
                const auto overlay = instance->animation.overlay;
                if (overlay.clip >= clips.size() || clips[overlay.clip].name != "drwright") { continue; }
                EXPECT_TRUE(instance->animation.looping);
                EXPECT_FLOAT_EQ(instance->animation.playback_rate, 1.0f);
                EXPECT_FLOAT_EQ(overlay.time, std::min(input.action_time, clips[overlay.clip].duration));
                nw::render::Pose base;
                ASSERT_TRUE(instance->animation.backend->sample(instance->animation.clip, instance->animation.time, base));
                ASSERT_EQ(instance->animation.pose.local.size(), base.local.size());
                ASSERT_EQ(clips[overlay.clip].tracks.size(), base.local.size());
                for (size_t joint = 0; joint < base.local.size(); ++joint) {
                    const auto& track = clips[overlay.clip].tracks[joint];
                    const auto& pose = instance->animation.pose.local[joint];
                    if (track.translations.empty()) { EXPECT_EQ(pose.translation, base.local[joint].translation); }
                    if (track.rotations.empty()) { EXPECT_EQ(pose.rotation, base.local[joint].rotation); }
                    if (track.scales.empty()) { EXPECT_EQ(pose.scale, base.local[joint].scale); }
                }
                ++sampled;
            }
            EXPECT_GT(sampled, 0u);
        }
    };
    for (const auto action : {PreviewItemAction::pickup, PreviewItemAction::equip,
             PreviewItemAction::unequip, PreviewItemAction::drop, PreviewItemAction::pickup}) {
        SCOPED_TRACE(static_cast<int>(action));
        sync_play_preview_inventory(preview);
        context->Update();
        const auto revision = toolset_preview_item_world(preview.session).revision;
        auto* slot = preview.inventory_document->QuerySelector(".preview_equipment_slot[data-slot='4']");
        auto* board = preview.inventory_document->GetElementById("preview_inventory_board");
        ASSERT_NE(slot, nullptr);
        ASSERT_NE(board, nullptr);
        if (action == PreviewItemAction::equip || action == PreviewItemAction::unequip) {
            Rml::Element* source_element = slot;
            Rml::Element* destination = board;
            if (action == PreviewItemAction::equip) {
                const auto carried = std::ranges::find(preview.inventory_rows.inventory, item, &InventoryRow::item);
                ASSERT_NE(carried, preview.inventory_rows.inventory.end());
                const auto index = static_cast<size_t>(carried - preview.inventory_rows.inventory.begin());
                source_element = preview.inventory_document->QuerySelector(
                    ".preview_inventory_item[data-item='" + std::to_string(index) + "']");
                destination = slot;
            }
            ASSERT_NE(source_element, nullptr);
            const auto center_of = [](Rml::Element& element) {
                return Rml::Vector2i{static_cast<int>(element.GetAbsoluteLeft() + element.GetOffsetWidth() / 2),
                    static_cast<int>(element.GetAbsoluteTop() + element.GetOffsetHeight() / 2)};
            };
            const auto from = center_of(*source_element);
            const auto to = center_of(*destination);
            context->ProcessMouseMove(from.x, from.y, 0);
            context->ProcessMouseButtonDown(0, 0);
            context->ProcessMouseMove(from.x + 4, from.y + 4, 0);
            ASSERT_TRUE(preview.inventory_dragging);
            EXPECT_TRUE(source_element->IsClassSet("inventory_drag_source"));
            // A dirty UI must not replace the native drag source before release.
            preview.inventory_dirty = true;
            sync_play_preview_inventory(preview);
            EXPECT_EQ(preview.inventory_document->GetElementById("preview_inventory_board"), board);
            if (action == PreviewItemAction::equip) {
                auto* head = preview.inventory_document->QuerySelector(".preview_equipment_slot[data-slot='0']");
                ASSERT_NE(head, nullptr);
                const auto incompatible = center_of(*head);
                context->ProcessMouseMove(incompatible.x, incompatible.y, 0);
                context->Update();
                EXPECT_FALSE(head->IsClassSet("inventory_drop_target"));
                EXPECT_TRUE(head->IsClassSet("inventory_drop_invalid"));
                EXPECT_TRUE(head->QuerySelector(".inventory_drop_outline")->IsVisible());
                capture("tmp/preview-inventory-invalid-equip.png");
            }
            context->ProcessMouseMove(to.x, to.y, 0);
            context->Update();
            EXPECT_TRUE(destination->IsClassSet("inventory_drop_target"));
            EXPECT_FALSE(preview.pending_item);
            capture(action == PreviewItemAction::equip ? "tmp/preview-inventory-drag-equip.png" : "tmp/preview-inventory-drag-pack.png");
            context->ProcessMouseButtonUp(0, 0);
            EXPECT_FALSE(preview.inventory_dragging);
            EXPECT_FALSE(source_element->IsClassSet("inventory_drag_source"));
            EXPECT_FALSE(destination->IsClassSet("inventory_drop_target"));
        } else if (action == PreviewItemAction::drop) {
            const auto carried = std::ranges::find(preview.inventory_rows.inventory, item, &InventoryRow::item);
            ASSERT_NE(carried, preview.inventory_rows.inventory.end());
            const auto index = static_cast<size_t>(carried - preview.inventory_rows.inventory.begin());
            auto* cell = preview.inventory_document->QuerySelector(
                ".preview_inventory_item[data-item='" + std::to_string(index) + "']");
            ASSERT_NE(cell, nullptr);
            cell->DispatchEvent("click", {});
            Rml::Dictionary key_parameters;
            key_parameters["key_identifier"] = static_cast<int>(Rml::Input::KI_DELETE);
            cell->DispatchEvent("keydown", key_parameters);
        } else {
            // Pickup comes from the world, not an inventory-panel shortcut.
            preview.pending_item = PreviewItemRequest{item};
        }
        ASSERT_TRUE(preview.pending_item);
        EXPECT_EQ(toolset_preview_item_world(preview.session).revision, revision);
        PreviewInputSample sample;
        ASSERT_TRUE(set_preview_item_action(sample, *preview.pending_item));
        preview.pending_item.reset();
        auto tick = tick_toolset_preview(preview.session, {&sample, 1}, preview.spatial_rows, preview.locomotion_rows);
        sample = {};
        const bool hand_action = action == PreviewItemAction::equip || action == PreviewItemAction::unequip;
        if (hand_action) {
            ASSERT_EQ(toolset_preview_action_state(preview.session).phase, nw::gameplay::ActionPhase::drawing);
            EXPECT_FALSE(tick.action_result.changed);
        }
        for (int i = 0; i < 120 && !tick.action_result.changed && toolset_preview_action_state(preview.session).phase != nw::gameplay::ActionPhase::finished; ++i) {
            if (hand_action) {
                sample_equipment_pose();
                EXPECT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::righthand) != nullptr, action == PreviewItemAction::unequip);
                const auto state = toolset_preview_action_state(preview.session);
                const auto marker = action == PreviewItemAction::equip ? state.hand_timings[0].equip_tick : state.hand_timings[0].unequip_tick;
                if (state.elapsed_ticks == marker / 2) {
                    capture(action == PreviewItemAction::equip ? "tmp/preview-equip-windup.png" : "tmp/preview-unequip-windup.png");
                }
            }
            tick = tick_toolset_preview(preview.session, {&sample, 1}, preview.spatial_rows, preview.locomotion_rows);
        }
        ASSERT_EQ(tick.status, PreviewStatus::ok);
        const std::array results{tick.action_result};
        ASSERT_TRUE(results[0].ok()) << preview_item_message(results[0]);
        world = toolset_preview_item_world(preview.session);
        std::vector<nw::ObjectHandle> removed;
        std::vector<nw::ObjectHandle> added;
        if (results[0].equipment_changed) {
            removed.push_back(started.actor);
            added.push_back(started.actor);
        } else if (action == PreviewItemAction::pickup) {
            removed.push_back(item);
        } else {
            added.push_back(item);
        }
        const size_t model_count_before = scene->scene()->static_models.size();
        std::string base_clip;
        float base_time = 0.0f;
        if (hand_action) {
            for (size_t model = 0; model < scene->scene()->static_models.size(); ++model) {
                if (scene->scene()->static_area_model_info[model].object != started.actor) { continue; }
                const auto* instance = scene->scene()->static_model_instance(model);
                if (!instance || !instance->animation.enabled) { continue; }
                base_clip = scene->scene()->static_models[model]->animations[instance->animation.clip].name;
                base_time = instance->animation.time;
                break;
            }
            ASSERT_FALSE(base_clip.empty());
        }
        ASSERT_TRUE(scene->replace_area_transient_visuals(removed, added, world.area).ok());
        if (hand_action) {
            size_t retained = 0;
            for (size_t model = 0; model < scene->scene()->static_models.size(); ++model) {
                if (scene->scene()->static_area_model_info[model].object != started.actor) { continue; }
                const auto* instance = scene->scene()->static_model_instance(model);
                if (!instance || !instance->animation.enabled) { continue; }
                const auto& clips = scene->scene()->static_models[model]->animations;
                if (clips[instance->animation.clip].name == base_clip) {
                    EXPECT_FLOAT_EQ(instance->animation.time, base_time);
                    ++retained;
                }
            }
            EXPECT_GT(retained, 0u);
        }
        if (action == PreviewItemAction::equip) { EXPECT_GT(scene->scene()->static_models.size(), model_count_before); }
        if (action == PreviewItemAction::unequip) { EXPECT_LT(scene->scene()->static_models.size(), model_count_before); }
        EXPECT_EQ(has_visual(item), action == PreviewItemAction::drop);
        EXPECT_TRUE(has_visual(started.actor));
        if (hand_action) { sample_equipment_pose(); }
        preview.item_message.clear();
        preview.inventory_dirty = true;
        sync_play_preview_inventory(preview);
        capture(action == PreviewItemAction::equip  ? "tmp/preview-items-equipped.png"
                : action == PreviewItemAction::drop ? "tmp/preview-items-dropped.png"
                                                    : "tmp/preview-items-action.png");
        if (hand_action) {
            for (uint32_t time = 0; time < 120 && toolset_preview_action_state(preview.session).phase == nw::gameplay::ActionPhase::drawing; ++time) {
                const auto recovery = tick_toolset_preview(preview.session, {&sample, 1}, preview.spatial_rows, preview.locomotion_rows);
                EXPECT_EQ(recovery.action_result.status, nw::gameplay::ActionStatus::none);
                sample_equipment_pose();
            }
            EXPECT_EQ(toolset_preview_action_state(preview.session).phase, nw::gameplay::ActionPhase::finished);
            capture(action == PreviewItemAction::equip ? "tmp/preview-equip-finished.png" : "tmp/preview-unequip-finished.png");
        }
    }
    // A carried item keeps the clicked cell under the pointer when rearranged.
    auto* pack_item = preview.inventory_document->QuerySelector(".preview_inventory_item");
    auto* pack_board = preview.inventory_document->GetElementById("preview_inventory_board");
    ASSERT_TRUE(pack_item && pack_board);
    context->ProcessMouseMove(static_cast<int>(pack_item->GetAbsoluteLeft() + 8),
        static_cast<int>(pack_item->GetAbsoluteTop() + 8), 0);
    context->ProcessMouseButtonDown(0, 0);
    context->ProcessMouseMove(static_cast<int>(pack_board->GetAbsoluteLeft() + 6 * 32 + 8),
        static_cast<int>(pack_board->GetAbsoluteTop() + 2 * 32 + 8), 0);
    context->Update();
    ASSERT_TRUE(pack_board->IsClassSet("inventory_drop_target"));
    auto* footprint = pack_board->QuerySelector(".inventory_drop_outline");
    ASSERT_NE(footprint, nullptr);
    EXPECT_EQ(footprint->GetOffsetWidth(), static_cast<float>(preview.inventory_drag.width * 32));
    EXPECT_EQ(footprint->GetOffsetHeight(), static_cast<float>(preview.inventory_drag.height * 32));
    context->ProcessMouseButtonUp(0, 0);
    ASSERT_TRUE(preview.pending_item);
    EXPECT_EQ(preview.pending_item->action, PreviewItemAction::move_inventory);
    EXPECT_EQ(preview.pending_item->inventory_cell.col, 6);
    EXPECT_EQ(preview.pending_item->inventory_cell.row, 2 + preview.inventory_drag.height - 1);
    const auto destination_cell = preview.pending_item->inventory_cell;
    PreviewInputSample placement;
    ASSERT_TRUE(set_preview_item_action(placement, *preview.pending_item));
    preview.pending_item.reset();
    const auto placed = tick_toolset_preview(preview.session, {&placement, 1}, preview.spatial_rows, preview.locomotion_rows);
    ASSERT_TRUE(placed.action_result.ok());
    EXPECT_FALSE(placed.action_result.equipment_changed);
    preview.inventory_dirty = true;
    sync_play_preview_inventory(preview);
    const auto relocated = std::ranges::find(preview.inventory_rows.inventory, item, &InventoryRow::item);
    ASSERT_NE(relocated, preview.inventory_rows.inventory.end());
    EXPECT_EQ(relocated->column, destination_cell.col);
    EXPECT_EQ(relocated->row, destination_cell.row);

    move_window(-2000, -2000);
    EXPECT_GE(panel->GetAbsoluteLeft(), 3.0f);
    EXPECT_GE(panel->GetAbsoluteTop(), 3.0f);
    panel->RemoveProperty("left");
    panel->RemoveProperty("top");
    context->Update();
    // Fractional display scale must keep all grid cells inside the board. Hover
    // text is laid out on the first event and disappears when leaving the item.
    for (const float scale : {1.0f, 1.25f}) {
        context->SetDensityIndependentPixelRatio(scale);
        context->Update();
        pack_board = preview.inventory_document->GetElementById("preview_inventory_board");
        Rml::ElementList cells;
        pack_board->QuerySelectorAll(cells, ".preview_inventory_cell");
        for (auto* cell : cells) {
            EXPECT_LE(cell->GetAbsoluteLeft() + cell->GetOffsetWidth(), pack_board->GetAbsoluteLeft() + pack_board->GetOffsetWidth() + 1);
            EXPECT_LE(cell->GetAbsoluteTop() + cell->GetOffsetHeight(), pack_board->GetAbsoluteTop() + pack_board->GetOffsetHeight() + 1);
        }
        pack_item = preview.inventory_document->QuerySelector(".preview_inventory_item");
        context->ProcessMouseMove(static_cast<int>(pack_item->GetAbsoluteLeft() + 8),
            static_cast<int>(pack_item->GetAbsoluteTop() + 8), 0);
        context->Update();
        auto* tooltip = preview.inventory_document->GetElementById("preview_inventory_tooltip");
        ASSERT_NE(tooltip, nullptr);
        EXPECT_TRUE(tooltip->IsClassSet("visible"));
        EXPECT_FALSE(tooltip->GetInnerRML().empty());
        EXPECT_GT(tooltip->GetOffsetWidth(), 10.0f);
        EXPECT_GE(tooltip->GetAbsoluteLeft(), 0.0f);
        EXPECT_LE(tooltip->GetAbsoluteLeft() + tooltip->GetOffsetWidth(), 1000.0f);
        capture(scale == 1.0f ? "tmp/preview-inventory-hover.png" : "tmp/preview-inventory-hover-scaled.png");
        context->ProcessMouseMove(0, 0, 0);
        EXPECT_FALSE(tooltip->IsClassSet("visible"));
    }
    context->SetDensityIndependentPixelRatio(1.0f);
    context->Update();
    // Closing mid-drag cancels the native cursor ghost without submitting a drop.
    auto* carried_control = preview.inventory_document->QuerySelector(".preview_inventory_item");
    ASSERT_NE(carried_control, nullptr);
    const int drag_x = static_cast<int>(carried_control->GetAbsoluteLeft() + 8);
    const int drag_y = static_cast<int>(carried_control->GetAbsoluteTop() + 8);
    context->ProcessMouseMove(drag_x, drag_y, 0);
    context->ProcessMouseButtonDown(0, 0);
    context->ProcessMouseMove(drag_x + 8, drag_y + 8, 0);
    ASSERT_TRUE(preview.inventory_dragging);
    RuntimeInputState inventory_input;
    toggle_play_preview_inventory(preview, inventory_input);
    EXPECT_FALSE(preview.inventory_dragging);
    EXPECT_FALSE(preview.pending_item);
    capture("tmp/preview-inventory-drag-cancelled.png");
    ASSERT_TRUE(open_play_preview_inventory(preview, inventory_input));
    context->ProcessMouseButtonUp(0, 0);
    EXPECT_FALSE(preview.pending_item);
    // The same panel must remain usable in smaller windows; its content scrolls.
    for (const auto size : {Rml::Vector2i{800, 600}, Rml::Vector2i{640, 480}}) {
        ASSERT_TRUE(resize_context(ctx, static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y)));
        renderer.on_resize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), context);
        ui.contexts().toolset->SetDimensions(size);
        context->Update();
        EXPECT_GE(panel->GetAbsoluteLeft(), 0.0f);
        EXPECT_GE(panel->GetAbsoluteTop(), 0.0f);
        EXPECT_LE(panel->GetAbsoluteLeft() + panel->GetOffsetWidth(), static_cast<float>(size.x));
        EXPECT_LE(panel->GetAbsoluteTop() + panel->GetOffsetHeight(), static_cast<float>(size.y));
        auto* content = preview.inventory_document->GetElementById("preview_inventory_main");
        ASSERT_NE(content, nullptr);
        EXPECT_LE(content->GetScrollWidth(), content->GetClientWidth() + 1.0f);
        ASSERT_TRUE(renderer.begin_frame());
        ui.contexts().toolset->Update();
        ui.contexts().toolset->Render();
        context->Render();
        renderer.finish_render_pass();
        const auto path = "tmp/preview-inventory-" + std::to_string(size.x) + ".png";
        ASSERT_TRUE(capture_screenshot(ctx, renderer.command_list(), path.c_str()));
        ASSERT_TRUE(renderer.begin_frame());
        renderer.end_frame();
    }
    ASSERT_TRUE(resize_context(ctx, 1000, 760));
    renderer.on_resize(1000, 760, context);
    ui.contexts().toolset->SetDimensions({1000, 760});
    world = toolset_preview_item_world(preview.session);
    std::vector<nw::ObjectHandle> removed{world.ground.begin(), world.ground.end()};
    removed.push_back(started.actor);
    ASSERT_TRUE(scene->replace_area_transient_visuals(removed, authored, area->handle()).ok());
    preview.inventory_visible = false;
    sync_play_preview_inventory(preview);
    stop_toolset_preview(preview.session);
    EXPECT_FALSE(nw::kernel::objects().valid(item));
    EXPECT_TRUE(has_visual(source->handle()));
    EXPECT_EQ(scene->scene()->static_models.size(), baseline_models);
    EXPECT_EQ(scene->scene()->active_object, selection);
    scene->set_area_object_selection_enabled(true);
    capture("tmp/preview-items-restored.png");
}

TEST(ClientGfxResources, PickupApproachRendersAndPublishesInventory)
{
    using namespace nw::gfx;
    using namespace nw::toolset;
    namespace viewer = nw::render::viewer;
    const bool owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (owns_video) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    }
    const auto video = create_scope_exit([&] { if (owns_video) { SDL_QuitSubSystem(SDL_INIT_VIDEO); } });
    auto* core = create_core({.app_name = "pickup-action-preview", .enable_validation = true});
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
    auto* module = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_NE(module, nullptr);
    auto* area = module->get_area(0);
    ASSERT_NE(area, nullptr);
    auto* source = nw::kernel::objects().load<nw::Item>("nw_waxbt001");
    ASSERT_NE(source, nullptr);
    area->items.push_back(source);
    auto& components = nw::kernel::objects().components();
    ASSERT_TRUE(components.set_area(source->handle(), area->handle().id));
    ASSERT_TRUE(components.set_position(source->handle(), module->entry_position));
    ItemIconTextureCache inventory_icons;
    PlayPreviewState preview;
    preview.inventory_icons = &inventory_icons;
    const auto started = start_toolset_preview(preview.session, {
                                                                    .area = area->handle(),
                                                                    .actor = nw::Resource{nw::Resref{"pl_agent_001"}, nw::ResourceType::utc},
                                                                    .spawn_position = module->entry_position + glm::vec3{0, 4, 0},
                                                                });
    ASSERT_TRUE(started.ok()) << started.diagnostic;
    auto world = toolset_preview_item_world(preview.session);
    const auto item = world.ground.back();
    const nw::nav::NavRayProjectionInput ray{
        .origin = module->entry_position + glm::vec3{0, 0, 10}, .displacement = {0, 0, -20}};
    nw::nav::NavRayProjectionResult projected;
    project_toolset_preview_rays(preview.session, {&ray, 1}, {&projected, 1});
    ASSERT_EQ(projected.status, nw::nav::NavStatus::ok);
    ASSERT_TRUE(components.set_position(item, projected.position));
    const std::filesystem::path root{ROLLNW_TEST_SOURCE_DIR};
    RmlNwgfxRenderer renderer;
    ASSERT_TRUE(renderer.initialize(core, ctx));
    renderer.set_generated_textures(&inventory_icons.textures, nullptr);
    viewer::ViewerDevice device{ctx, nw::kernel::resman()};
    ASSERT_TRUE(device.initialize({.shader_roots = {root / "lib/nw/render/shaders"}}));
    auto scene = device.make_session();
    scene->set_preview_scene_load_options({.area_object_editing = true});
    ASSERT_TRUE(scene->load_live_object(area->handle(), "pickup-action-preview"));
    std::vector<nw::ObjectHandle> additions{world.ground.begin(), world.ground.end()};
    additions.push_back(started.actor);
    ASSERT_TRUE(scene->replace_area_transient_visuals(world.authored, additions, world.area).ok());
    const viewer::ViewerViewport viewport{0, 0, 1000, 760};
    scene->set_area_object_selection_enabled(false);
    scene->set_area_day_night_autoplay(false);
    ASSERT_TRUE(scene->fit_to_scene(viewport));
    scene->camera().set_orbit_view(module->entry_position, 8.0f, 180.0f, 40.0f);
    scene->tick(0);
    const auto model = std::ranges::find_if(scene->scene()->static_area_model_info,
        [&](const auto& row) { return row.object == item; });
    ASSERT_NE(model, scene->scene()->static_area_model_info.end());
    const auto index = static_cast<size_t>(model - scene->scene()->static_area_model_info.begin());
    const auto clip = scene->camera().get_projection_matrix() * scene->camera().get_view_matrix()
        * glm::vec4(scene->scene()->static_model_instance(index)->current_bounds.center(), 1.0f);
    const glm::vec2 center{(clip.x / clip.w + 1.0f) * 500.0f, (clip.y / clip.w + 1.0f) * 380.0f};
    std::vector<glm::vec2> pixels;
    for (int y = -12; y <= 12; y += 2) {
        for (int x = -12; x <= 12; x += 2) {
            pixels.push_back(center + glm::vec2{x, y});
        }
    }
    std::vector<viewer::AreaObjectSelection> hits(pixels.size());
    viewer::select_area_pointer_objects(pixels, scene->camera(), viewport, *scene->scene()->area_render_scene,
        *scene->scene(), hits, {.triggers_enabled = false, .encounters_enabled = false});
    const auto hit = std::ranges::find_if(hits, [&](const auto& row) { return row.object == item; });
    if (hit == hits.end()) {
        std::filesystem::create_directories("tmp");
        ASSERT_TRUE(renderer.begin_frame());
        scene->render(renderer.command_list(), viewport);
        ASSERT_TRUE(capture_screenshot(ctx, renderer.command_list(), "tmp/pickup-capture-debug.png"));
        ASSERT_TRUE(renderer.begin_frame());
        renderer.end_frame();
    }
    ASSERT_NE(hit, hits.end()) << "item position: " << projected.position.x << "," << projected.position.y << "," << projected.position.z;
    const auto pixel = pixels[static_cast<size_t>(hit - hits.begin())];
    ASSERT_EQ(scene->update_area_item_hover(pixel, viewport), item);

    Rml::SystemInterface system;
    ClientRmlRuntime ui{root / "tools/client/ui", nw::kernel::resman()};
    ASSERT_TRUE(ui.initialize(system, renderer, std::filesystem::path{ROLLNW_TEST_CLIENT_EXECUTABLE}.parent_path(), {1000, 760}));
    ASSERT_TRUE(ui.create_overlay_contexts({1000, 760}));
    auto* overlay = ui.contexts().palette;
    preview.inventory_document = ui.load_document(*overlay,
        nw::Resource{nw::Resref{"ui/preview_inventory"}, nw::ResourceType::rml});
    ASSERT_NE(preview.inventory_document, nullptr);
    auto* background = ui.contexts().toolset->LoadDocumentFromMemory(
        "<rml><head><style>body {margin:0;width:100%;height:100%;background:#171a1f;}</style></head><body></body></rml>");
    ASSERT_NE(background, nullptr);
    background->Show();
    renderer.on_resize(1000, 760, overlay);
    const char* capture_dir = std::getenv("ROLLNW_CAPTURE_PICKUP");
    if (capture_dir) { std::filesystem::create_directories(capture_dir); }
    size_t transfer_count = 0;
    size_t grab_frames = 0;
    size_t path_requests = 0;
    RuntimeInputState input;
    PreviewFixedStepState fixed;
    for (int frame = 0; frame < 80; ++frame) {
        if (frame >= 6) {
            if (frame == 6) {
                ASSERT_TRUE(set_preview_item_action(input.pending, {scene->update_area_item_hover(pixel, viewport)}));
            }
            // Include the live input lifetime: a pending click must be consumed
            // after ticks so the next rendered frame cannot restart the grab.
            PcDeviceSample physical;
            ASSERT_EQ(acquire_pc_device_sample(input, 0.05,
                          {.keyboard = false, .controller = false, .pointer = true}, physical),
                PreviewStatus::ok);
            PreviewInputSample sample;
            ASSERT_EQ(translate_pc_input_samples({&physical, 1}, {&sample, 1}), PreviewStatus::ok);
            std::array<PreviewInputSample, 6> samples;
            const auto batch = build_preview_tick_samples(fixed, 0.05, sample, samples);
            ASSERT_EQ(batch.status, PreviewStatus::ok);
            ASSERT_GT(batch.tick_count, 0u);
            const auto tick = tick_toolset_preview(preview.session, std::span{samples}.first(batch.tick_count), preview.spatial_rows, preview.locomotion_rows);
            ASSERT_EQ(tick.status, PreviewStatus::ok);
            consume_runtime_input_edges(input);
            path_requests += tick.path_request_count;
            if (tick.action_result.changed) {
                ASSERT_TRUE(tick.action_result.ok());
                ++transfer_count;
                const std::array removed{item};
                ASSERT_TRUE(scene->replace_area_transient_visuals(removed, {}, world.area).ok());
            }
            EXPECT_GT(scene->update_area_object_spatial_states(preview.spatial_rows).render_model_root_count, 0u);
            const auto action = toolset_preview_action_state(preview.session);
            grab_frames += action.phase == nw::gameplay::ActionPhase::grabbing;
            const std::array animations{viewer::AreaCreatureAnimationInput{
                .owner = started.actor,
                .locomotion = preview.locomotion_rows[0] == PreviewActorLocomotion::walking_forward
                    ? viewer::AreaCreatureLocomotion::walking_forward
                    : viewer::AreaCreatureLocomotion::idle,
                .action_time = static_cast<float>(action.phase == nw::gameplay::ActionPhase::recovering
                                       ? action.pickup_ticks - action.elapsed_ticks
                                       : action.elapsed_ticks)
                    / nw::gameplay::action_ticks_per_second,
                .action = action.phase == nw::gameplay::ActionPhase::grabbing || action.phase == nw::gameplay::ActionPhase::recovering
                    ? viewer::AreaCreatureActionAnimation::pickup
                    : viewer::AreaCreatureActionAnimation::none}};
            ASSERT_EQ(scene->update_area_creature_animations(animations).rejected_input_count, 0u);
        }
        (void)scene->update_area_item_hover(frame < 64 ? std::optional{pixel} : std::nullopt, viewport);
        preview.inventory_visible = frame >= 64;
        sync_play_preview_inventory(preview);
        ASSERT_TRUE(renderer.begin_frame());
        ui.contexts().toolset->Update();
        ui.contexts().toolset->Render();
        renderer.finish_render_pass();
        scene->tick(50);
        scene->render(renderer.command_list(), viewport);
        overlay->Update();
        overlay->Render();
        renderer.finish_render_pass();
        if (capture_dir) {
            const auto path = std::filesystem::path{capture_dir} / ("frame-" + std::to_string(1000 + frame) + ".png");
            ASSERT_TRUE(capture_screenshot(ctx, renderer.command_list(), path.string().c_str()));
            ASSERT_TRUE(renderer.begin_frame());
        }
        renderer.end_frame();
    }
    EXPECT_GT(grab_frames, 0u);
    EXPECT_EQ(path_requests, 1u);
    EXPECT_EQ(transfer_count, 1u);
    EXPECT_EQ(toolset_preview_action_state(preview.session).status, nw::gameplay::ActionStatus::applied);
    EXPECT_TRUE(nw::kernel::objects().get<nw::Creature>(started.actor)->inventory().has_item(nw::kernel::objects().get<nw::Item>(item)));
}
