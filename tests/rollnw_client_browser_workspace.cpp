#include "appearance_view.hpp"
#include "browser_view.hpp"
#include "client_preferences.hpp"
#include "creature_body_part_editor.hpp"
#include "creature_workbench_view.hpp"
#include "dialog_view.hpp"
#include "inventory_workbench_view.hpp"
#include "loading_view.hpp"
#include "object_edits.hpp"
#include "object_workbench_view.hpp"
#include "script_commands.hpp"
#include "shell_view.hpp"
#include "smalls_rmlui.hpp"
#include "workspace_view.hpp"

#include <nw/kernel/Rules.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Door.hpp>
#include <nw/objects/Encounter.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/objects/Placeable.hpp>
#include <nw/objects/Sound.hpp>
#include <nw/objects/Store.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/scope_exit.hpp>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <tuple>

using namespace nw::toolset;

namespace {

class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override { }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override { }
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override { }
    void EnableScissorRegion(bool) override { }
    void SetScissorRegion(Rml::Rectanglei) override { }
};

class KernelServiceScope {
public:
    KernelServiceScope() { nw::kernel::services().start(); }
    ~KernelServiceScope()
    {
        nw::kernel::services().shutdown();
        nw::kernel::services().start();
    }
};

} // namespace

class ClientBrowserWorkspace : public ::testing::Test {
protected:
    void SetUp() override
    {
        Rml::SetRenderInterface(&renderer);
        initialized = Rml::Initialise();
        ASSERT_TRUE(initialized);
        std::ifstream input{source / "tools/client/assets/fonts/inter/Inter-Medium.ttf", std::ios::binary};
        font = {std::istreambuf_iterator<char>{input}, {}};
        ASSERT_FALSE(font.empty());
        ASSERT_TRUE(Rml::LoadFontFace({font.data(), font.size()}, "RollnwSans", Rml::Style::FontStyle::Normal,
            static_cast<Rml::Style::FontWeight>(500)));
        ASSERT_TRUE(Rml::LoadFontFace({font.data(), font.size()}, "RollnwMono", Rml::Style::FontStyle::Normal,
            Rml::Style::FontWeight::Normal));
        context = Rml::CreateContext("browser-workspace", {1200, 700});
        ASSERT_NE(context, nullptr);
        document = context->LoadDocument((source / "tools/client/ui/panel.rml").string());
        ASSERT_NE(document, nullptr);
        document->GetElementById("panel")->SetProperty("display", "flex");
        document->GetElementById("panel")->SetProperty("width", "320px");
        document->Show();
        context->Update();
    }

    void TearDown() override
    {
        if (context) { Rml::RemoveContext("browser-workspace"); }
        if (initialized) { Rml::Shutdown(); }
        Rml::SetRenderInterface(nullptr);
    }

    std::filesystem::path source{ROLLNW_TEST_SOURCE_DIR};
    NullRenderInterface renderer;
    std::vector<Rml::byte> font;
    bool initialized = false;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
};

TEST_F(ClientBrowserWorkspace, RealProjectFiltersAndHomeWindowsKeepTheirExistingContracts)
{
    KernelServiceScope services;
    const auto root = std::filesystem::path{"tmp/client_browser_workspace"}
        / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(root);
    const auto imported = import_module_project("test_data/user/modules/DockerDemo.mod", root, {ProjectImportFormat::json});
    ASSERT_TRUE(imported.ok) << imported.message;
    RmlSmallsBridge bridge;
    WorkspaceState workspace;
    ShellController shell;
    ToolsetBackend backend;
    backend.bind(&bridge, &shell, &workspace);
    const auto unbind = create_scope_exit([] { script_command_host().bind(nullptr, nullptr); });
    const auto opened = backend.open_project(root.string());
    ASSERT_TRUE(opened.ok()) << opened.message;
    shell.set_showing_project_tree(true);
    BrowserViewState browser;
    browser.selected_recent_index = std::numeric_limits<int>::max();
    refresh_browser_view(document, browser, backend, shell, true, false);
    ASSERT_FALSE(browser.project_rows.empty());
    EXPECT_EQ(browser.selected_recent_index, -1);
    auto* list = document->GetElementById("recent_list");
    ASSERT_NE(list, nullptr);
    auto* first = list->GetChild(0);
    EXPECT_FALSE(render_project_tree_window(document, browser, false));
    EXPECT_EQ(list->GetChild(0), first);
    const auto container = std::find_if(browser.project_rows.begin(), browser.project_rows.end(),
        [](const ProjectTreeRow& row) { return row.node.is_container(); });
    ASSERT_NE(container, browser.project_rows.end());
    browser.collapsed_project_nodes.insert(container->node.id);
    auto* search = rmlui_dynamic_cast<Rml::ElementFormControl*>(document->GetElementById("recent_search"));
    ASSERT_NE(search, nullptr);
    search->SetValue("start");
    refresh_browser_view(document, browser, backend, shell, true, true);
    ASSERT_FALSE(browser.project_rows.empty());
    EXPECT_EQ(browser.last_recent_query, "start");
    EXPECT_TRUE(std::none_of(browser.project_rows.begin(), browser.project_rows.end(),
        [](const ProjectTreeRow& row) { return row.collapsed; }));
    EXPECT_EQ(document->GetElementById("title_project")->GetInnerRML(), "Choose Preview Creature");

    refresh_home_area_catalog(browser, backend, false);
    ASSERT_FALSE(browser.home_areas.empty());
    const auto generation = browser.home_area_generation;
    LoadingViewState loading;
    std::string markup;
    EXPECT_TRUE(append_workspace_home_start_markup(markup, browser, backend, loading, "test version"));
    document->GetElementById("workspace_content")->SetInnerRML(markup + "</div>");
    context->Update();
    EXPECT_TRUE(sync_home_area_window(document, browser, true, true));
    auto* home_list = document->GetElementById("home_area_list");
    ASSERT_NE(home_list, nullptr);
    auto* home_first = home_list->GetChild(0);
    EXPECT_FALSE(sync_home_area_window(document, browser, true, false));
    EXPECT_EQ(home_list->GetChild(0), home_first);
    EXPECT_GE(browser.rendered_home_area_columns, 1);
    EXPECT_LE(browser.rendered_home_area_columns, 4);
    EXPECT_FALSE(sync_home_area_window(document, browser, false, true));
    browser.home_area_query = "missing area query";
    refresh_home_area_catalog(browser, backend, true);
    EXPECT_EQ(browser.home_area_generation, generation);
    EXPECT_EQ(browser.home_area_query, "missing area query");
    EXPECT_TRUE(browser.home_areas.empty());
    EXPECT_TRUE(sync_home_area_window(document, browser, true, true));
    EXPECT_NE(home_list->GetInnerRML().find("No matching areas."), std::string::npos);
    ToolsetBackend empty_backend;
    refresh_home_area_catalog(browser, empty_backend, false);
    EXPECT_TRUE(browser.home_area_query.empty());

    workspace.open_area_tab("shared/areas/start.caf.json", "Start");
    document->SetInnerRML("<div id='workspace_viewer_viewport' style='position:absolute;left:-20px;top:-12px;width:200px;height:140px;'></div>");
    context->Update();
    auto request = active_workspace_viewer_viewport_request(document, workspace, backend, 160, 120);
    ASSERT_TRUE(request);
    EXPECT_EQ(request->resource_path, "shared/areas/start.caf.json");
    EXPECT_EQ(request->kind, WorkspaceViewerViewportKind::area);
    EXPECT_EQ(request->rect.x, 0);
    EXPECT_EQ(request->rect.y, 0);
    EXPECT_EQ(request->rect.width, 160u);
    EXPECT_EQ(request->rect.height, 120u);
    EXPECT_FALSE(active_workspace_viewer_viewport_request(document, workspace, backend, 0, 120));
    EXPECT_FALSE(active_workspace_viewer_viewport_request(document, workspace, empty_backend, 160, 120));
    document->GetElementById("workspace_viewer_viewport")->SetProperty("height", "7px");
    context->Update();
    EXPECT_FALSE(active_workspace_viewer_viewport_request(document, workspace, backend, 160, 120));
}

TEST_F(ClientBrowserWorkspace, TabSynchronizationCloseAndReorderPreserveLockedPrefix)
{
    WorkspaceState workspace;
    workspace.ensure_default_tabs();
    workspace.open_tab("first", "First", WorkspaceTabKind::generic);
    workspace.open_tab("second", "Second", WorkspaceTabKind::generic);
    WorkspaceViewState view;
    refresh_workspace_tabs(document, view, workspace);
    context->Update();
    auto* track = document->GetElementById("workspace_tab_track");
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->GetNumChildren(), workspace.tabs().size());
    EXPECT_TRUE(track->GetChild(0)->IsClassSet("locked"));
    EXPECT_TRUE(track->GetChild(1)->IsClassSet("locked"));
    EXPECT_EQ(workspace_tab_target_index_at_point(document, {-100, 0}, workspace.tabs(), "second", 3), 2u);
    EXPECT_EQ(workspace_tab_target_index_at_point(nullptr, {0, 0}, workspace.tabs(), "second", 3), 3u);
    view.workspace_tab_dragging = true;
    view.workspace_tab_drag_id = "second";
    EXPECT_TRUE(sync_workspace_tab_elements(document, view, workspace));
    EXPECT_TRUE(track->GetChild(3)->IsClassSet("dragging"));
    ASSERT_TRUE(workspace.move_tab("second", 2));
    EXPECT_FALSE(sync_workspace_tab_elements(document, view, workspace));
    refresh_workspace_tabs(document, view, workspace);
    context->Update();
    EXPECT_TRUE(sync_workspace_tab_elements(document, view, workspace));
    ASSERT_TRUE(workspace.close_tab("second"));
    EXPECT_TRUE(remove_workspace_tab_element(document, view, workspace, "second"));
    track = document->GetElementById("workspace_tab_track");
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->GetNumChildren(), workspace.tabs().size());
    clear_workspace_tab_drag(view);
    EXPECT_FALSE(view.workspace_tab_dragging);
    EXPECT_TRUE(view.workspace_tab_drag_id.empty());
    float scroll = -100;
    apply_tab_scroll(document, kWorkspaceTabScrollStrip, scroll);
    EXPECT_EQ(scroll, 0);
    EXPECT_TRUE(document->GetElementById("workspace_tabs_previous")->IsClassSet("disabled"));
    scroll = 100;
    apply_tab_scroll(nullptr, kWorkspaceTabScrollStrip, scroll);
    EXPECT_EQ(scroll, 0);
}

TEST_F(ClientBrowserWorkspace, HiddenRowsDoNotClaimThePointer)
{
    auto* list = document->GetElementById("recent_list");
    ASSERT_NE(list, nullptr);
    list->SetInnerRML(area_rows_markup(std::array{LoadedAreaEntry{.name = "Start", .resref = "start"}}));
    context->Update();
    auto* item = list->GetChild(0)->GetChild(0);
    ASSERT_NE(item, nullptr);
    const Rml::Vector2f point{item->GetAbsoluteLeft() + 4, item->GetAbsoluteTop() + 4};
    EXPECT_EQ(find_recent_item_at(list, point), item);
    list->SetProperty("display", "none");
    context->Update();
    EXPECT_EQ(recent_item_at_point(document, point), nullptr);
    EXPECT_EQ(find_recent_item_at(list, point), nullptr);
}

TEST_F(ClientBrowserWorkspace, RebuildRetainsTheSelectedIndexAndItsHighlight)
{
    BrowserViewState browser;
    // Synthetic flat rows isolate the existing index/highlight state transition.
    browser.project_rows.push_back({ProjectTreeNode{.id = "selected", .label = "Selected"}});
    ASSERT_TRUE(render_project_tree_window(document, browser, true));
    set_recent_selected(document, browser, 0);
    auto* list = document->GetElementById("recent_list");
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(list->GetChild(0)->GetChild(0)->IsClassSet("selected"));
    ASSERT_TRUE(render_project_tree_window(document, browser, true));
    EXPECT_EQ(browser.selected_recent_index, 0);
    EXPECT_TRUE(list->GetChild(0)->GetChild(0)->IsClassSet("selected"));
}

class ClientShellView : public ClientBrowserWorkspace { };

TEST_F(ClientShellView, DockClampsAndPreviewLayoutRestoreTheEditor)
{
    ShellController shell;
    ShellViewState view;
    shell.set_showing_areas(true);
    shell.set_output_panel_visible(true);
    apply_left_dock_width(document, shell, nullptr, 100000, {});
    apply_bottom_dock_height(document, shell, nullptr, 100000, {});
    EXPECT_EQ(shell.docks.pane(DockRegion::left).size_px, 640);
    EXPECT_EQ(shell.docks.pane(DockRegion::bottom).size_px, 624);
    auto* workspace = document->GetElementById("workspace_shell");
    ASSERT_NE(workspace, nullptr);
    EXPECT_EQ(workspace->GetProperty("left")->ToString(), "640px");
    EXPECT_EQ(workspace->GetProperty("bottom")->ToString(), "624px");
    apply_shell_layout(document, shell, {true, true});
    EXPECT_TRUE(document->IsClassSet("play_preview_placement_pending"));
    EXPECT_EQ(workspace->GetProperty("left")->ToString(), "0px");
    EXPECT_EQ(workspace->GetProperty("bottom")->ToString(), "0px");
    apply_shell_layout(document, shell, {});
    EXPECT_FALSE(document->IsClassSet("play_preview_active"));
    EXPECT_EQ(workspace->GetProperty("left")->ToString(), "640px");
    apply_left_dock_width(document, shell, nullptr, 1, {});
    apply_bottom_dock_height(document, shell, nullptr, 1, {});
    EXPECT_EQ(shell.docks.pane(DockRegion::left).size_px, 260);
    EXPECT_EQ(shell.docks.pane(DockRegion::bottom).size_px, 160);
    apply_left_dock_width(document, shell, nullptr, 0, {});
    apply_bottom_dock_height(nullptr, shell, nullptr, 500, {});
    EXPECT_EQ(shell.docks.pane(DockRegion::bottom).size_px, 160);

    // Captured scalar state is exercised without acquiring a desktop capture.
    view.bottom_dock_resizing = true;
    view.bottom_dock_resize_start_y = 100;
    view.bottom_dock_resize_start_height_px = 240;
    SDL_MouseMotionEvent motion{};
    motion.y = 110;
    EXPECT_TRUE(update_bottom_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_EQ(shell.docks.pane(DockRegion::bottom).size_px, 230);
    EXPECT_TRUE(end_bottom_dock_resize(view));
    EXPECT_FALSE(end_bottom_dock_resize(view));
    view.left_dock_resizing = true;
    view.left_dock_resize_start_x = 100;
    view.left_dock_resize_start_width_px = 360;
    motion.x = 80;
    EXPECT_TRUE(update_left_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_EQ(shell.docks.pane(DockRegion::left).size_px, 340);
    EXPECT_TRUE(end_left_dock_resize(view));
    EXPECT_FALSE(end_left_dock_resize(view));
}

TEST_F(ClientShellView, CapturedResizeRejectsUnsafeDeltasBeforeIntegerConversion)
{
    ShellController shell;
    ShellViewState view;
    shell.set_output_panel_visible(true);
    view.left_dock_resizing = true;
    view.left_dock_resize_start_x = -2000000000.0f;
    view.left_dock_resize_start_width_px = 360;
    SDL_MouseMotionEvent motion{};
    motion.x = 2000000000.0f;
    EXPECT_TRUE(update_left_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_EQ(shell.docks.pane(DockRegion::left).size_px, 640);
    view.bottom_dock_resizing = true;
    view.bottom_dock_resize_start_y = 2000000000.0f;
    view.bottom_dock_resize_start_height_px = 240;
    motion.y = -2000000000.0f;
    EXPECT_TRUE(update_bottom_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_EQ(shell.docks.pane(DockRegion::bottom).size_px, 624);
    view.left_dock_resize_start_x = 0;
    motion.x = 2147483520.0f;
    EXPECT_TRUE(update_left_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_EQ(shell.docks.pane(DockRegion::left).size_px, 640);
    view.left_dock_resize_start_x = 100;
    motion.x = 99.5f;
    EXPECT_TRUE(update_left_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_EQ(shell.docks.pane(DockRegion::left).size_px, 359);
    motion.x = -2000000000.0f;
    EXPECT_TRUE(update_left_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_EQ(shell.docks.pane(DockRegion::left).size_px, 359);
    const auto before = shell.docks.pane(DockRegion::left).size_px;
    motion.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(update_left_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_FALSE(view.left_dock_resizing);
    EXPECT_EQ(shell.docks.pane(DockRegion::left).size_px, before);
    view.left_dock_resizing = true;
    view.left_dock_resize_start_x = std::numeric_limits<float>::infinity();
    motion.x = 50;
    EXPECT_FALSE(update_left_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_FALSE(view.left_dock_resizing);
    motion.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(update_bottom_dock_resize(document, view, shell, nullptr, motion, {}));
    EXPECT_FALSE(view.bottom_dock_resizing);
    EXPECT_EQ(shell.docks.pane(DockRegion::bottom).size_px, 624);
}

TEST_F(ClientShellView, OutputPreservesAppendOffsetsAndDefersHiddenScroll)
{
    ShellController shell;
    ShellViewState view;
    shell.append_output("info", "alpha\néclair");
    shell.append_output("error", "other <&>");
    refresh_output_view(document, view, shell);
    EXPECT_EQ(view.output_selection.text, "alpha\néclair\nother <&>");
    view.output_selection.anchor = 0;
    view.output_selection.focus = 5;
    shell.append_output("info", "next");
    refresh_output_view(document, view, shell);
    EXPECT_EQ(view.output_selection.range(), (std::pair<size_t, size_t>{0, 5}));
    auto* lines = document->GetElementById("output_list_lines");
    ASSERT_NE(lines, nullptr);
    EXPECT_NE(lines->GetInnerRML().find("output_text_selection"), std::string::npos);
    EXPECT_NE(lines->GetInnerRML().find("&lt;&amp;&gt;"), std::string::npos);
    EXPECT_EQ(view.output_scroll_after_layout, OutputScrollAfterLayout::follow_tail);
    EXPECT_FALSE(apply_output_scroll_after_layout(document, view, shell));
    EXPECT_EQ(view.output_scroll_after_layout, OutputScrollAfterLayout::follow_tail);
    shell.set_output_panel_visible(true);
    refresh_bottom_dock_view(document, shell, {});
    context->Update();
    (void)apply_output_scroll_after_layout(document, view, shell);
    EXPECT_EQ(view.output_scroll_after_layout, OutputScrollAfterLayout::none);
    auto* filter = rmlui_dynamic_cast<Rml::ElementFormControl*>(document->GetElementById("output_filter"));
    ASSERT_NE(filter, nullptr);
    filter->SetValue("OTHER");
    refresh_output_view(document, view, shell);
    EXPECT_EQ(view.output_selection.text, "other <&>");
    EXPECT_FALSE(view.output_selection.active());
    view.output_selection.anchor = 1000;
    view.output_selection.focus = 1000;
    refresh_output_view(document, view, shell);
    EXPECT_EQ(view.output_selection.anchor, view.output_selection.text.size());
}

TEST_F(ClientShellView, TerminalCompletionPreservesUnicodeArgumentsAndRejectsSelection)
{
    ShellController shell;
    shell.set_terminal_visible(true);
    refresh_bottom_dock_view(document, shell, {});
    context->Update();
    WorkspaceState workspace;
    RmlSmallsBridge bridge;
    ToolsetBackend backend;
    backend.bind(&bridge, &shell, &workspace);
    const auto unbind = create_scope_exit([] { script_command_host().bind(nullptr, nullptr); });
    auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("terminal_input"));
    ASSERT_NE(input, nullptr);
    input->SetValue("command.und élève");
    context->Update();
    input->SetSelectionRange(11, 11);
    EXPECT_TRUE(complete_terminal_command(document, shell, backend));
    EXPECT_EQ(input->GetValue(), "command.undo élève");
    int start = 0, end = 0;
    Rml::String selected;
    input->GetSelection(&start, &end, &selected);
    EXPECT_EQ(start, 12);
    EXPECT_EQ(end, 12);
    input->SetSelectionRange(0, 7);
    EXPECT_FALSE(complete_terminal_command(document, shell, backend));
    EXPECT_EQ(input->GetValue(), "command.undo élève");
    EXPECT_FALSE(complete_terminal_command(nullptr, shell, backend));
    shell.append_terminal("info", "a <&> b");
    refresh_terminal_view(document, shell);
    EXPECT_NE(document->GetElementById("terminal_output_lines")->GetInnerRML().find("&lt;&amp;&gt;"), std::string::npos);
}

TEST_F(ClientShellView, OutputHitOffsetsAreUtf8BytesAndBadMetadataIsRejected)
{
    ShellController shell;
    ShellViewState view;
    shell.set_output_panel_visible(true);
    shell.append_output("info", "éclair");
    refresh_bottom_dock_view(document, shell, {});
    refresh_output_view(document, view, shell);
    context->Update();
    auto* lines = document->GetElementById("output_list_lines");
    ASSERT_NE(lines, nullptr);
    auto* row = lines->GetChild(0);
    ASSERT_NE(row, nullptr);
    const auto origin = row->GetAbsoluteOffset(Rml::BoxArea::Content);
    const float glyph_width = static_cast<float>(Rml::ElementUtilities::GetStringWidth(row, "é"));
    ASSERT_GT(glyph_width, 0);
    Rml::Vector2f point{origin.x + glyph_width * 0.75f, origin.y + 4};
    EXPECT_EQ(output_text_offset_at_point(context, document, view, point), 2u);
    point.x = origin.x + row->GetClientWidth() - 4;
    EXPECT_EQ(output_text_offset_at_point(context, document, view, point), view.output_selection.text.size());
    row->SetAttribute("data-output-start", "bad");
    EXPECT_FALSE(output_text_offset_at_point(context, document, view, point));
    row->SetAttribute("data-output-start", "1000");
    EXPECT_FALSE(output_text_offset_at_point(context, document, view, point));
    row->SetAttribute("data-output-start", "0");
    row->SetAttribute("data-output-length", "1000");
    EXPECT_EQ(output_text_offset_at_point(context, document, view, point), view.output_selection.text.size());
    document->GetElementById("output_list")->SetProperty("display", "none");
    context->Update();
    EXPECT_FALSE(output_text_offset_at_point(context, document, view, point));
}

TEST(ClientPreferenceHistory, RecentHistoryCanonicalizesDeduplicatesAndPersists)
{
    const auto root = std::filesystem::path{"tmp/client_preferences"}
        / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "one");
    std::filesystem::create_directories(root / "two");
    const auto prefs = root / "preferences.json";
    ShellController shell;
    std::vector<RecentProjectEntry> rows;
    remember_recent_project(prefs, shell.docks, rows, {});
    EXPECT_FALSE(std::filesystem::exists(prefs));
    remember_recent_project(prefs, shell.docks, rows, root / "one");
    remember_recent_project(prefs, shell.docks, rows, root / "two");
    remember_recent_project(prefs, shell.docks, rows, root / "one/../two");
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].path, std::filesystem::weakly_canonical(root / "two").string());
    EXPECT_EQ(rows[1].path, std::filesystem::weakly_canonical(root / "one").string());
    DockLayout restored;
    std::vector<RecentProjectEntry> loaded;
    load_ui_preferences(prefs, restored, loaded);
    ASSERT_EQ(loaded.size(), 2);
    EXPECT_EQ(loaded[0].path, rows[0].path);
    EXPECT_EQ(restored.pane(DockRegion::left).size_px, shell.docks.pane(DockRegion::left).size_px);
    for (size_t i = 0; i < kMaxRecentProjects + 2; ++i) {
        remember_recent_project(prefs, shell.docks, rows, root / std::to_string(i));
    }
    EXPECT_EQ(rows.size(), kMaxRecentProjects);
    EXPECT_EQ(rows.front().name, std::to_string(kMaxRecentProjects + 1));
}

TEST(ClientShellInput, GraveToggleSuppressesOnlyTheNextTextEvent)
{
    ShellViewState view;
    SDL_Event event{};
    view.suppress_terminal_toggle_text_input = true;
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = "`";
    EXPECT_TRUE(consume_terminal_toggle_text_input(view, event));
    EXPECT_FALSE(consume_terminal_toggle_text_input(view, event));
    view.suppress_terminal_toggle_text_input = true;
    event.text.text = "letter";
    EXPECT_FALSE(consume_terminal_toggle_text_input(view, event));
    EXPECT_FALSE(view.suppress_terminal_toggle_text_input);
    view.suppress_terminal_toggle_text_input = true;
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_GRAVE;
    EXPECT_FALSE(consume_terminal_toggle_text_input(view, event));
    EXPECT_TRUE(view.suppress_terminal_toggle_text_input);
    event.key.key = SDLK_A;
    EXPECT_FALSE(consume_terminal_toggle_text_input(view, event));
    EXPECT_FALSE(view.suppress_terminal_toggle_text_input);
}

class ClientObjectWorkbench : public ClientBrowserWorkspace {
protected:
    void SetUp() override
    {
        ClientBrowserWorkspace::SetUp();
        auto& runtime = nw::kernel::runtime();
        runtime.add_module_path("stdlib/core");
        runtime.add_module_path("stdlib/nwn1");
        runtime.add_module_path("stdlib/toolset");
        ASSERT_NE(runtime.load_module("core.creature"), nullptr);
        ASSERT_NE(runtime.load_module("core.item"), nullptr);
        ASSERT_NE(runtime.load_module("nwn1.propsets"), nullptr);
        ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);
        workspace.open_tab("first", "First", WorkspaceTabKind::preview);
        backend.bind(&bridge, &shell, &workspace);
        command.source = CommandSource::widget;
        command.workspace = &workspace;
        command.active_tab_id = workspace.active_tab_id();
    }
    void TearDown() override
    {
        script_command_host().bind(nullptr, nullptr);
        smalls_rmlui_host().clear_active_object();
        ClientBrowserWorkspace::TearDown();
        nw::kernel::services().shutdown();
        nw::kernel::services().start();
    }
    class Listener final : public Rml::EventListener {
    public:
        explicit Listener(ClientObjectWorkbench& fixture)
            : owner{fixture}
        {
        }
        void ProcessEvent(Rml::Event& event) override
        {
            process_object_workbench_change(event, owner.view, owner.workspace,
                owner.backend, owner.shell, owner.command);
        }
        ClientObjectWorkbench& owner;
    };
    void activate(nw::ObjectHandle object)
    {
        smalls_rmlui_host().publish_active_object(object);
        view.active_object_tab_id = workspace.active_tab_id();
        command.active_tab_id = workspace.active_tab_id();
        rebuild_object_workbench_snapshots(view, object);
    }
    ObjectWorkbenchViewState view;
    WorkspaceState workspace;
    RmlSmallsBridge bridge;
    ShellController shell;
    ToolsetBackend backend;
    CommandContext command;
};

TEST_F(ClientObjectWorkbench, NativeSurfaceCaptureOwnsItsEnumAcrossSelectorDomReplacement)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.creature_view.creature_spell_filter_field = CreatureSpellFilterField::class_;
    view.appearance_view.color_editor_object = actor->handle();
    view.appearance_view.color_editor_channel = 0;
    view.appearance_view.appearance_selector_open = true;
    view.appearance_view.sound_resource_selector_open = true;
    view.details_rendered = true;
    document->SetInnerRML("<button class='object_workbench_tab' data-surface='inventory'><span id='target'>Inventory</span></button>");
    auto click = capture_object_workbench_surface_click(document->GetElementById("target"));
    ASSERT_TRUE(click);
    ASSERT_EQ(click->surface, ObjectWorkbenchSurface::inventory);
    document->SetInnerRML("<div class='smalls_selector active'><button class='smalls_selector_close'>Close</button></div>");
    class CloseListener final : public Rml::EventListener {
    public:
        CloseListener(ObjectWorkbenchViewState& view_state, Rml::ElementDocument& source_document)
            : state{view_state}
            , document{source_document}
        {
        }
        void ProcessEvent(Rml::Event&) override
        {
            ++calls;
            EXPECT_EQ(state.creature_view.creature_spell_filter_field, CreatureSpellFilterField::none);
            EXPECT_EQ(state.appearance_view.color_editor_channel, -1);
            EXPECT_TRUE(state.appearance_view.appearance_selector_open);
            EXPECT_TRUE(state.appearance_view.sound_resource_selector_open);
            EXPECT_FALSE(click->pending);
            document.SetInnerRML("<div id='replacement'>Replacement during close</div>");
        }
        ObjectWorkbenchViewState& state;
        Rml::ElementDocument& document;
        ObjectWorkbenchSurfaceClick* click = nullptr;
        int calls = 0;
    } listener{view, *document};
    listener.click = &*click;
    context->AddEventListener("click", &listener);
    const auto remove_listener = create_scope_exit([&] { context->RemoveEventListener("click", &listener); });
    ASSERT_TRUE(apply_object_workbench_surface_click(*click, view, document, backend));
    EXPECT_EQ(listener.calls, 1);
    EXPECT_NE(document->GetElementById("replacement"), nullptr);
    EXPECT_EQ(view.object_workbench_surface, ObjectWorkbenchSurface::inventory);
    EXPECT_FALSE(view.appearance_view.appearance_selector_open);
    EXPECT_FALSE(view.appearance_view.sound_resource_selector_open);
    EXPECT_FALSE(view.details_rendered);
    EXPECT_FALSE(apply_object_workbench_surface_click(*click, view, document, backend));
    EXPECT_EQ(listener.calls, 1);
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientObjectWorkbench, NativeSurfaceRejectsUnsupportedAndUnknownNamesWithExistingCleanup)
{
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    nw::kernel::runtime().init_object_propsets(sound->handle());
    activate(sound->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::sounds;
    for (const char* name : {"classes", "inventory", "unknown", "", "Sounds"}) {
        SCOPED_TRACE(name);
        view.appearance_view.appearance_selector_open = true;
        view.appearance_view.sound_resource_selector_open = true;
        view.details_rendered = true;
        document->SetInnerRML(std::string{"<button id='target' class='object_workbench_tab' data-surface='"} + name + "'>Surface</button>");
        auto click = capture_object_workbench_surface_click(document->GetElementById("target"));
        ASSERT_TRUE(click);
        EXPECT_TRUE(apply_object_workbench_surface_click(*click, view, document, backend));
        EXPECT_EQ(view.object_workbench_surface, ObjectWorkbenchSurface::sounds);
        EXPECT_FALSE(view.appearance_view.appearance_selector_open);
        EXPECT_FALSE(view.appearance_view.sound_resource_selector_open);
        EXPECT_FALSE(view.details_rendered);
    }
    document->SetInnerRML("<button id='other'>Other</button>");
    EXPECT_FALSE(capture_object_workbench_surface_click(document->GetElementById("other")));
    auto* module = nw::kernel::objects().make<nw::Module>();
    ASSERT_NE(module, nullptr);
    activate(module->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::variables;
    ObjectWorkbenchSurfaceClick haks{.surface = ObjectWorkbenchSurface::haks};
    ASSERT_TRUE(backend.current_project_dir().empty());
    EXPECT_TRUE(apply_object_workbench_surface_click(haks, view, document, backend));
    EXPECT_EQ(view.object_workbench_surface, ObjectWorkbenchSurface::variables);
    ObjectWorkbenchSurfaceClick malformed{.surface = static_cast<ObjectWorkbenchSurface>(255)};
    EXPECT_TRUE(apply_object_workbench_surface_click(malformed, view, document, backend));
    EXPECT_EQ(view.object_workbench_surface, ObjectWorkbenchSurface::variables);
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientObjectWorkbench, NativeSurfaceAppliesTheExistingLiveTypeCapabilities)
{
    // Actual live objects give the supported singleton surfaces their own owners.
    auto* item = nw::kernel::objects().make<nw::Item>();
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    auto* encounter = nw::kernel::objects().make<nw::Encounter>();
    auto* store = nw::kernel::objects().make<nw::Store>();
    ASSERT_NE(item, nullptr);
    ASSERT_NE(sound, nullptr);
    ASSERT_NE(encounter, nullptr);
    ASSERT_NE(store, nullptr);
    for (const auto& [object, name, expected] : {
             std::tuple{item->handle(), "item-properties", ObjectWorkbenchSurface::item_properties},
             std::tuple{item->handle(), "inventory", ObjectWorkbenchSurface::inventory},
             std::tuple{sound->handle(), "sounds", ObjectWorkbenchSurface::sounds},
             std::tuple{encounter->handle(), "spawns", ObjectWorkbenchSurface::spawns},
             std::tuple{store->handle(), "store-inventory", ObjectWorkbenchSurface::store_inventory}}) {
        SCOPED_TRACE(name);
        nw::kernel::runtime().init_object_propsets(object);
        activate(object);
        document->SetInnerRML(std::string{"<button id='target' class='object_workbench_tab' data-surface='"} + name + "'>Surface</button>");
        auto click = capture_object_workbench_surface_click(document->GetElementById("target"));
        ASSERT_TRUE(click);
        EXPECT_TRUE(apply_object_workbench_surface_click(*click, view, document, backend));
        EXPECT_EQ(view.object_workbench_surface, expected);
        EXPECT_EQ(workspace.undo_count(), 0);
    }
}

TEST_F(ClientObjectWorkbench, NativeVariableClickOwnsArgumentsAndCommitsOneUndo)
{
    auto* module = nw::kernel::objects().make<nw::Module>();
    ASSERT_NE(module, nullptr);
    module->locals.set_int("label", 5);
    activate(module->handle());
    document->SetInnerRML("<button class='object_variable_remove' data-name='label' data-type='1'><span id='target'>Remove</span></button>");
    auto click = capture_object_workbench_command_click(document->GetElementById("target"), view, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, ObjectWorkbenchCommandKind::variable_remove);
    EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
    document->SetInnerRML("<button class='object_variable_remove' data-name='replacement' data-type='1'>New</button>");
    EXPECT_TRUE(execute_object_workbench_command_click(*click, view, workspace, backend, shell, command));
    EXPECT_EQ(module->locals.get_int("label"), 0);
    EXPECT_EQ(workspace.undo_count(), 1);
    const auto logs = shell.output_lines.size();
    EXPECT_FALSE(execute_object_workbench_command_click(*click, view, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_EQ(shell.output_lines.size(), logs);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(module->locals.get_int("label"), 5);
}

TEST_F(ClientObjectWorkbench, NativeDetailsClickUsesLiveMetadataAndRejectsReplacementOwner)
{
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    nw::kernel::runtime().init_object_propsets(sound->handle());
    activate(sound->handle());
    ASSERT_TRUE(active_object_details_matches_tab(view, workspace));
    const auto row = std::ranges::find(view.object_details.rows, ObjectDetailsEditorKind::boolean, &ObjectDetailsRow::editor);
    ASSERT_NE(row, view.object_details.rows.end());
    const auto index = static_cast<uint32_t>(std::distance(view.object_details.rows.begin(), row));
    const auto before = row->edit_value;
    document->SetInnerRML("<span id='target' class='object_details_boolean' data-row='" + std::to_string(index)
        + "' data-current='" + std::to_string(before) + "'>Toggle</span>");
    auto click = capture_object_workbench_command_click(document->GetElementById("target"), view, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, ObjectWorkbenchCommandKind::boolean);
    EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::after_native);
    EXPECT_TRUE(execute_object_workbench_command_click(*click, view, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 1);
    ObjectDetailsSnapshot current;
    build_object_details(nw::kernel::runtime(), sound->handle(), current);
    ASSERT_LT(index, current.rows.size());
    EXPECT_EQ(current.rows[index].edit_value, 1 - before);
    EXPECT_FALSE(execute_object_workbench_command_click(*click, view, workspace, backend, shell, command));
    ASSERT_TRUE(workspace.undo(command).ok());
    rebuild_object_workbench_snapshots(view, sound->handle());
    click = capture_object_workbench_command_click(document->GetElementById("target"), view, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    auto* replacement = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(replacement, nullptr);
    nw::kernel::runtime().init_object_propsets(replacement->handle());
    activate(replacement->handle());
    const auto logs = shell.output_lines.size();
    EXPECT_FALSE(execute_object_workbench_command_click(*click, view, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_EQ(shell.output_lines.size(), logs);
    build_object_details(nw::kernel::runtime(), sound->handle(), current);
    ASSERT_LT(index, current.rows.size());
    EXPECT_EQ(current.rows[index].edit_value, before);
}

TEST_F(ClientObjectWorkbench, NativeIntegerAndDoorClicksPreservePhasesAndUndo)
{
    auto* door = nw::kernel::objects().make<nw::Door>();
    ASSERT_NE(door, nullptr);
    for (const auto [object, editor] : {std::pair{door->handle(), ObjectDetailsEditorKind::integer},
             std::pair{door->handle(), ObjectDetailsEditorKind::door_state}}) {
        SCOPED_TRACE(object.to_ull());
        nw::kernel::runtime().init_object_propsets(object);
        activate(object);
        ASSERT_TRUE(active_object_details_matches_tab(view, workspace));
        const auto row = std::ranges::find_if(view.object_details.rows, [&](const ObjectDetailsRow& candidate) {
            return candidate.editor == editor && candidate.kind == ObjectDetailsRowKind::value
                && (editor != ObjectDetailsEditorKind::integer || candidate.edit_value < candidate.edit_max);
        });
        ASSERT_NE(row, view.object_details.rows.end());
        const auto index = static_cast<uint32_t>(std::distance(view.object_details.rows.begin(), row));
        const auto before = row->edit_value;
        const bool integer = editor == ObjectDetailsEditorKind::integer;
        document->SetInnerRML("<button id='target' class='"
            + std::string{integer ? "object_details_integer_step" : "object_details_cycle_state"}
            + "' data-row='" + std::to_string(index) + "' data-current='" + std::to_string(before)
            + "' data-delta='1'>Next</button>");
        auto click = capture_object_workbench_command_click(document->GetElementById("target"), view, workspace, backend.module_generation());
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, integer ? ObjectWorkbenchCommandKind::integer_step : ObjectWorkbenchCommandKind::door_state);
        EXPECT_EQ(click->release_phase, integer ? ClientRmlForwardPhase::before_native : ClientRmlForwardPhase::after_native);
        EXPECT_TRUE(execute_object_workbench_command_click(*click, view, workspace, backend, shell, command));
        EXPECT_EQ(workspace.undo_count(), 1);
        ObjectDetailsSnapshot current;
        build_object_details(nw::kernel::runtime(), object, current);
        ASSERT_LT(index, current.rows.size());
        EXPECT_EQ(current.rows[index].edit_value, integer ? before + 1 : (before + 1) % 3);
        ASSERT_TRUE(workspace.undo(command).ok());
        build_object_details(nw::kernel::runtime(), object, current);
        ASSERT_LT(index, current.rows.size());
        EXPECT_EQ(current.rows[index].edit_value, before);
    }
}

TEST_F(ClientObjectWorkbench, NativeDetailsRejectsAnOldDenseTokenForAnotherProperty)
{
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    nw::kernel::runtime().init_object_propsets(sound->handle());
    activate(sound->handle());
    ASSERT_TRUE(active_object_details_matches_tab(view, workspace));
    const auto& rows = view.object_details.rows;
    size_t first = rows.size();
    size_t second = rows.size();
    for (size_t i = 0; i < rows.size() && second == rows.size(); ++i) {
        if (rows[i].kind != ObjectDetailsRowKind::value || rows[i].editor != ObjectDetailsEditorKind::boolean) { continue; }
        for (size_t j = i + 1; j < rows.size(); ++j) {
            if (rows[j].kind == ObjectDetailsRowKind::value && rows[j].editor == rows[i].editor
                && rows[j].edit_value == rows[i].edit_value
                && (rows[j].field_index != rows[i].field_index || rows[j].propset_type != rows[i].propset_type
                    || rows[j].element_index != rows[i].element_index)) {
                first = i;
                second = j;
                break;
            }
        }
    }
    ASSERT_LT(second, rows.size());
    const auto original = rows[first];
    // Simulate stale UI metadata: this dense slot displays a different property
    // with the same value, while the live SmallS provider still owns its meaning.
    view.object_details.rows[first] = rows[second];
    document->SetInnerRML("<span id='target' class='object_details_boolean' data-row='" + std::to_string(first)
        + "' data-current='" + std::to_string(rows[first].edit_value) + "'>Toggle</span>");
    auto click = capture_object_workbench_command_click(document->GetElementById("target"), view, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, ObjectWorkbenchCommandKind::boolean);
    const auto logs = shell.output_lines.size();
    EXPECT_FALSE(execute_object_workbench_command_click(*click, view, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_EQ(shell.output_lines.size(), logs);
    ObjectDetailsSnapshot current;
    build_object_details(nw::kernel::runtime(), sound->handle(), current);
    ASSERT_LT(first, current.rows.size());
    EXPECT_EQ(current.rows[first].field_index, original.field_index);
    EXPECT_EQ(current.rows[first].edit_value, original.edit_value);
}

TEST_F(ClientObjectWorkbench, InvalidNativeControlsReleaseWithoutAnEdit)
{
    document->SetInnerRML("<button id='integer' class='object_details_integer_step' data-row='2147483648' data-current='0' data-delta='1'>+</button>"
                          "<span id='boolean' class='object_details_boolean' data-row='-1' data-current='0'>Toggle</span>"
                          "<button id='type' class='object_variable_type' data-name='label' data-type='4'>Type</button><button id='other'>Other</button>");
    for (const auto [id, phase] : {std::pair{"integer", ClientRmlForwardPhase::before_native},
             std::pair{"boolean", ClientRmlForwardPhase::after_native},
             std::pair{"type", ClientRmlForwardPhase::before_native}}) {
        SCOPED_TRACE(id);
        auto click = capture_object_workbench_command_click(document->GetElementById(id), view, workspace, backend.module_generation());
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, ObjectWorkbenchCommandKind::none);
        EXPECT_EQ(click->release_phase, phase);
        EXPECT_FALSE(execute_object_workbench_command_click(*click, view, workspace, backend, shell, command));
    }
    EXPECT_FALSE(capture_object_workbench_command_click(document->GetElementById("other"), view, workspace, backend.module_generation()));
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientObjectWorkbench, DetailsWindowsCacheAndRejectStaleActiveTabs)
{
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    nw::kernel::runtime().init_object_propsets(sound->handle());
    activate(sound->handle());
    ASSERT_TRUE(active_object_details_matches_tab(view, workspace));
    document->SetInnerRML("<div id='property_tree_rows' style='height:400px;width:600px;overflow:auto;'></div>"
                          "<div id='property_tree_count'></div><div id='object_details_combobox_popup'></div>");
    context->Update();
    EXPECT_TRUE(sync_object_details_window(document, view, workspace, true));
    context->Update();
    auto* list = document->GetElementById("property_tree_rows");
    ASSERT_NE(list, nullptr);
    auto* first = list->GetChild(0);
    EXPECT_FALSE(sync_object_details_window(document, view, workspace, false));
    EXPECT_EQ(first, list->GetChild(0));
    EXPECT_EQ(active_details_row_count(view, workspace), view.object_details.rows.size());
    EXPECT_FALSE(open_object_details_sound_position_combobox(document, view, workspace, UINT32_MAX));
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    EXPECT_FALSE(active_object_details_matches_tab(view, workspace));
    EXPECT_TRUE(sync_object_details_window(document, view, workspace, true));
    EXPECT_NE(list->GetInnerRML().find("Waiting for the live preview object."), std::string::npos);
    clear_object_workbench_snapshots(view);
    EXPECT_TRUE(view.object_details.rows.empty());
    EXPECT_EQ(view.details_list.total_rows(), 0);
}

TEST_F(ClientObjectWorkbench, EnterRenameCommitsOnceDespiteSynchronousBlur)
{
    auto* module = nw::kernel::objects().make<nw::Module>();
    ASSERT_NE(module, nullptr);
    module->locals.set_int("old", 5);
    activate(module->handle());
    document->SetInnerRML("<input id='variable' class='object_variable_name' type='text' data-name='old' data-type='1' value='new'/>");
    context->Update();
    Listener listener{*this};
    context->AddEventListener("change", &listener, false);
    context->AddEventListener("blur", &listener, true);
    const auto remove = create_scope_exit([&] {
        context->RemoveEventListener("change", &listener, false);
        context->RemoveEventListener("blur", &listener, true);
    });
    auto* input = document->GetElementById("variable");
    ASSERT_NE(input, nullptr);
    input->Focus();
    Rml::Dictionary params{{Rml::String{"linebreak"}, Rml::Variant{true}}};
    input->DispatchEvent("change", params);
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_FALSE(view.suppress_blur_commit);
    EXPECT_EQ(module->locals.get_int("old"), 0);
    EXPECT_EQ(module->locals.get_int("new"), 5);
    EXPECT_TRUE(std::none_of(shell.output_lines.begin(), shell.output_lines.end(),
        [](const auto& row) { return row.first == "error" || row.first == "warn"; }));
}

TEST_F(ClientObjectWorkbench, VariableWindowsPreserveUtf8RowsAndSkipInactiveSurfaces)
{
    auto* module = nw::kernel::objects().make<nw::Module>();
    ASSERT_NE(module, nullptr);
    nw::kernel::runtime().init_object_propsets(module->handle());
    module->locals.set_string("label", "café <&>");
    module->locals.set_int("count", 2);
    activate(module->handle());
    ASSERT_TRUE(active_object_variables_match_tab(view, workspace));
    document->SetInnerRML("<div id='object_variable_rows' style='height:200px;width:600px;overflow:auto;'></div>"
                          "<div id='object_variable_count'></div>");
    context->Update();
    EXPECT_FALSE(sync_object_variable_window(document, view, workspace, true));
    view.object_workbench_surface = ObjectWorkbenchSurface::variables;
    EXPECT_TRUE(sync_object_variable_window(document, view, workspace, true));
    context->Update();
    auto* list = document->GetElementById("object_variable_rows");
    ASSERT_NE(list, nullptr);
    Rml::ElementList values;
    list->GetElementsByClassName(values, "object_variable_value");
    EXPECT_TRUE(std::any_of(values.begin(), values.end(), [](Rml::Element* element) {
        auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(element);
        return input && input->GetValue() == "café <&>";
    }));
    EXPECT_EQ(document->GetElementById("object_variable_count")->GetInnerRML(), "2");
    auto* first = list->GetChild(0);
    EXPECT_FALSE(sync_object_variable_window(document, view, workspace, false));
    EXPECT_EQ(list->GetChild(0), first);
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    EXPECT_FALSE(active_object_variables_match_tab(view, workspace));
    EXPECT_TRUE(sync_object_variable_window(document, view, workspace, true));
    EXPECT_NE(list->GetInnerRML().find("Waiting for a live object."), std::string::npos);
}

TEST_F(ClientObjectWorkbench, NumericPrefixRejectsInvalidInsertedText)
{
    document->SetInnerRML("<input id='variable' class='object_variable_value' type='text' data-type='1' data-last-valid='12' value='12'/>");
    context->Update();
    Listener listener{*this};
    context->AddEventListener("change", &listener, false);
    const auto remove = create_scope_exit([&] { context->RemoveEventListener("change", &listener, false); });
    auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("variable"));
    ASSERT_NE(input, nullptr);
    input->Focus();
    input->SetValue("12x");
    context->Update();
    input->SetSelectionRange(3, 3);
    input->DispatchEvent("change", {});
    EXPECT_EQ(input->GetValue(), "12");
    int cursor = 0;
    input->GetSelection(&cursor, nullptr, nullptr);
    EXPECT_EQ(cursor, 2);
    input->SetValue("-");
    context->Update();
    input->DispatchEvent("change", {});
    EXPECT_EQ(input->GetAttribute<Rml::String>("data-last-valid", ""), "-");
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientObjectWorkbench, SoundGestureCannotCommitAgainstTheReplacementObject)
{
    auto* first = nw::kernel::objects().make<nw::Sound>();
    auto* second = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    auto& runtime = nw::kernel::runtime();
    runtime.init_object_propsets(first->handle());
    runtime.init_object_propsets(second->handle());
    activate(first->handle());
    ASSERT_EQ(view.object_details.status, ObjectDetailsStatus::ready) << view.object_details.diagnostic;
    const auto volume = std::find_if(view.object_details.rows.begin(), view.object_details.rows.end(),
        [](const auto& row) { return row.editor == ObjectDetailsEditorKind::sound_volume; });
    ASSERT_NE(volume, view.object_details.rows.end());
    const auto row = static_cast<uint32_t>(volume - view.object_details.rows.begin());
    const auto before = volume->edit_value;
    document->SetInnerRML("<input id='volume' class='object_details_sound_volume' type='range' min='0' max='10' data-row='"
        + std::to_string(row) + "' data-current='" + std::to_string(before) + "'/>");
    context->Update();
    Listener listener{*this};
    context->AddEventListener("change", &listener, false);
    const auto remove = create_scope_exit([&] { context->RemoveEventListener("change", &listener, false); });
    Rml::Dictionary params{{Rml::String{"value"}, Rml::Variant{7.0f}}};
    document->GetElementById("volume")->DispatchEvent("change", params);
    ASSERT_TRUE(view.pending_sound_volume);
    EXPECT_EQ(workspace.undo_count(), 0);
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    activate(second->handle());
    ASSERT_EQ(view.object_details.rows[row].edit_value, before);
    EXPECT_FALSE(commit_object_workbench_sound_volume(view, workspace, backend, shell, command));
    EXPECT_FALSE(view.pending_sound_volume);
    ObjectDetailsSnapshot snapshot;
    build_object_details(runtime, second->handle(), snapshot);
    EXPECT_EQ(snapshot.rows[row].edit_value, before);
    build_object_details(runtime, first->handle(), snapshot);
    EXPECT_EQ(snapshot.rows[row].edit_value, before);
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientObjectWorkbench, SoundGestureCoalescesAndRejectsStaleOrInvalidInputs)
{
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    auto& runtime = nw::kernel::runtime();
    runtime.init_object_propsets(sound->handle());
    activate(sound->handle());
    const auto volume = std::find_if(view.object_details.rows.begin(), view.object_details.rows.end(),
        [](const auto& row) { return row.editor == ObjectDetailsEditorKind::sound_volume; });
    ASSERT_NE(volume, view.object_details.rows.end());
    const auto row = static_cast<uint32_t>(volume - view.object_details.rows.begin());
    const auto before = volume->edit_value;
    document->SetInnerRML("<input id='volume' class='object_details_sound_volume' type='range' min='0' max='10' data-row='"
        + std::to_string(row) + "' data-current='" + std::to_string(before) + "'/>");
    context->Update();
    Listener listener{*this};
    context->AddEventListener("change", &listener, false);
    const auto remove = create_scope_exit([&] { context->RemoveEventListener("change", &listener, false); });
    auto* input = document->GetElementById("volume");
    ASSERT_NE(input, nullptr);
    const auto stage = [&](float value) {
        Rml::Dictionary params{{Rml::String{"value"}, Rml::Variant{value}}};
        input->DispatchEvent("change", params);
    };
    stage(3);
    stage(7);
    ASSERT_TRUE(view.pending_sound_volume);
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_TRUE(commit_object_workbench_sound_volume(view, workspace, backend, shell, command));
    EXPECT_FALSE(commit_object_workbench_sound_volume(view, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 1);
    rebuild_object_workbench_snapshots(view, sound->handle());
    EXPECT_EQ(view.object_details.rows[row].edit_value, *sound_volume_storage_value(7));
    EXPECT_TRUE(workspace.undo(command).ok());
    rebuild_object_workbench_snapshots(view, sound->handle());
    EXPECT_EQ(view.object_details.rows[row].edit_value, before);
    stage(7);
    view.object_details.rows[row].edit_value = before + 1;
    EXPECT_FALSE(commit_object_workbench_sound_volume(view, workspace, backend, shell, command));
    rebuild_object_workbench_snapshots(view, sound->handle());

    auto* replacement = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(replacement, nullptr);
    runtime.init_object_propsets(replacement->handle());
    stage(7);
    smalls_rmlui_host().publish_active_object(replacement->handle());
    const auto log_rows = shell.output_lines.size();
    EXPECT_FALSE(commit_object_workbench_sound_volume(view, workspace, backend, shell, command));
    EXPECT_EQ(shell.output_lines.size(), log_rows);
    ObjectDetailsSnapshot replacement_snapshot;
    build_object_details(runtime, replacement->handle(), replacement_snapshot);
    EXPECT_EQ(replacement_snapshot.rows[row].edit_value, before);
    smalls_rmlui_host().publish_active_object(sound->handle());

    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
             std::numeric_limits<float>::max(), -1.0f, 11.0f, 3.5f}) {
        stage(7);
        ASSERT_TRUE(view.pending_sound_volume);
        stage(invalid);
        EXPECT_FALSE(view.pending_sound_volume);
        EXPECT_FALSE(commit_object_workbench_sound_volume(view, workspace, backend, shell, command));
    }
    stage(7);
    clear_object_workbench_snapshots(view);
    EXPECT_FALSE(view.pending_sound_volume);
    EXPECT_FALSE(commit_object_workbench_sound_volume(view, workspace, backend, shell, command));
    activate(sound->handle());
    stage(7);
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    command.active_tab_id = workspace.active_tab_id();
    EXPECT_FALSE(commit_object_workbench_sound_volume(view, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 0);
    ObjectDetailsSnapshot snapshot;
    build_object_details(runtime, sound->handle(), snapshot);
    EXPECT_EQ(snapshot.rows[row].edit_value, before);
}

class ClientCreatureWorkbench : public ClientObjectWorkbench { };

TEST_F(ClientCreatureWorkbench, NativeClassAndFeatCommandsUseLiveRowsAndOneUndo)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    CreatureWorkbenchViewState creature;
    rebuild_creature_class_presentation(creature, actor->handle());
    rebuild_active_creature_feats(creature, actor->handle());
    const auto target = object_workbench_target(view, workspace);
    const auto row = std::find_if(creature.creature_class_presentation.rows.begin(), creature.creature_class_presentation.rows.end(),
        [](const auto& value) { return value.level < value.maximum_level; });
    ASSERT_NE(row, creature.creature_class_presentation.rows.end());
    const auto slot = row->slot;
    const auto level = row->level;
    document->SetInnerRML("<button class='creature_class_level_adjust' data-slot='" + std::to_string(slot)
        + "' data-delta='1'><span id='target'>+</span></button>");
    auto click = capture_creature_workbench_command_click(document->GetElementById("target"), creature, target, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, CreatureWorkbenchCommandKind::class_level);
    EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
    document->SetInnerRML("<div>Replacement</div>");
    ASSERT_TRUE(execute_creature_workbench_command_click(*click, creature, target, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_EQ(editable_creature_class_levels(nw::kernel::runtime(), actor->handle())[static_cast<size_t>(slot)], level + 1);
    const auto log_count = shell.output_lines.size();
    EXPECT_FALSE(execute_creature_workbench_command_click(*click, creature, target, workspace, backend, shell, command));
    EXPECT_EQ(shell.output_lines.size(), log_count);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(editable_creature_class_levels(nw::kernel::runtime(), actor->handle())[static_cast<size_t>(slot)], level);

    ASSERT_FALSE(creature.creature_feats.rows.empty());
    const auto feat = creature.creature_feats.rows.front();
    document->SetInnerRML("<div class='creature_feat_row' data-key='" + std::to_string(feat.feat_id) + "'><span id='target'>Feat</span></div>");
    click = capture_creature_workbench_command_click(document->GetElementById("target"), creature, target, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, CreatureWorkbenchCommandKind::feat);
    ASSERT_TRUE(execute_creature_workbench_command_click(*click, creature, target, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 1);
    rebuild_active_creature_feats(creature, actor->handle());
    const auto changed = std::ranges::find(creature.creature_feats.rows, feat.feat_id, &CreatureFeatRow::feat_id);
    ASSERT_NE(changed, creature.creature_feats.rows.end());
    EXPECT_EQ(changed->assigned, !feat.assigned);
    ASSERT_TRUE(workspace.undo(command).ok());
    rebuild_active_creature_feats(creature, actor->handle());
    const auto restored = std::ranges::find(creature.creature_feats.rows, feat.feat_id, &CreatureFeatRow::feat_id);
    ASSERT_NE(restored, creature.creature_feats.rows.end());
    EXPECT_EQ(restored->assigned, feat.assigned);
}

TEST_F(ClientCreatureWorkbench, NativeKnownAndMemorizedCommandsPreserveSelectedMeaningAndUndo)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    for (const bool memorized : {false, true}) {
        SCOPED_TRACE(memorized);
        auto* actor = nw::kernel::objects().load_file<nw::Creature>(memorized
                ? "test_data/user/development/wizard_pm.utc"
                : "test_data/user/development/sorcrdd.utc");
        ASSERT_NE(actor, nullptr);
        activate(actor->handle());
        view.object_workbench_surface = ObjectWorkbenchSurface::spells;
        CreatureWorkbenchViewState creature;
        rebuild_active_creature_spells(creature, actor->handle(), memorized ? 10 : 9, 0);
        ASSERT_EQ(creature.creature_spells.status, CreatureSpellViewStatus::ready);
        ASSERT_EQ(creature.creature_spells.memorizes, memorized);
        const auto row = std::ranges::find(creature.creature_spells.rows, 100, &CreatureSpellRow::spell_id);
        ASSERT_NE(row, creature.creature_spells.rows.end());
        const auto before = *row;
        if (memorized) {
            ASSERT_GT(before.uses, 0);
        } else {
            ASSERT_TRUE(before.known);
        }
        document->SetInnerRML(memorized
                ? "<div class='creature_spell_row' data-key='100'><button class='creature_spell_decrement' data-spell='100'><span id='target'>-</span></button></div>"
                : "<div class='creature_spell_row' data-key='100'><span id='target'>Spell</span></div>");
        const auto target = object_workbench_target(view, workspace);
        auto click = capture_creature_workbench_command_click(document->GetElementById("target"), creature, target, workspace, backend.module_generation());
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, memorized ? CreatureWorkbenchCommandKind::memorized_spell : CreatureWorkbenchCommandKind::known_spell);
        EXPECT_EQ(click->class_id, memorized ? 10 : 9);
        EXPECT_EQ(click->metamagic, 0);
        document->SetInnerRML("<div>Replacement</div>");
        ASSERT_TRUE(execute_creature_workbench_command_click(*click, creature, target, workspace, backend, shell, command));
        EXPECT_EQ(workspace.undo_count(), 1);
        CreatureSpellViewSnapshot current;
        build_creature_spell_rows(nw::kernel::runtime(), actor->handle(), memorized ? 10 : 9, 0, current);
        const auto changed = std::ranges::find(current.rows, 100, &CreatureSpellRow::spell_id);
        ASSERT_NE(changed, current.rows.end());
        if (memorized) {
            EXPECT_EQ(changed->uses, before.uses - 1);
        } else {
            EXPECT_FALSE(changed->known);
        }
        EXPECT_FALSE(execute_creature_workbench_command_click(*click, creature, target, workspace, backend, shell, command));
        if (memorized) {
            // Removing a use made an actual slot available for the increment.
            rebuild_active_creature_spells(creature, actor->handle(), 10, 0);
            document->SetInnerRML("<button id='target' class='creature_spell_increment' data-spell='100'>+</button>");
            auto increment = capture_creature_workbench_command_click(document->GetElementById("target"), creature, target, workspace, backend.module_generation());
            ASSERT_TRUE(increment);
            ASSERT_EQ(increment->kind, CreatureWorkbenchCommandKind::memorized_spell);
            ASSERT_TRUE(execute_creature_workbench_command_click(*increment, creature, target, workspace, backend, shell, command));
            EXPECT_EQ(workspace.undo_count(), 2);
            build_creature_spell_rows(nw::kernel::runtime(), actor->handle(), 10, 0, current);
            const auto added = std::ranges::find(current.rows, 100, &CreatureSpellRow::spell_id);
            ASSERT_NE(added, current.rows.end());
            EXPECT_EQ(added->uses, before.uses);
            ASSERT_TRUE(workspace.undo(command).ok());
        }
        ASSERT_TRUE(workspace.undo(command).ok());
        build_creature_spell_rows(nw::kernel::runtime(), actor->handle(), memorized ? 10 : 9, 0, current);
        const auto restored = std::ranges::find(current.rows, 100, &CreatureSpellRow::spell_id);
        ASSERT_NE(restored, current.rows.end());
        EXPECT_EQ(restored->uses, before.uses);
        EXPECT_EQ(restored->known, before.known);
    }
}

TEST_F(ClientCreatureWorkbench, NativeCommandRejectsChangedFiltersOwnersAndMutations)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::spells;
    CreatureWorkbenchViewState creature;
    rebuild_active_creature_spells(creature, actor->handle(), 10, 0);
    document->SetInnerRML("<button id='target' class='creature_spell_decrement' data-spell='100'>-</button>");
    const auto target = object_workbench_target(view, workspace);
    const auto original = capture_creature_workbench_command_click(document->GetElementById("target"), creature, target, workspace, backend.module_generation());
    ASSERT_TRUE(original);
    ASSERT_EQ(original->kind, CreatureWorkbenchCommandKind::memorized_spell);
    const auto log_count = shell.output_lines.size();
    auto click = *original;
    creature.creature_spells.selected_class = 9;
    EXPECT_FALSE(execute_creature_workbench_command_click(click, creature, target, workspace, backend, shell, command));
    creature.creature_spells.selected_class = 10;
    click = *original;
    creature.creature_spells.selected_metamagic = 1;
    EXPECT_FALSE(execute_creature_workbench_command_click(click, creature, target, workspace, backend, shell, command));
    creature.creature_spells.selected_metamagic = 0;
    click = *original;
    ++click.module_generation;
    EXPECT_FALSE(execute_creature_workbench_command_click(click, creature, target, workspace, backend, shell, command));
    click = *original;
    ++click.current;
    EXPECT_FALSE(execute_creature_workbench_command_click(click, creature, target, workspace, backend, shell, command));
    click = *original;
    auto replacement_target = target;
    auto* replacement = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(replacement, nullptr);
    replacement_target.object = replacement->handle();
    EXPECT_FALSE(execute_creature_workbench_command_click(click, creature, replacement_target, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_EQ(shell.output_lines.size(), log_count);

    CommandArgs args;
    for (const char* value : {"10", "100", "0", "-1"}) {
        args.push_back(CommandArg::positional_string(value));
    }
    ASSERT_TRUE(backend.execute_command({"object.creature.adjust_memorized_spell", std::move(args)}, command).ok());
    EXPECT_EQ(workspace.undo_count(), 1);
    click = *original;
    EXPECT_FALSE(execute_creature_workbench_command_click(click, creature, target, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_EQ(shell.output_lines.size(), log_count);
}

TEST_F(ClientCreatureWorkbench, NativeCommandCaptureRejectsMalformedAndUnavailableControls)
{
    CreatureWorkbenchViewState creature;
    document->SetInnerRML("<button id='slot' class='creature_class_level_adjust' data-slot='2147483648' data-delta='1'>+</button>"
                          "<button id='spell' class='creature_spell_increment' data-spell='-1'>+</button>"
                          "<div id='feat' class='creature_feat_row' data-key='7suffix'>Feat</div><div id='other'>Other</div>");
    for (const char* id : {"slot", "spell", "feat"}) {
        const auto click = capture_creature_workbench_command_click(document->GetElementById(id), creature,
            object_workbench_target(view, workspace), workspace, backend.module_generation());
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, CreatureWorkbenchCommandKind::none);
        EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
        EXPECT_TRUE(click->tab_id.empty());
    }
    EXPECT_FALSE(capture_creature_workbench_command_click(document->GetElementById("other"), creature,
        object_workbench_target(view, workspace), workspace, backend.module_generation()));
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientCreatureWorkbench, RealCreatureClassesAndFeatWindowsKeepTheirContracts)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    CreatureWorkbenchViewState creature;
    rebuild_creature_class_presentation(creature, actor->handle());
    rebuild_active_creature_feats(creature, actor->handle());
    auto target = object_workbench_target(view, workspace);
    EXPECT_TRUE(active_creature_class_presentation_matches_tab(creature, target));
    ASSERT_FALSE(creature.creature_class_presentation.rows.empty());
    std::string markup;
    append_creature_classes_markup(markup, creature, target);
    EXPECT_NE(markup.find("creature_class_level_adjust"), std::string::npos);
    ASSERT_EQ(creature.creature_feats.status, CreatureFeatViewStatus::ready) << creature.creature_feats.diagnostic;
    ASSERT_FALSE(creature.creature_feats.rows.empty());
    document->SetInnerRML("<div id='creature_feat_rows' style='height:240px;width:600px;overflow:auto;'></div>"
                          "<div id='creature_feat_count'></div>");
    context->Update();
    EXPECT_TRUE(sync_creature_feat_window(document, creature, target, true));
    context->Update();
    auto* list = document->GetElementById("creature_feat_rows");
    ASSERT_NE(list, nullptr);
    auto* first = list->GetChild(0);
    EXPECT_FALSE(sync_creature_feat_window(document, creature, target, false));
    EXPECT_EQ(list->GetChild(0), first);
    const auto feat_id = creature.creature_feats.rows.front().feat_id;
    creature.creature_feat_query = creature.creature_feats.text_view(creature.creature_feats.rows.front().name);
    rebuild_active_creature_feats(creature, actor->handle());
    EXPECT_TRUE(std::any_of(creature.creature_feats.rows.begin(), creature.creature_feats.rows.end(),
        [&](const auto& row) { return row.feat_id == feat_id; }));
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    target = object_workbench_target(view, workspace);
    EXPECT_FALSE(target.matches_active_tab);
    EXPECT_TRUE(sync_creature_feat_window(document, creature, target, true));
    EXPECT_NE(list->GetInnerRML().find("Waiting for a live Creature."), std::string::npos);
    clear_active_creature_feats(creature);
    EXPECT_TRUE(creature.creature_feats.rows.empty());
    EXPECT_EQ(creature.creature_feat_list.total_rows(), 0);
}

TEST_F(ClientCreatureWorkbench, RealSpellFiltersRejectStaleAndInvalidChoices)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::spells;
    CreatureWorkbenchViewState creature;
    rebuild_active_creature_spells(creature, actor->handle());
    ASSERT_EQ(creature.creature_spells.status, CreatureSpellViewStatus::ready) << creature.creature_spells.diagnostic;
    ASSERT_FALSE(creature.creature_spells.rows.empty());
    const auto target = object_workbench_target(view, workspace);
    const auto spell = std::find_if(creature.creature_spells.rows.begin(), creature.creature_spells.rows.end(),
        [](const auto& row) { return row.level >= 0 && row.level <= 9; });
    ASSERT_NE(spell, creature.creature_spells.rows.end());
    const auto level = spell->level;
    creature.creature_spell_query = creature.creature_spells.text_view(spell->name);
    EXPECT_TRUE(open_creature_spell_filter(creature, target, CreatureSpellFilterField::level));
    EXPECT_FALSE(commit_creature_spell_filter(creature, target, 10));
    EXPECT_TRUE(commit_creature_spell_filter(creature, target, level));
    ASSERT_FALSE(creature.creature_spell_matches.empty());
    EXPECT_TRUE(std::all_of(creature.creature_spell_matches.begin(), creature.creature_spell_matches.end(),
        [&](uint32_t index) { return index < creature.creature_spells.rows.size() && creature.creature_spells.rows[index].level == level; }));
    std::string markup;
    append_creature_spell_markup(markup, creature, target);
    document->SetInnerRML("<div id='object_workbench' style='height:500px;width:600px;overflow:auto;'>" + markup + "</div>");
    context->Update();
    EXPECT_TRUE(sync_creature_spell_window(document, creature, target, true));
    context->Update();
    auto* rows = document->GetElementById("creature_spell_rows");
    ASSERT_NE(rows, nullptr);
    auto* first = rows->GetChild(0);
    EXPECT_FALSE(sync_creature_spell_window(document, creature, target, false));
    EXPECT_EQ(rows->GetChild(0), first);
    EXPECT_EQ(document->GetElementById("creature_spell_value_header")->GetInnerRML(), "Uses");
    EXPECT_TRUE(open_creature_spell_filter(creature, target, CreatureSpellFilterField::class_));
    markup.clear();
    append_creature_spell_markup(markup, creature, target);
    append_creature_workbench_overlay_markup(markup, creature, target);
    document->SetInnerRML("<div id='object_workbench' style='height:500px;width:600px;overflow:auto;'>" + markup + "</div>");
    context->Update();
    EXPECT_TRUE(sync_creature_spell_filter_window(document, creature, target, true));
    context->Update();
    EXPECT_FALSE(sync_creature_spell_filter_window(document, creature, target, false));
    EXPECT_FALSE(commit_creature_spell_filter(creature, target, std::numeric_limits<int32_t>::max()));
    rows = document->GetElementById("creature_spell_rows");
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    const auto stale = object_workbench_target(view, workspace);
    EXPECT_FALSE(commit_creature_spell_filter(creature, stale, creature.creature_spells.selected_class));
    EXPECT_TRUE(sync_creature_spell_window(document, creature, stale, true));
    EXPECT_NE(rows->GetInnerRML().find("Waiting for a live Creature."), std::string::npos);
    clear_active_creature_spells(creature);
    EXPECT_TRUE(creature.creature_spell_matches.empty());
    EXPECT_FALSE(creature.creature_spell_combobox.is_active());
}

class ClientInventoryWorkbench : public ClientObjectWorkbench {
protected:
    void use_icon_fixture(nw::ObjectHandle item)
    {
        auto& components = nw::kernel::objects().components();
        ASSERT_TRUE(components.clear_item_icons(item));
        for (uint8_t variant = 0; variant < nw::ObjectItemIconState::variant_count; ++variant) {
            ASSERT_TRUE(components.add_item_icon_layer(item, variant,
                nw::ObjectItemIconLayer{.resource = nw::Resref{"bioRGBA"}}));
        }
    }
};

TEST_F(ClientInventoryWorkbench, NativeSelectionEquipmentAndUndoPreserveLiveItemIdentity)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    auto* item = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(actor->inventory().add_item(item));
    ASSERT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::belt), nullptr);
    const auto before = actor->inventory().items.back();
    activate(actor->handle());
    InventoryWorkbenchViewState inventory;
    rebuild_active_creature_inventory(inventory, actor->handle());
    ASSERT_EQ(inventory.creature_inventory.status, InventoryViewStatus::ready);
    const auto source_index = static_cast<int32_t>(inventory.creature_inventory.inventory.back().source_index);
    const auto target = object_workbench_target(view, workspace);
    document->SetInnerRML("<div class='creature_inventory_item' data-key='" + std::to_string(source_index) + "'><span id='target'>Item</span></div>");
    auto click = capture_inventory_workbench_click(document->GetElementById("target"), inventory, target, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->inventory_item, item->handle());
    EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
    document->SetInnerRML("<div>Replacement</div>");
    ASSERT_TRUE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
    EXPECT_EQ(inventory.creature_inventory_selection, source_index);
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_FALSE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));

    document->SetInnerRML("<div id='target' class='creature_equipment_slot' data-slot='10'>Belt</div>");
    click = capture_inventory_workbench_click(document->GetElementById("target"), inventory, target, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, InventoryWorkbenchClickKind::equipment);
    EXPECT_EQ(click->inventory_item, item->handle());
    EXPECT_EQ(click->selection, source_index);
    ASSERT_TRUE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::belt), item);
    EXPECT_FALSE(actor->inventory().has_item(item));
    EXPECT_EQ(inventory.creature_inventory_selection, -1);
    const auto log_count = shell.output_lines.size();
    EXPECT_FALSE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
    EXPECT_EQ(shell.output_lines.size(), log_count);

    rebuild_active_creature_inventory(inventory, actor->handle());
    click = capture_inventory_workbench_click(document->GetElementById("target"), inventory, target, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->equipped_item, item->handle());
    ASSERT_TRUE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 2);
    EXPECT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::belt), nullptr);
    EXPECT_TRUE(actor->inventory().has_item(item));
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::belt), item);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::belt), nullptr);
    const auto restored = std::find_if(actor->inventory().items.begin(), actor->inventory().items.end(),
        [&](const auto& entry) { return nw::inventory_item_ptr(entry) == item; });
    ASSERT_NE(restored, actor->inventory().items.end());
    EXPECT_EQ(restored->pos_x, before.pos_x);
    EXPECT_EQ(restored->pos_y, before.pos_y);
    EXPECT_EQ(restored->infinite, before.infinite);
}

TEST_F(ClientInventoryWorkbench, NativePagesAndSelectionShareTheThreeLiveGridOwnerTypes)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    auto* item_owner = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
    auto* placeable = nw::kernel::objects().make<nw::Placeable>();
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(item_owner, nullptr);
    ASSERT_NE(placeable, nullptr);
    for (const auto object : {actor->handle(), item_owner->handle(), placeable->handle()}) {
        SCOPED_TRACE(object.to_ull());
        auto* item = nw::kernel::objects().load<nw::Item>("nw_wswss001");
        ASSERT_NE(item, nullptr);
        auto& grid = object == actor->handle() ? actor->inventory()
            : object == item_owner->handle()   ? item_owner->inventory()
                                               : placeable->inventory();
        ASSERT_TRUE(grid.add_item(item));
        activate(object);
        InventoryWorkbenchViewState inventory;
        rebuild_active_creature_inventory(inventory, object);
        ASSERT_EQ(inventory.creature_inventory.status, InventoryViewStatus::ready);
        const auto index = inventory.creature_inventory.inventory.back().source_index;
        const auto desired_page = grid.pages() > 1 ? 1 : 0;
        const auto target = object_workbench_target(view, workspace);
        document->SetInnerRML("<div id='item' class='creature_inventory_item' data-key='" + std::to_string(index)
            + "'>Item</div><button id='page' class='creature_inventory_page' data-page='" + std::to_string(desired_page) + "'>Page</button>");
        auto click = capture_inventory_workbench_click(document->GetElementById("item"), inventory, target, workspace, backend.module_generation());
        ASSERT_TRUE(click);
        ASSERT_TRUE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
        EXPECT_EQ(inventory.creature_inventory_selection, static_cast<int32_t>(index));
        click = capture_inventory_workbench_click(document->GetElementById("page"), inventory, target, workspace, backend.module_generation());
        ASSERT_TRUE(click);
        ASSERT_TRUE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
        EXPECT_EQ(inventory.creature_inventory_page, desired_page);
        EXPECT_EQ(inventory.creature_inventory_selection, -1);
        EXPECT_FALSE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
        EXPECT_EQ(workspace.undo_count(), 0);
    }
}

TEST_F(ClientInventoryWorkbench, NativeDenseSourceAndSlotChangesRejectBeforeAnEdit)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    auto* first = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
    auto* second = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_TRUE(actor->inventory().add_item(first));
    ASSERT_TRUE(actor->inventory().add_item(second));
    activate(actor->handle());
    InventoryWorkbenchViewState inventory;
    rebuild_active_creature_inventory(inventory, actor->handle());
    const auto index = static_cast<int32_t>(inventory.creature_inventory.inventory.size() - 2);
    const auto target = object_workbench_target(view, workspace);
    document->SetInnerRML("<div id='item' class='creature_inventory_item' data-key='" + std::to_string(index)
        + "'>Item</div><div id='slot' class='creature_equipment_slot' data-slot='10'>Belt</div>");
    auto click = capture_inventory_workbench_click(document->GetElementById("item"), inventory, target, workspace, backend.module_generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->inventory_item, first->handle());
    // Simulate an unsignaled live source reorder with two identical blueprints.
    const auto epoch = object_mutation_state().epoch;
    std::swap(actor->inventory().items[static_cast<size_t>(index)], actor->inventory().items[static_cast<size_t>(index + 1)]);
    EXPECT_FALSE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
    EXPECT_EQ(inventory.creature_inventory_selection, -1);
    EXPECT_EQ(object_mutation_state().epoch, epoch);
    std::swap(actor->inventory().items[static_cast<size_t>(index)], actor->inventory().items[static_cast<size_t>(index + 1)]);

    inventory.creature_inventory_selection = index;
    const auto original = capture_inventory_workbench_click(document->GetElementById("slot"), inventory, target, workspace, backend.module_generation());
    ASSERT_TRUE(original);
    ASSERT_EQ(original->kind, InventoryWorkbenchClickKind::equipment);
    const auto log_count = shell.output_lines.size();
    click = original;
    inventory.creature_inventory_selection = index + 1;
    EXPECT_FALSE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
    inventory.creature_inventory_selection = index;
    click = original;
    ++click->module_generation;
    EXPECT_FALSE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
    click = original;
    auto other = target;
    other.object = first->handle();
    EXPECT_FALSE(apply_inventory_workbench_click(*click, inventory, other, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_EQ(shell.output_lines.size(), log_count);

    CommandArgs args{CommandArg::positional_string(std::to_string(index + 1)), CommandArg::positional_string("10")};
    ASSERT_TRUE(backend.execute_command({"object.creature.equip_inventory_item", std::move(args)}, command).ok());
    EXPECT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::belt), second);
    click = original;
    // Keep the fixture's epoch current to isolate live slot identity validation.
    click->mutation_epoch = object_mutation_state().epoch;
    EXPECT_FALSE(apply_inventory_workbench_click(*click, inventory, target, workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_EQ(nw::get_equipped_item(actor, nw::EquipIndex::belt), second);
    EXPECT_TRUE(actor->inventory().has_item(first));
    EXPECT_EQ(shell.output_lines.size(), log_count);
}

TEST_F(ClientInventoryWorkbench, NativeCaptureRejectsMalformedAndUnavailableControls)
{
    InventoryWorkbenchViewState inventory;
    document->SetInnerRML("<div id='slot' class='creature_equipment_slot' data-slot='18'>Slot</div>"
                          "<div id='item' class='creature_inventory_item' data-key='2147483648'>Item</div>"
                          "<button id='page' class='creature_inventory_page' data-page='-1'>Page</button><div id='other'>Other</div>");
    for (const char* id : {"slot", "item", "page"}) {
        const auto click = capture_inventory_workbench_click(document->GetElementById(id), inventory,
            object_workbench_target(view, workspace), workspace, backend.module_generation());
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, InventoryWorkbenchClickKind::none);
        EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
        EXPECT_TRUE(click->tab_id.empty());
    }
    EXPECT_FALSE(capture_inventory_workbench_click(document->GetElementById("other"), inventory,
        object_workbench_target(view, workspace), workspace, backend.module_generation()));
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientInventoryWorkbench, RealEquipmentGridSelectionAndIconsKeepTheirOwner)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    auto* item = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(item, nullptr);
    use_icon_fixture(item->handle());
    ASSERT_TRUE(actor->inventory().add_item(item));
    activate(actor->handle());
    InventoryWorkbenchViewState inventory;
    auto* const bound_textures = &inventory.item_icon_cache.textures;
    inventory.creature_inventory_page = std::numeric_limits<int32_t>::max();
    inventory.creature_inventory_selection = std::numeric_limits<int32_t>::max();
    rebuild_active_creature_inventory(inventory, actor->handle());
    ASSERT_EQ(inventory.creature_inventory.status, InventoryViewStatus::ready) << inventory.creature_inventory.diagnostic;
    EXPECT_EQ(inventory.creature_inventory_page, 0);
    EXPECT_EQ(inventory.creature_inventory_selection, -1);
    ASSERT_FALSE(inventory.creature_inventory.inventory.empty());
    const auto row = inventory.creature_inventory.inventory.back();
    ASSERT_EQ(row.item, item->handle());
    inventory.creature_inventory_page = row.page;
    inventory.creature_inventory_selection = static_cast<int32_t>(row.source_index);
    const std::string icon_source{inventory.creature_inventory.text_view(row.icon_source)};
    ASSERT_FALSE(icon_source.empty());
    ASSERT_NE(find_generated_texture(*bound_textures, icon_source), nullptr);
    const auto texture_count = bound_textures->size();
    ASSERT_GT(texture_count, 0);
    const auto target = object_workbench_target(view, workspace);
    std::string markup;
    append_creature_inventory_markup(markup, inventory, target);
    document->SetInnerRML(markup);
    context->Update();
    Rml::ElementList slots;
    document->GetElementsByClassName(slots, "creature_equipment_slot");
    EXPECT_EQ(slots.size(), 18);
    auto* board = document->GetElementById("creature_inventory_board");
    ASSERT_NE(board, nullptr);
    Rml::ElementList selected;
    board->GetElementsByClassName(selected, "selected");
    ASSERT_EQ(selected.size(), 1);
    EXPECT_EQ(selected.front()->GetAttribute<int>("data-key", -1), static_cast<int>(row.source_index));
    EXPECT_EQ(document->GetElementById("creature_inventory_count")->GetInnerRML(),
        std::to_string(inventory.creature_inventory.inventory.size()));
    EXPECT_TRUE(sync_creature_inventory_window(document, inventory, target, true));
    board = document->GetElementById("creature_inventory_board");
    EXPECT_FALSE(sync_creature_inventory_window(document, inventory, target, false));
    EXPECT_EQ(document->GetElementById("creature_inventory_board"), board);
    rebuild_active_creature_inventory(inventory, actor->handle());
    EXPECT_EQ(inventory.creature_inventory_selection, static_cast<int32_t>(row.source_index));
    EXPECT_EQ(inventory.creature_inventory_page, row.page);
    EXPECT_EQ(&inventory.item_icon_cache.textures, bound_textures);
    EXPECT_EQ(bound_textures->size(), texture_count);
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    EXPECT_TRUE(sync_creature_inventory_window(document, inventory, object_workbench_target(view, workspace), true));
    EXPECT_NE(document->GetElementById("creature_inventory_page_surface")->GetInnerRML().find("Waiting for a live object inventory."), std::string::npos);
    clear_active_creature_inventory(inventory);
    EXPECT_EQ(inventory.creature_inventory_page, 0);
    EXPECT_EQ(inventory.creature_inventory_selection, -1);
    EXPECT_TRUE(inventory.creature_inventory.inventory.empty());
    EXPECT_EQ(bound_textures->size(), texture_count);
}

TEST_F(ClientInventoryWorkbench, SharedItemAndPlaceableGridsReportInvalidFootprints)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto* item_owner = nw::kernel::objects().load<nw::Item>("x2_it_mbelt001");
    auto* placeable = nw::kernel::objects().make<nw::Placeable>();
    ASSERT_NE(item_owner, nullptr);
    ASSERT_NE(placeable, nullptr);
    InventoryWorkbenchViewState inventory;
    for (auto object : std::array{item_owner->handle(), placeable->handle()}) {
        auto* item = nw::kernel::objects().load<nw::Item>("nw_wswss001");
        ASSERT_NE(item, nullptr);
        use_icon_fixture(item->handle());
        ASSERT_TRUE(object.type == nw::ObjectType::item
                ? item_owner->inventory().add_item(item)
                : placeable->inventory().add_item(item));
        activate(object);
        rebuild_active_creature_inventory(inventory, object);
        ASSERT_EQ(inventory.creature_inventory.status, InventoryViewStatus::ready) << inventory.creature_inventory.diagnostic;
        ASSERT_EQ(inventory.creature_inventory.inventory.size(), 1);
        std::string markup;
        append_creature_inventory_markup(markup, inventory, object_workbench_target(view, workspace));
        EXPECT_NE(markup.find("item_inventory_editor"), std::string::npos);
        EXPECT_EQ(markup.find("creature_equipment_slot"), std::string::npos);
        document->SetInnerRML(markup);
        context->Update();
        ASSERT_NE(document->GetElementById("creature_inventory_board"), nullptr);
        ASSERT_TRUE(nw::kernel::objects().components().set_item_layout(item->handle(), 11, 1));
        rebuild_active_creature_inventory(inventory, object);
        EXPECT_EQ(inventory.creature_inventory.status, InventoryViewStatus::invalid_data);
        EXPECT_FALSE(inventory.creature_inventory.diagnostic.empty());
        EXPECT_TRUE(sync_creature_inventory_window(document, inventory, object_workbench_target(view, workspace), true));
        EXPECT_NE(document->GetElementById("creature_inventory_page_surface")->GetInnerRML().find("error"), std::string::npos);
        EXPECT_EQ(document->GetElementById("creature_inventory_count")->GetInnerRML(), "0");
    }
    rebuild_active_creature_inventory(inventory, nw::ObjectHandle{});
    EXPECT_EQ(inventory.creature_inventory.status, InventoryViewStatus::invalid_object);
    EXPECT_EQ(inventory.creature_inventory_page, 0);
    EXPECT_EQ(inventory.creature_inventory_selection, -1);
}

class ClientAppearanceView : public ClientObjectWorkbench {
protected:
    void load_fixture()
    {
        ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
        auto& runtime = nw::kernel::runtime();
        runtime.add_module_path("stdlib/core");
        runtime.add_module_path("stdlib/nwn1");
        runtime.add_module_path("stdlib/toolset");
        ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);
    }
};

TEST_F(ClientAppearanceView, WorkbenchActivationAndMutationRetainTheirDifferentState)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    auto* other = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(other, nullptr);
    smalls_rmlui_host().publish_active_object(actor->handle());
    activate_object_workbench(view, actor->handle(), workspace.active_tab_id());
    ASSERT_EQ(view.object_details.status, ObjectDetailsStatus::ready);
    ASSERT_EQ(view.creature_view.creature_spells.status, CreatureSpellViewStatus::ready);
    view.creature_view.creature_feat_query = "retained feat query";
    view.creature_view.creature_spell_query = "retained spell query";
    view.object_workbench_surface = ObjectWorkbenchSurface::spells;
    view.inventory_view.creature_inventory_selection = -1;
    const auto selected_class = view.creature_view.creature_spells.selected_class;
    const auto selected_metamagic = view.creature_view.creature_spells.selected_metamagic;
    EXPECT_TRUE(refresh_object_workbench_snapshots(view, actor->handle()));
    EXPECT_EQ(view.object_workbench_surface, ObjectWorkbenchSurface::spells);
    EXPECT_EQ(view.creature_view.creature_feat_query, "retained feat query");
    EXPECT_EQ(view.creature_view.creature_spell_query, "retained spell query");
    EXPECT_EQ(view.creature_view.creature_spells.selected_class, selected_class);
    EXPECT_EQ(view.creature_view.creature_spells.selected_metamagic, selected_metamagic);
    EXPECT_FALSE(refresh_object_workbench_snapshots(view, other->handle()));
    EXPECT_EQ(view.object_details.object, actor->handle());

    view.inventory_view.creature_inventory_page = 7;
    view.inventory_view.creature_inventory_selection = 8;
    view.appearance_view.appearance_selector_open = true;
    view.appearance_view.sound_resource_selector_open = true;
    smalls_rmlui_host().publish_active_object(other->handle());
    activate_object_workbench(view, other->handle(), workspace.active_tab_id());
    EXPECT_EQ(view.object_details.object, other->handle());
    EXPECT_EQ(view.object_workbench_surface, default_object_workbench_surface());
    EXPECT_EQ(view.inventory_view.creature_inventory_page, 0);
    EXPECT_EQ(view.inventory_view.creature_inventory_selection, -1);
    EXPECT_FALSE(view.appearance_view.appearance_selector_open);
    EXPECT_FALSE(view.appearance_view.sound_resource_selector_open);
    EXPECT_EQ(view.creature_view.creature_feat_query, "retained feat query");
    EXPECT_EQ(view.creature_view.creature_spell_query, "retained spell query");
    clear_object_workbench_snapshots(view);
    clear_object_workbench_children(view);
    EXPECT_TRUE(view.creature_view.creature_feats.rows.empty());
    EXPECT_TRUE(view.creature_view.creature_spells.rows.empty());
    EXPECT_EQ(view.inventory_view.creature_inventory.inventory.size(), 0);
    EXPECT_FALSE(refresh_object_workbench_snapshots(view, other->handle()));
}

TEST_F(ClientAppearanceView, WorkbenchCompositionHydratesOnlyTheDisplayedCreatureSurface)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    ASSERT_NE(actor, nullptr);
    smalls_rmlui_host().publish_active_object(actor->handle());
    activate_object_workbench(view, actor->handle(), workspace.active_tab_id());
    view.object_workbench_surface = ObjectWorkbenchSurface::classes;
    document->SetInnerRML("<div id='creature_surface_details'></div><div id='creature_tab_classes'></div>"
                          "<div id='creature_surface_classes'></div><div id='creature_classes_dynamic'></div>"
                          "<div id='creature_workbench_dynamic_overlays'></div>"
                          "<div id='item_surface_details'>unrelated surface</div>");
    hydrate_object_workbench(document, view, workspace);
    context->Update();
    EXPECT_TRUE(document->GetElementById("creature_tab_classes")->IsClassSet("active"));
    EXPECT_TRUE(document->GetElementById("creature_surface_classes")->IsClassSet("active"));
    EXPECT_NE(document->GetElementById("creature_classes_dynamic")->GetInnerRML().find("creature_class_level_adjust"), std::string::npos);
    EXPECT_EQ(document->GetElementById("item_surface_details")->GetInnerRML(), "unrelated surface");
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    activate_object_workbench(view, sound->handle(), workspace.active_tab_id());
    view.object_workbench_surface = ObjectWorkbenchSurface::sounds;
    std::string markup;
    append_object_workbench_markup(markup, view, workspace, backend);
    EXPECT_NE(markup.find("data.sound.resources"), std::string::npos);
    view.appearance_view.sound_resource_selector_open = true;
    markup.clear();
    append_object_workbench_markup(markup, view, workspace, backend);
    EXPECT_NE(markup.find("sound_catalog_search"), std::string::npos);
}

TEST_F(ClientAppearanceView, CatalogQueriesWindowsAndModuleInvalidationKeepTheirContracts)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    AppearanceViewState appearance;
    const auto generation = backend.module_generation();
    rebuild_active_appearances(appearance, generation, actor->handle());
    ASSERT_EQ(appearance.creature_appearance_catalog.status, AppearanceCatalogStatus::ready);
    ASSERT_FALSE(appearance.creature_appearance_catalog.rows.empty());
    appearance.appearance_query = "Bodak";
    appearance.appearance_selector_open = true;
    rebuild_active_appearances(appearance, generation, actor->handle());
    ASSERT_FALSE(appearance.appearance_matches.empty());
    EXPECT_EQ(appearance.appearance_query, "Bodak");
    auto target = object_workbench_target(view, workspace);
    EXPECT_TRUE(active_appearances_match_tab(appearance, target));
    std::string markup;
    append_appearance_selector_markup(markup, appearance);
    document->SetInnerRML(markup);
    context->Update();
    EXPECT_TRUE(sync_appearance_window(document, appearance, target, true));
    context->Update();
    auto* rows = document->GetElementById("appearance_rows");
    ASSERT_NE(rows, nullptr);
    auto* first = rows->GetChild(0);
    EXPECT_FALSE(sync_appearance_window(document, appearance, target, false));
    EXPECT_EQ(rows->GetChild(0), first);
    EXPECT_EQ(appearance_editor_field_from_name("wings"), AppearanceEditorField::wings);
    EXPECT_EQ(appearance_editor_field_from_name("tail"), AppearanceEditorField::tail);
    EXPECT_FALSE(appearance_editor_field_from_name("unknown"));
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    target = object_workbench_target(view, workspace);
    EXPECT_FALSE(active_appearances_match_tab(appearance, target));
    EXPECT_TRUE(sync_appearance_window(document, appearance, target, true));
    EXPECT_NE(rows->GetInnerRML().find("Waiting for a live Creature or Placeable."), std::string::npos);
    // Inject the next valid generation fact without performing a desktop project reload.
    appearance.color_editor_object = actor->handle();
    appearance.color_editor_channel = 0;
    rebuild_active_appearances(appearance, generation + 1, actor->handle());
    EXPECT_EQ(appearance.appearance_catalog_generation, generation + 1);
    EXPECT_TRUE(appearance.appearance_query.empty());
    EXPECT_FALSE(appearance.appearance_selector_open);
    EXPECT_EQ(appearance.color_editor_channel, -1);
    clear_active_appearances(appearance);
    EXPECT_TRUE(appearance.appearance_matches.empty());
    EXPECT_EQ(appearance.appearance_list.total_rows(), 0);
}

TEST_F(ClientAppearanceView, ColorSelectorsCommitAndUndoAndRejectMissingChannelsOrTabs)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    AppearanceViewState appearance;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    auto target = object_workbench_target(view, workspace);
    auto& runtime = nw::kernel::runtime();
    const auto rows = creature_color_editor_rows(runtime, actor->handle());
    ASSERT_FALSE(rows.empty());
    const auto row = rows.front();
    ASSERT_TRUE(open_color_editor(appearance, actor->handle(), row.color));
    EXPECT_TRUE(active_color_editor_matches_tab(appearance, target));
    std::string markup;
    append_creature_colors_markup(markup, target);
    append_creature_color_selector_markup(markup, appearance, target);
    EXPECT_NE(markup.find("creature_color_palette"), std::string::npos);
    document->SetInnerRML(markup);
    context->Update();
    EXPECT_NE(document->GetElementById("creature_color_palette"), nullptr);
    const auto before = editable_creature_colors(runtime, actor->handle());
    const auto desired = (row.value + 1) % (kPltPaletteColumns * kPltPaletteRows);
    ASSERT_TRUE(commit_active_color_selection(appearance, target, backend, shell, command, desired));
    EXPECT_EQ(workspace.undo_count(), 1);
    const auto after = editable_creature_colors(runtime, actor->handle());
    ASSERT_LT(row.color, after.size());
    EXPECT_EQ(after[row.color], desired);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(editable_creature_colors(runtime, actor->handle()), before);
    EXPECT_FALSE(open_color_editor(appearance, actor->handle(), std::numeric_limits<uint32_t>::max()));
    EXPECT_FALSE(active_color_editor_matches_tab(appearance, target));
    ASSERT_TRUE(open_color_editor(appearance, actor->handle(), row.color));
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    target = object_workbench_target(view, workspace);
    EXPECT_FALSE(commit_active_color_selection(appearance, target, backend, shell, command, desired));
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientAppearanceView, NativeSoundSelectorOwnsSelectionThroughDomReplacementAndUndo)
{
    load_fixture();
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    auto& runtime = nw::kernel::runtime();
    runtime.init_object_propsets(sound->handle());
    activate(sound->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::sounds;
    auto& appearance = view.appearance_view;
    appearance.appearance_selector_open = true;
    appearance.sound_catalog_query = "previous query";
    const auto generation = nw::kernel::resman().generation();
    const auto capture = [&](Rml::Element* hit) {
        return capture_sound_resource_click(hit, appearance, object_workbench_target(view, workspace),
            workspace, backend.module_generation(), generation);
    };
    const auto apply = [&](SoundResourceClick& click) {
        return apply_sound_resource_click(click, appearance, object_workbench_target(view, workspace),
            workspace, backend, shell, command);
    };
    document->SetInnerRML("<button id='sound_resource_add'><span id='target'>Add</span></button>");
    auto click = capture(document->GetElementById("target"));
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, SoundResourceClickKind::open);
    EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
    ASSERT_EQ(apply(*click), SoundResourceClickEffect::opened);
    EXPECT_FALSE(appearance.appearance_selector_open);
    EXPECT_TRUE(appearance.sound_resource_selector_open);
    EXPECT_TRUE(appearance.sound_catalog_query.empty());
    EXPECT_EQ(appearance.sound_catalog_list.scroll_top(), 0);
    EXPECT_EQ(apply(*click), SoundResourceClickEffect::none);

    appearance.sound_catalog_query = "al_pl_whispersm";
    rebuild_sound_catalog(appearance, generation, true);
    ASSERT_EQ(appearance.sound_catalog.status, SoundCatalogStatus::ready);
    ASSERT_EQ(appearance.sound_catalog_matches.size(), 1);
    std::string markup;
    append_sound_resource_selector_markup(markup, appearance);
    document->SetInnerRML(markup);
    context->Update();
    ASSERT_TRUE(sync_sound_catalog_window(document, appearance, object_workbench_target(view, workspace), generation, true));
    Rml::ElementList rows;
    document->GetElementsByClassName(rows, "sound_catalog_row");
    ASSERT_EQ(rows.size(), 1);
    click = capture(rows.front());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->resource.view(), "al_pl_whispersm");
    EXPECT_EQ(click->query, appearance.sound_catalog_query);
    const auto before = snapshot_sound_resources(runtime, sound->handle());
    ASSERT_TRUE(before);
    document->SetInnerRML("<div>Replacement after release</div>");
    ASSERT_EQ(apply(*click), SoundResourceClickEffect::closed);
    EXPECT_FALSE(appearance.sound_resource_selector_open);
    EXPECT_TRUE(appearance.sound_catalog_query.empty());
    const auto after = snapshot_sound_resources(runtime, sound->handle());
    ASSERT_TRUE(after);
    ASSERT_EQ(after->size(), before->size() + 1);
    EXPECT_EQ(after->back().view(), "al_pl_whispersm");
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_EQ(apply(*click), SoundResourceClickEffect::none);
    EXPECT_EQ(workspace.undo_count(), 1);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(snapshot_sound_resources(runtime, sound->handle()), before);

    appearance.sound_resource_selector_open = true;
    appearance.sound_catalog_query = "al_pl_whispersm";
    document->SetInnerRML("<button id='sound_resource_selector_back'><span id='target'>Back</span></button>");
    click = capture(document->GetElementById("target"));
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, SoundResourceClickKind::close);
    ASSERT_EQ(apply(*click), SoundResourceClickEffect::closed);
    EXPECT_FALSE(appearance.sound_resource_selector_open);
    EXPECT_TRUE(appearance.sound_catalog_query.empty());
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientAppearanceView, NativeSoundSelectionRejectsChangedMeaningAndOwners)
{
    load_fixture();
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    auto& runtime = nw::kernel::runtime();
    runtime.init_object_propsets(sound->handle());
    activate(sound->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::sounds;
    auto& appearance = view.appearance_view;
    const auto generation = nw::kernel::resman().generation();
    appearance.sound_resource_selector_open = true;
    rebuild_sound_catalog(appearance, generation, true);
    ASSERT_EQ(appearance.sound_catalog.status, SoundCatalogStatus::ready);
    ASSERT_GT(appearance.sound_catalog.rows.size(), 1);
    ASSERT_NE(appearance.sound_catalog.rows[0].resource, appearance.sound_catalog.rows[1].resource);
    document->SetInnerRML("<div id='target' class='sound_catalog_row' data-key='0'>Sound</div>");
    const auto captured = capture_sound_resource_click(document->GetElementById("target"), appearance,
        object_workbench_target(view, workspace), workspace, backend.module_generation(), generation);
    ASSERT_TRUE(captured);
    ASSERT_EQ(captured->kind, SoundResourceClickKind::select);
    const auto apply = [&](SoundResourceClick& click) {
        return apply_sound_resource_click(click, appearance, object_workbench_target(view, workspace),
            workspace, backend, shell, command);
    };
    const auto before = snapshot_sound_resources(runtime, sound->handle());
    ASSERT_TRUE(before);
    const auto output_count = shell.output_lines.size();
    auto click = *captured;
    std::swap(appearance.sound_catalog.rows[0].resource, appearance.sound_catalog.rows[1].resource);
    EXPECT_EQ(apply(click), SoundResourceClickEffect::none);
    std::swap(appearance.sound_catalog.rows[0].resource, appearance.sound_catalog.rows[1].resource);
    click = *captured;
    appearance.sound_catalog_query = "changed during release";
    EXPECT_EQ(apply(click), SoundResourceClickEffect::none);
    appearance.sound_catalog_query.clear();
    click = *captured;
    ++appearance.sound_catalog_generation;
    EXPECT_EQ(apply(click), SoundResourceClickEffect::none);
    appearance.sound_catalog_generation = generation;
    click = *captured;
    ++click.resource_generation;
    EXPECT_EQ(apply(click), SoundResourceClickEffect::none);
    click = *captured;
    ++click.module_generation;
    EXPECT_EQ(apply(click), SoundResourceClickEffect::none);
    click = *captured;
    appearance.sound_catalog_matches.erase(appearance.sound_catalog_matches.begin());
    EXPECT_EQ(apply(click), SoundResourceClickEffect::none);
    rebuild_sound_catalog(appearance, generation, true);
    EXPECT_EQ(snapshot_sound_resources(runtime, sound->handle()), before);
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_EQ(shell.output_lines.size(), output_count);

    // A real intervening edit changes the existing mutation epoch. The old
    // request cannot append another resource or create another undo/log entry.
    ASSERT_TRUE(commit_sound_catalog_selection(appearance, object_workbench_target(view, workspace), backend, shell, command, 1));
    click = *captured;
    const auto edited = snapshot_sound_resources(runtime, sound->handle());
    const auto edited_output_count = shell.output_lines.size();
    EXPECT_EQ(apply(click), SoundResourceClickEffect::none);
    EXPECT_EQ(snapshot_sound_resources(runtime, sound->handle()), edited);
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_EQ(shell.output_lines.size(), edited_output_count);

    auto* replacement = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(replacement, nullptr);
    runtime.init_object_propsets(replacement->handle());
    activate(replacement->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::sounds;
    appearance.sound_resource_selector_open = true;
    click = *captured;
    click.mutation_epoch = object_mutation_state().epoch;
    EXPECT_EQ(apply(click), SoundResourceClickEffect::none);
    EXPECT_EQ(snapshot_sound_resources(runtime, sound->handle()), edited);
    EXPECT_EQ(workspace.undo_count(), 1);
    activate(sound->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::sounds;
    appearance.sound_resource_selector_open = true;
    rebuild_sound_catalog(appearance, generation, true);
    click = *captured;
    click.mutation_epoch = object_mutation_state().epoch;
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    command.active_tab_id = workspace.active_tab_id();
    EXPECT_EQ(apply(click), SoundResourceClickEffect::none);
    EXPECT_EQ(workspace.undo_count(), 0);
    ASSERT_NE(workspace.find_tab("first"), nullptr);
    EXPECT_EQ(workspace.find_tab("first")->undo_stack.size(), 1);
    EXPECT_EQ(shell.output_lines.size(), edited_output_count);
}

TEST_F(ClientAppearanceView, NativeSoundCaptureRejectsMalformedAndUnavailableControls)
{
    load_fixture();
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    nw::kernel::runtime().init_object_propsets(sound->handle());
    activate(sound->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::sounds;
    auto& appearance = view.appearance_view;
    const auto generation = nw::kernel::resman().generation();
    appearance.sound_resource_selector_open = true;
    rebuild_sound_catalog(appearance, generation, true);
    for (const auto* key : {"-1", "2147483648", "0suffix", "", "2147483647"}) {
        SCOPED_TRACE(key);
        document->SetInnerRML("<button id='target' class='sound_catalog_row' data-key='" + std::string{key} + "'>Sound</button>");
        auto click = capture_sound_resource_click(document->GetElementById("target"), appearance,
            object_workbench_target(view, workspace), workspace, backend.module_generation(), generation);
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, SoundResourceClickKind::none);
        EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
        EXPECT_EQ(apply_sound_resource_click(*click, appearance, object_workbench_target(view, workspace),
                      workspace, backend, shell, command),
            SoundResourceClickEffect::none);
    }
    document->SetInnerRML("<button id='target'>Unrelated</button>");
    EXPECT_FALSE(capture_sound_resource_click(document->GetElementById("target"), appearance,
        object_workbench_target(view, workspace), workspace, backend.module_generation(), generation));
    appearance.sound_resource_selector_open = false;
    document->SetInnerRML("<button id='sound_resource_selector_back'>Back</button>");
    auto click = capture_sound_resource_click(document->GetElementById("sound_resource_selector_back"), appearance,
        object_workbench_target(view, workspace), workspace, backend.module_generation(), generation);
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, SoundResourceClickKind::none);
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientAppearanceView, SoundWindowSelectionAndUndoRejectStaleTabsAndIndices)
{
    load_fixture();
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    auto& runtime = nw::kernel::runtime();
    runtime.init_object_propsets(sound->handle());
    activate(sound->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::sounds;
    AppearanceViewState appearance;
    const auto generation = nw::kernel::resman().generation();
    rebuild_sound_catalog(appearance, generation, true);
    ASSERT_EQ(appearance.sound_catalog.status, SoundCatalogStatus::ready);
    appearance.sound_catalog_query = "al_pl_whispersm";
    appearance.sound_resource_selector_open = true;
    rebuild_sound_catalog(appearance, generation, true);
    ASSERT_EQ(appearance.sound_catalog_matches.size(), 1);
    auto target = object_workbench_target(view, workspace);
    std::string markup;
    append_sound_resource_selector_markup(markup, appearance);
    document->SetInnerRML(markup);
    context->Update();
    EXPECT_TRUE(sync_sound_catalog_window(document, appearance, target, generation, true));
    context->Update();
    EXPECT_FALSE(sync_sound_catalog_window(document, appearance, target, generation, false));
    EXPECT_FALSE(commit_sound_catalog_selection(appearance, target, backend, shell, command, std::numeric_limits<uint32_t>::max()));
    const auto before = snapshot_sound_resources(runtime, sound->handle());
    ASSERT_TRUE(before);
    ASSERT_TRUE(commit_sound_catalog_selection(appearance, target, backend, shell, command, appearance.sound_catalog_matches.front()));
    const auto after = snapshot_sound_resources(runtime, sound->handle());
    ASSERT_TRUE(after);
    EXPECT_EQ(after->size(), before->size() + 1);
    EXPECT_EQ(after->back().view(), "al_pl_whispersm");
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(snapshot_sound_resources(runtime, sound->handle()), before);
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    target = object_workbench_target(view, workspace);
    EXPECT_FALSE(commit_sound_catalog_selection(appearance, target, backend, shell, command, appearance.sound_catalog_matches.front()));
    EXPECT_FALSE(sync_sound_catalog_window(document, appearance, target, generation, true));
    clear_active_sound_catalog(appearance);
    EXPECT_FALSE(appearance.sound_resource_selector_open);
    EXPECT_TRUE(appearance.sound_catalog_matches.empty());
}

TEST_F(ClientAppearanceView, BodyPreviewProviderRestoresEquipmentAndColors)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    ASSERT_TRUE(update_appearance_preview_rows(actor->handle(), true));
    auto& components = nw::kernel::objects().components();
    const auto* visual = components.find_visual(actor->handle());
    ASSERT_NE(visual, nullptr);
    const auto models = visual->models;
    const auto colors = visual->base_plt_colors.data;
    ASSERT_FALSE(models.empty());
    ASSERT_TRUE(update_appearance_preview_rows(actor->handle(), false));
    visual = components.find_visual(actor->handle());
    ASSERT_NE(visual, nullptr);
    EXPECT_FALSE(std::equal(models.begin(), models.end(), visual->models.begin(), visual->models.end(),
        [](const auto& left, const auto& right) {
            return left.model == right.model && left.slot == right.slot
                && left.plt_colors.data == right.plt_colors.data;
        }));
    ASSERT_TRUE(update_appearance_preview_rows(actor->handle(), true));
    visual = components.find_visual(actor->handle());
    ASSERT_NE(visual, nullptr);
    EXPECT_EQ(visual->base_plt_colors.data, colors);
    ASSERT_EQ(visual->models.size(), models.size());
    for (size_t index = 0; index < models.size(); ++index) {
        const auto& actual = visual->models[index];
        const auto& expected = models[index];
        EXPECT_EQ(actual.model, expected.model);
        EXPECT_EQ(actual.plt_texture, expected.plt_texture);
        EXPECT_EQ(actual.attach_to, expected.attach_to);
        EXPECT_EQ(actual.attach_from, expected.attach_from);
        EXPECT_EQ(actual.kind, expected.kind);
        EXPECT_EQ(actual.slot, expected.slot);
        EXPECT_EQ(actual.part, expected.part);
        EXPECT_EQ(actual.source_part, expected.source_part);
        EXPECT_EQ(actual.model_part, expected.model_part);
        EXPECT_EQ(actual.plt_color_mask, expected.plt_color_mask);
        EXPECT_EQ(actual.flags, expected.flags);
        EXPECT_EQ(actual.plt_colors.data, expected.plt_colors.data);
    }
    EXPECT_FALSE(update_appearance_preview_rows(nw::ObjectHandle{}, false));
}

class ClientWorkspaceContent : public ClientBrowserWorkspace { };

TEST_F(ClientWorkspaceContent, ResourceInspectorAndDialogAcquisitionUseCurrentProjectAndTab)
{
    KernelServiceScope services;
    const std::filesystem::path task_root{"tmp/client_workspace_content"};
    const auto root = task_root / "project";
    std::filesystem::remove_all(task_root);
    ASSERT_TRUE(initialize_project(root, "Fixture").ok);
    const std::filesystem::path relative{"shared/blueprints/creatures/pl_agent_001.utc.json"};
    std::filesystem::create_directories((root / relative).parent_path());
    std::filesystem::copy_file("test_data/user/development/pl_agent_001.utc.json", root / relative);
    WorkspaceTab tab;
    tab.id = "resource:fixture";
    tab.kind = WorkspaceTabKind::resource;
    tab.detail = relative.generic_string();
    const auto resource = resource_document_for_tab(root, tab);
    ASSERT_TRUE(resource);
    ASSERT_TRUE(resource->ok) << resource->message;
    EXPECT_EQ(resource->title, "Agent");
    std::string markup;
    append_resource_document_inspector(markup, *resource);
    EXPECT_NE(markup.find("Agent"), std::string::npos);
    document->SetInnerRML(markup);
    context->Update();
    Rml::ElementList titles;
    document->GetElementsByClassName(titles, "resource_inspector_title");
    ASSERT_EQ(titles.size(), 1);
    EXPECT_EQ(titles.front()->GetInnerRML(), "Agent");
    EXPECT_FALSE(resource_document_for_tab({}, tab));
    {
        std::ofstream outside_file{task_root / "outside.txt"};
        ASSERT_TRUE(outside_file);
        outside_file << "outside\n";
    }
    tab.detail = "../outside.txt";
    const auto outside = resource_document_for_tab(root, tab);
    ASSERT_TRUE(outside);
    EXPECT_FALSE(outside->ok);
    EXPECT_NE(outside->message.find("outside the current project"), std::string::npos);
    tab.kind = WorkspaceTabKind::home;
    EXPECT_FALSE(resource_document_for_tab(root, tab));
    markup.clear();
    append_missing_resource_document(markup);
    EXPECT_NE(markup.find("No project resource selected"), std::string::npos);

    std::filesystem::create_directories(root / "dialogs");
    std::filesystem::copy_file("test_data/user/development/alue_ranger.dlg", root / "dialogs/alue_ranger.dlg");
    tab.kind = WorkspaceTabKind::dialog;
    tab.id = "dialog:fixture";
    tab.detail = "dialogs/alue_ranger.dlg";
    DialogViewState dialog;
    ensure_active_dialog_document(dialog, root, &tab);
    ASSERT_EQ(dialog.document.status, DialogDocumentStatus::ready) << dialog.document.diagnostic;
    ASSERT_GT(dialog.document.rows.size(), 1);
    ASSERT_TRUE(select_dialog_view_row(dialog, 1));
    const auto* rows = dialog.document.rows.data();
    ensure_active_dialog_document(dialog, root, &tab);
    EXPECT_EQ(dialog.document.rows.data(), rows);
    EXPECT_EQ(dialog.list.selected(), 1);
    document->SetInnerRML(dialog_view_markup(dialog));
    context->Update();
    EXPECT_TRUE(sync_dialog_view(document, dialog, true));
    context->Update();
    EXPECT_FALSE(sync_dialog_view(document, dialog, false));
    tab.detail = "dialogs/missing.dlg";
    ensure_active_dialog_document(dialog, root, &tab);
    EXPECT_NE(dialog.document.status, DialogDocumentStatus::ready);
    EXPECT_NE(dialog.document.status, DialogDocumentStatus::empty);
    EXPECT_FALSE(dialog.document.diagnostic.empty());
    tab.kind = WorkspaceTabKind::home;
    ensure_active_dialog_document(dialog, root, &tab);
    EXPECT_TRUE(dialog.tab_id.empty());
    EXPECT_TRUE(dialog.document.rows.empty());
    EXPECT_EQ(dialog.document.status, DialogDocumentStatus::empty);
}
