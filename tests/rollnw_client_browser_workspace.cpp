#include "appearance_view.hpp"
#include "area_object_editor.hpp"
#include "area_tile_editor.hpp"
#include "browser_view.hpp"
#include "client_input.hpp"
#include "client_preferences.hpp"
#include "creature_body_part_editor.hpp"
#include "creature_workbench_view.hpp"
#include "dialog_view.hpp"
#include "editor_input.hpp"
#include "inventory_workbench_view.hpp"
#include "loading_view.hpp"
#include "object_edits.hpp"
#include "object_workbench_view.hpp"
#include "script_commands.hpp"
#include "shell_view.hpp"
#include "smalls_rmlui.hpp"
#include "workspace_view.hpp"

#include <nw/kernel/Rules.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/objects/Area.hpp>
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
#include <nw/profiles/nwn1/toolset_visual.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>
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
    Rml::ElementList area_cards;
    home_list->GetElementsByClassName(area_cards, "home_area_card");
    ASSERT_FALSE(area_cards.empty());
    auto area_click = capture_home_workspace_click(area_cards.front(), browser, backend);
    ASSERT_TRUE(area_click);
    ASSERT_EQ(area_click->kind, HomeWorkspaceClickKind::select_area);
    auto stale_area = *area_click;
    ++stale_area.area_generation;
    EXPECT_EQ(consume_home_workspace_click(stale_area, browser, backend), HomeWorkspaceClickKind::none);
    EXPECT_EQ(consume_home_workspace_click(*area_click, browser, backend), HomeWorkspaceClickKind::select_area);
    EXPECT_EQ(consume_home_workspace_click(*area_click, browser, backend), HomeWorkspaceClickKind::none);
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

TEST_F(ClientBrowserWorkspace, BrowserClicksKeepReleaseOrderAndOwnCurrentResourceIdentities)
{
    KernelServiceScope services;
    const auto root = std::filesystem::path{"tmp/client_browser_workspace"}
        / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(root);
    ASSERT_TRUE(import_module_project("test_data/user/modules/DockerDemo.mod", root, {ProjectImportFormat::json}).ok);
    std::filesystem::create_directories(root / "shared/creatures");
    std::filesystem::copy_file("test_data/user/development/pl_agent_001.utc.json",
        root / "shared/creatures/pl_agent_001.utc.json");
    RmlSmallsBridge bridge;
    WorkspaceState workspace;
    ShellController shell;
    ToolsetBackend backend;
    backend.bind(&bridge, &shell, &workspace);
    const auto unbind = create_scope_exit([] { script_command_host().bind(nullptr, nullptr); });
    ASSERT_TRUE(backend.open_project(root.string()).ok());
    shell.set_showing_project_tree(true);
    BrowserViewState browser;
    refresh_browser_view(document, browser, backend, shell, true, false);
    context->Update();
    auto* list = document->GetElementById("recent_list");
    ASSERT_NE(list, nullptr);
    const auto container = std::ranges::find_if(browser.project_rows,
        [](const ProjectTreeRow& row) { return row.node.is_container(); });
    ASSERT_NE(container, browser.project_rows.end());
    const auto container_index = static_cast<int32_t>(container - browser.project_rows.begin());
    const auto container_id = container->node.id;
    const auto materialize = [&](int32_t index) -> Rml::Element* {
        list->SetScrollTop(static_cast<float>(index) * 26);
        (void)render_project_tree_window(document, browser, true);
        context->Update();
        Rml::ElementList rows;
        list->GetElementsByClassName(rows, "recent_item");
        const auto found = std::ranges::find_if(rows, [&](Rml::Element* row) { return client_row_key(row) == index; });
        return found != rows.end() ? *found : nullptr;
    };
    const auto press = [&](Rml::Element* row) {
        context->ProcessMouseMove(static_cast<int>(row->GetAbsoluteLeft() + row->GetClientWidth() / 2),
            static_cast<int>(row->GetAbsoluteTop() + row->GetClientHeight() / 2), 0);
        context->ProcessMouseButtonDown(0, 0);
        browser.pressed_recent_index = client_row_key(row).value_or(-1);
    };
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    auto* row = materialize(container_index);
    ASSERT_NE(row, nullptr);
    press(row);
    auto folder = prepare_browser_row_click(document, row, browser, backend, shell, false);
    EXPECT_EQ(folder.kind, BrowserRowClickKind::none);
    EXPECT_TRUE(browser.collapsed_project_nodes.contains(container_id));
    EXPECT_EQ(browser.selected_recent_index, -1);
    EXPECT_EQ(browser.pressed_recent_index, -1);
    ClientInputDispatchState folder_dispatch;
    ASSERT_TRUE(forward_client_input(folder_dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, nullptr, event)
            .performed);

    browser.collapsed_project_nodes.clear();
    refresh_browser_view(document, browser, backend, shell, true, false);
    context->Update();
    const auto actor = std::ranges::find_if(browser.project_rows, [](const ProjectTreeRow& source_row) {
        return !source_row.node.is_container()
            && nw::Resource::from_path(source_row.node.relative_path, false).type == nw::ResourceType::utc;
    });
    ASSERT_NE(actor, browser.project_rows.end());
    const auto actor_index = static_cast<int32_t>(actor - browser.project_rows.begin());
    const auto actor_path = actor->node.relative_path;
    row = materialize(actor_index);
    ASSERT_NE(row, nullptr);
    press(row);
    row->SetAttribute("data-key", "0tail");
    EXPECT_EQ(prepare_browser_row_click(document, row, browser, backend, shell, false).kind, BrowserRowClickKind::none);
    row->SetAttribute("data-key", std::to_string(actor_index));
    auto click = prepare_browser_row_click(document, row, browser, backend, shell, false);
    ASSERT_EQ(click.kind, BrowserRowClickKind::open_resource);
    EXPECT_EQ(browser.selected_recent_index, actor_index);
    const auto check_stale = [&](auto change) {
        auto stale = click;
        change(stale);
        EXPECT_EQ(consume_browser_row_click(stale, browser, backend, shell, false), BrowserRowClickKind::none);
        EXPECT_EQ(stale.kind, BrowserRowClickKind::none);
    };
    check_stale([](BrowserRowClick& stale) { ++stale.module_generation; });
    check_stale([](BrowserRowClick& stale) { ++stale.resource_generation; });
    check_stale([](BrowserRowClick& stale) { stale.query += "changed"; });
    check_stale([](BrowserRowClick& stale) { stale.row_id += "changed"; });
    check_stale([](BrowserRowClick& stale) { stale.project_dir /= "changed"; });
    check_stale([](BrowserRowClick& stale) { stale.index = std::numeric_limits<int32_t>::max(); });
    class ReleaseRecorder final : public Rml::EventListener {
    public:
        explicit ReleaseRecorder(Rml::Element& source_list)
            : list{source_list}
        {
        }
        void ProcessEvent(Rml::Event&) override
        {
            ++calls;
            list.SetInnerRML("<p>Replacement during release</p>");
        }
        Rml::Element& list;
        int calls = 0;
    } recorder{*list};
    context->AddEventListener("mouseup", &recorder);
    ClientInputDispatchState dispatch;
    ASSERT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::before_native, context, nullptr, event)
            .performed);
    context->RemoveEventListener("mouseup", &recorder);
    EXPECT_EQ(recorder.calls, 1);
    EXPECT_EQ(consume_browser_row_click(click, browser, backend, shell, false), BrowserRowClickKind::open_resource);
    CommandContext command{.active_tab_id = workspace.active_tab_id(), .source = CommandSource::widget, .workspace = &workspace};
    const auto resource_argument = click.resource_path.generic_string();
    ASSERT_TRUE(backend.execute_command("toolset.open_resource", {resource_argument}, command).ok());
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_EQ(workspace.active_tab()->detail, actor_path.generic_string());
    EXPECT_EQ(consume_browser_row_click(click, browser, backend, shell, false), BrowserRowClickKind::none);
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, nullptr, event)
            .performed);

    row = materialize(actor_index);
    ASSERT_NE(row, nullptr);
    press(row);
    auto preview = prepare_browser_row_click(document, row, browser, backend, shell, true);
    ASSERT_EQ(preview.kind, BrowserRowClickKind::preview_actor);
    const auto saved = load_project_preview_settings(backend.current_project_dir());
    ASSERT_TRUE(saved.ok) << saved.message;
    EXPECT_EQ(saved.test_actor, actor_path);
    ClientInputDispatchState preview_dispatch;
    ASSERT_TRUE(forward_client_input(preview_dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::before_native, context, nullptr, event)
            .performed);
    EXPECT_EQ(consume_browser_row_click(preview, browser, backend, shell, false), BrowserRowClickKind::none);

    shell.set_showing_areas(true);
    refresh_browser_view(document, browser, backend, shell, true, false);
    context->Update();
    Rml::ElementList area_rows;
    list->GetElementsByClassName(area_rows, "recent_item");
    ASSERT_FALSE(area_rows.empty());
    row = area_rows.front();
    press(row);
    auto area = prepare_browser_row_click(document, row, browser, backend, shell, false);
    ASSERT_EQ(area.kind, BrowserRowClickKind::select_area);
    ClientInputDispatchState area_dispatch;
    ASSERT_TRUE(forward_client_input(area_dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::before_native, context, nullptr, event)
            .performed);
    EXPECT_EQ(consume_browser_row_click(area, browser, backend, shell, false), BrowserRowClickKind::select_area);
    ASSERT_TRUE(backend.execute_command("toolset.select_area", {area.area_resref}, command).ok());
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_EQ(workspace.active_tab()->kind, WorkspaceTabKind::area);
}

TEST_F(ClientBrowserWorkspace, HomeProjectClicksOwnCurrentRowsAndPreferenceFailureRestoresHistory)
{
    KernelServiceScope services;
    const auto root = std::filesystem::path{"tmp/client_browser_workspace"}
        / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::remove_all(root);
    ASSERT_TRUE(initialize_project(root, "Home fixture").ok);
    BrowserViewState browser;
    browser.recent_projects = {{project_display_name(root), std::filesystem::absolute(root).string()},
        {"Missing fixture", std::filesystem::absolute(root / "missing").string()}};
    ToolsetBackend backend;
    auto* content = document->GetElementById("workspace_content");
    ASSERT_NE(content, nullptr);
    content->SetProperty("position", "absolute");
    content->SetProperty("left", "400px");
    content->SetProperty("top", "50px");
    content->SetProperty("width", "700px");
    content->SetProperty("height", "500px");
    const auto render = [&] {
        content->SetInnerRML(recent_projects_markup(browser.recent_projects));
        context->Update();
    };
    const auto control = [&](const char* class_name) {
        Rml::ElementList rows;
        content->GetElementsByClassName(rows, class_name);
        return rows.empty() ? nullptr : rows.front();
    };
    render();
    auto* row = control("home_project_item");
    ASSERT_NE(row, nullptr);
    context->ProcessMouseMove(static_cast<int>(row->GetAbsoluteLeft() + row->GetClientWidth() / 2),
        static_cast<int>(row->GetAbsoluteTop() + row->GetClientHeight() / 2), 0);
    context->ProcessMouseButtonDown(0, 0);
    auto open = capture_home_workspace_click(row, browser, backend);
    ASSERT_TRUE(open);
    ASSERT_EQ(open->kind, HomeWorkspaceClickKind::open_project);
    EXPECT_TRUE(open->project.error.empty());
    EXPECT_FALSE(browser.recent_projects[1].error.empty());
    class ReleaseRecorder final : public Rml::EventListener {
    public:
        explicit ReleaseRecorder(Rml::Element& source_content)
            : content{source_content}
        {
        }
        void ProcessEvent(Rml::Event&) override
        {
            ++calls;
            content.SetInnerRML("<p>Replacement during release</p>");
        }
        Rml::Element& content;
        int calls = 0;
    } recorder{*content};
    context->AddEventListener("mouseup", &recorder);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    ClientInputDispatchState dispatch;
    ASSERT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::before_native, context, nullptr, event)
            .performed);
    context->RemoveEventListener("mouseup", &recorder);
    EXPECT_EQ(recorder.calls, 1);
    EXPECT_EQ(consume_home_workspace_click(*open, browser, backend), HomeWorkspaceClickKind::open_project);
    EXPECT_EQ(open->project.path, std::filesystem::absolute(root).string());
    EXPECT_EQ(consume_home_workspace_click(*open, browser, backend), HomeWorkspaceClickKind::none);

    render();
    row = control("home_project_remove");
    ASSERT_NE(row, nullptr);
    auto remove = capture_home_workspace_click(row, browser, backend);
    ASSERT_TRUE(remove);
    ASSERT_EQ(remove->kind, HomeWorkspaceClickKind::remove_project);
    std::swap(browser.recent_projects[0], browser.recent_projects[1]);
    EXPECT_EQ(consume_home_workspace_click(*remove, browser, backend), HomeWorkspaceClickKind::none);
    std::swap(browser.recent_projects[0], browser.recent_projects[1]);
    for (const char* key : {"0tail", "+0", "-1", "2147483648", "17"}) {
        row->SetAttribute("data-key", key);
        auto invalid = capture_home_workspace_click(row, browser, backend);
        ASSERT_TRUE(invalid);
        EXPECT_EQ(invalid->kind, HomeWorkspaceClickKind::none);
        EXPECT_EQ(consume_home_workspace_click(*invalid, browser, backend), HomeWorkspaceClickKind::none);
    }
    row->SetAttribute("data-key", "0");
    remove = capture_home_workspace_click(row, browser, backend);
    ASSERT_TRUE(remove);
    EXPECT_EQ(consume_home_workspace_click(*remove, browser, backend), HomeWorkspaceClickKind::remove_project);
    const auto before = browser.recent_projects;
    DockLayout docks;
    const std::array indices{size_t{0}};
    EXPECT_EQ(forget_recent_project_preferences(root / "rollnw.json/preferences.json", docks,
                  browser.recent_projects, indices),
        RecentProjectForgetStatus::save_failed);
    ASSERT_EQ(browser.recent_projects.size(), before.size());
    for (size_t index = 0; index < before.size(); ++index) {
        EXPECT_EQ(std::tie(browser.recent_projects[index].name, browser.recent_projects[index].path, browser.recent_projects[index].error),
            std::tie(before[index].name, before[index].path, before[index].error));
    }
    EXPECT_EQ(forget_recent_project_preferences(root / "preferences.json", docks,
                  browser.recent_projects, indices),
        RecentProjectForgetStatus::saved);
    ASSERT_EQ(browser.recent_projects.size(), 1u);
    EXPECT_EQ(browser.recent_projects.front().path, before[1].path);
    std::vector<RecentProjectEntry> restored;
    load_ui_preferences(root / "preferences.json", docks, restored);
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored.front().path, before[1].path);
    const std::array invalid_indices{size_t{0}, size_t{2}};
    EXPECT_EQ(forget_recent_project_preferences(root / "preferences.json", docks,
                  browser.recent_projects, invalid_indices),
        RecentProjectForgetStatus::rejected);
    EXPECT_EQ(browser.recent_projects.size(), 1u);
    EXPECT_EQ(forget_recent_project_preferences(root / "unused.json", docks,
                  browser.recent_projects, {}),
        RecentProjectForgetStatus::saved);
    EXPECT_FALSE(std::filesystem::exists(root / "unused.json"));
    EXPECT_TRUE(is_project_directory(root));
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

TEST_F(ClientBrowserWorkspace, MalformedTabTargetIndicesCannotAliasMovableRows)
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
    Rml::Element* first = nullptr;
    for (int i = 0; i < track->GetNumChildren(); ++i) {
        auto* child = track->GetChild(i);
        if (child->GetAttribute<Rml::String>("data-tab", "") == "first") { first = child; }
    }
    ASSERT_NE(first, nullptr);
    const auto first_index = workspace_tab_current_index(workspace.tabs(), "first", 0);
    const auto second_index = workspace_tab_current_index(workspace.tabs(), "second", 0);
    ASSERT_LT(first_index, second_index);
    const auto valid = std::to_string(first_index);
    EXPECT_EQ(workspace_tab_target_index_at_point(document, {-100, 0}, workspace.tabs(), "second", second_index), first_index);
    for (const auto& key : {valid + "tail", "+" + valid, " " + valid, std::string{"-1"}, std::string{"184467440737095516160"}}) {
        SCOPED_TRACE(key);
        first->SetAttribute("data-index", key);
        EXPECT_EQ(workspace_tab_target_index_at_point(document, {-100, 0}, workspace.tabs(), "second", second_index), second_index);
    }
    first->SetAttribute("data-index", valid);
    EXPECT_EQ(workspace_tab_target_index_at_point(document, {-100, 0}, workspace.tabs(), "second", second_index), first_index);
}

TEST_F(ClientBrowserWorkspace, NativeTabCommandsOwnIdsAcrossReleaseAndKeepDirtyClosePrompt)
{
    WorkspaceState workspace;
    workspace.open_tab("first", "First", WorkspaceTabKind::generic);
    workspace.open_tab("second", "Second", WorkspaceTabKind::generic);
    ShellController shell;
    ToolsetBackend backend;
    backend.bind(nullptr, &shell, &workspace);
    const auto unbind = create_scope_exit([&] { script_command_host().bind(nullptr, nullptr); });
    WorkspaceViewState view;
    refresh_workspace_tabs(document, view, workspace);
    context->Update();
    Rml::ElementList tabs;
    document->GetElementsByClassName(tabs, "workspace_tab");
    auto target = std::ranges::find_if(tabs, [](Rml::Element* element) {
        return element->GetAttribute<Rml::String>("data-tab", "") == "first";
    });
    ASSERT_NE(target, tabs.end());
    context->ProcessMouseMove(static_cast<int>((*target)->GetAbsoluteLeft() + (*target)->GetClientWidth() / 2),
        static_cast<int>((*target)->GetAbsoluteTop() + (*target)->GetClientHeight() / 2), 0);
    context->ProcessMouseButtonDown(0, 0);
    auto click = capture_workspace_tab_click(document, *target, {-100, -100}, workspace);
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, WorkspaceTabClickKind::activate);
    class ReleaseRecorder final : public Rml::EventListener {
    public:
        ReleaseRecorder(WorkspaceState& source_workspace, Rml::ElementDocument& source_document)
            : workspace{source_workspace}
            , document{source_document}
        {
        }
        void ProcessEvent(Rml::Event&) override
        {
            ++calls;
            EXPECT_EQ(workspace.active_tab_id(), "second");
            document.SetInnerRML("<p>Replacement during release</p>");
        }
        WorkspaceState& workspace;
        Rml::ElementDocument& document;
        int calls = 0;
    } recorder{workspace, *document};
    context->AddEventListener("mouseup", &recorder);
    const auto remove = create_scope_exit([&] { context->RemoveEventListener("mouseup", &recorder); });
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    ClientInputDispatchState dispatch;
    ASSERT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset, ClientRmlForwardPhase::before_native,
        context, nullptr, event)
            .performed);
    EXPECT_EQ(recorder.calls, 1);
    auto invocation = take_workspace_tab_click_command(*click, workspace);
    ASSERT_TRUE(invocation);
    CommandContext command{.active_tab_id = workspace.active_tab_id(), .source = CommandSource::widget, .workspace = &workspace};
    ASSERT_TRUE(backend.execute_command(std::move(*invocation), command).ok());
    EXPECT_EQ(workspace.active_tab_id(), "first");
    EXPECT_FALSE(take_workspace_tab_click_command(*click, workspace));
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset, ClientRmlForwardPhase::before_native,
        context, nullptr, event)
            .performed);
    EXPECT_FALSE(sync_workspace_tab_click(document, view, workspace, WorkspaceTabClickKind::activate, "first"));
    document->SetInnerRML("<div id='workspace_tabs'><div id='workspace_tab_track'></div></div>");
    refresh_workspace_tabs(document, view, workspace);
    context->Update();
    Rml::ElementList closes;
    document->GetElementsByClassName(closes, "workspace_tab_close");
    target = std::ranges::find_if(closes, [](Rml::Element* element) {
        return element->GetAttribute<Rml::String>("data-tab", "") == "first";
    });
    ASSERT_NE(target, closes.end());
    click = capture_workspace_tab_click(document, *target, {-100, -100}, workspace);
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, WorkspaceTabClickKind::close);
    // Dirty state changes after capture and stays backend-owned.
    ASSERT_TRUE(workspace.set_tab_dirty("first", true));
    invocation = take_workspace_tab_click_command(*click, workspace);
    ASSERT_TRUE(invocation);
    auto result = backend.execute_command(std::move(*invocation), command);
    ASSERT_TRUE(result.prompt);
    EXPECT_EQ(result.prompt->id, "workspace.close_tab.save");
    ASSERT_EQ(result.prompt->actions.size(), 3);
    EXPECT_EQ(result.prompt->actions[0].id, "save");
    EXPECT_EQ(result.prompt->actions[1].id, "discard");
    EXPECT_EQ(result.prompt->actions[2].id, "cancel");
    EXPECT_NE(workspace.find_tab("first"), nullptr);
    ASSERT_TRUE(workspace.set_tab_dirty("first", false));
    click = capture_workspace_tab_click(document, *target, {-100, -100}, workspace);
    ASSERT_TRUE(click);
    invocation = take_workspace_tab_click_command(*click, workspace);
    ASSERT_TRUE(invocation);
    ASSERT_TRUE(backend.execute_command(std::move(*invocation), command).ok());
    EXPECT_EQ(workspace.find_tab("first"), nullptr);
    EXPECT_TRUE(sync_workspace_tab_click(document, view, workspace, WorkspaceTabClickKind::close, "first"));
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientBrowserWorkspace, TabDragArmsRealControlsAndReordersCurrentRowsOnce)
{
    KernelServiceScope kernel;
    WorkspaceState workspace;
    workspace.ensure_default_tabs();
    workspace.open_tab("first", "First", WorkspaceTabKind::generic);
    workspace.open_tab("second", "Second", WorkspaceTabKind::generic);
    WorkspaceViewState view;
    ToolsetBackend backend;
    RmlSmallsBridge bridge;
    ShellController shell;
    backend.bind(&bridge, &shell, &workspace);
    CommandContext command{.active_tab_id = workspace.active_tab_id(), .source = CommandSource::widget, .workspace = &workspace};
    refresh_workspace_tabs(document, view, workspace);
    context->Update();
    auto* track = document->GetElementById("workspace_tab_track");
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->GetNumChildren(), 4);
    auto* second = track->GetChild(3);
    ASSERT_EQ(second->GetAttribute<Rml::String>("data-tab", ""), "second");
    const Rml::Vector2f start{second->GetAbsoluteLeft() + second->GetOffsetWidth() / 2,
        second->GetAbsoluteTop() + second->GetOffsetHeight() / 2};
    ASSERT_TRUE(begin_workspace_tab_drag(document, view, second, start));
    EXPECT_EQ(view.workspace_tab_drag_id, "second");
    EXPECT_FALSE(view.workspace_tab_dragging);
    auto update = update_workspace_tab_drag(document, view, workspace, {start.x + 4, start.y});
    EXPECT_FALSE(update.handled);
    EXPECT_FALSE(update.command);
    update = update_workspace_tab_drag(document, view, workspace, {-100, start.y});
    ASSERT_TRUE(update.handled);
    ASSERT_TRUE(update.command);
    ASSERT_TRUE(backend.execute_command(std::move(*update.command), command).ok());
    ASSERT_EQ(workspace.tabs().size(), 4);
    EXPECT_EQ(workspace.tabs()[2].id, "second");
    EXPECT_FALSE(workspace.tabs()[0].movable);
    EXPECT_FALSE(workspace.tabs()[1].movable);
    refresh_workspace_tabs(document, view, workspace);
    context->Update();
    update = update_workspace_tab_drag(document, view, workspace, {-100, start.y});
    EXPECT_TRUE(update.handled);
    EXPECT_FALSE(update.command);
    for (int index = 0; index < 8; ++index) {
        workspace.open_tab("extra" + std::to_string(index), "An additional preview with a long title", WorkspaceTabKind::generic);
    }
    refresh_workspace_tabs(document, view, workspace);
    context->Update();
    auto* strip = document->GetElementById("workspace_tabs");
    ASSERT_NE(strip, nullptr);
    ASSERT_GT(strip->GetScrollWidth(), strip->GetClientWidth());
    const auto before_scroll = strip->GetScrollLeft();
    update = update_workspace_tab_drag(document, view, workspace,
        {strip->GetAbsoluteLeft() + strip->GetClientWidth() - 1, start.y});
    EXPECT_TRUE(update.handled);
    EXPECT_GT(view.workspace_tab_scroll_x, before_scroll);
    EXPECT_LE(view.workspace_tab_scroll_x, strip->GetScrollWidth() - strip->GetClientWidth());
    ASSERT_TRUE(workspace.close_tab("second"));
    update = update_workspace_tab_drag(document, view, workspace, {-100, start.y});
    EXPECT_TRUE(update.handled);
    EXPECT_FALSE(update.command);
    Rml::ElementList closes;
    document->GetElementsByClassName(closes, "workspace_tab_close");
    ASSERT_FALSE(closes.empty());
    EXPECT_FALSE(begin_workspace_tab_drag(document, view, closes.front(), start));
    EXPECT_TRUE(view.workspace_tab_drag_id.empty());
    view.workspace_tab_drag_id = "first";
    view.workspace_tab_dragging = true;
    update = update_workspace_tab_drag(document, view, workspace, {std::numeric_limits<float>::quiet_NaN(), 0});
    EXPECT_FALSE(update.handled);
    EXPECT_FALSE(update.command);
    EXPECT_TRUE(view.workspace_tab_drag_id.empty());
    EXPECT_FALSE(view.workspace_tab_dragging);
    EXPECT_FALSE(begin_workspace_tab_drag(document, view, nullptr, {0, std::numeric_limits<float>::infinity()}));
}

TEST_F(ClientBrowserWorkspace, NativeSubtabsAndStaleTargetIdentityUseSemanticIds)
{
    WorkspaceState workspace;
    workspace.open_or_replace_tab("first", "First", WorkspaceTabKind::generic, "first-source");
    ASSERT_NE(workspace.open_subtab("first", "one", "One"), nullptr);
    ASSERT_NE(workspace.open_subtab("first", "two", "Two"), nullptr);
    ShellController shell;
    ToolsetBackend backend;
    backend.bind(nullptr, &shell, &workspace);
    const auto unbind = create_scope_exit([&] { script_command_host().bind(nullptr, nullptr); });
    std::string markup;
    append_workspace_subtabs_markup(markup, *workspace.active_tab());
    document->SetInnerRML(markup);
    Rml::ElementList tabs;
    document->GetElementsByClassName(tabs, "workspace_subtab");
    ASSERT_EQ(tabs.size(), 2);
    auto click = capture_workspace_tab_click(document, tabs.front(), {-100, -100}, workspace);
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, WorkspaceTabClickKind::activate_subtab);
    const auto captured = *click;
    workspace.find_tab("first")->detail = "changed-source";
    EXPECT_FALSE(take_workspace_tab_click_command(*click, workspace));
    workspace.find_tab("first")->detail = "first-source";
    click = captured;
    document->SetInnerRML("<p>Replacement</p>");
    auto invocation = take_workspace_tab_click_command(*click, workspace);
    ASSERT_TRUE(invocation);
    CommandContext command{.active_tab_id = workspace.active_tab_id(), .source = CommandSource::widget, .workspace = &workspace};
    ASSERT_TRUE(backend.execute_command(std::move(*invocation), command).ok());
    ASSERT_NE(workspace.active_subtab(), nullptr);
    EXPECT_EQ(workspace.active_subtab()->id, "one");
    markup.clear();
    append_workspace_subtabs_markup(markup, *workspace.active_tab());
    document->SetInnerRML(markup);
    Rml::ElementList closes;
    document->GetElementsByClassName(closes, "workspace_subtab_close");
    ASSERT_EQ(closes.size(), 2);
    click = capture_workspace_tab_click(document, closes.front(), {-100, -100}, workspace);
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, WorkspaceTabClickKind::close_subtab);
    invocation = take_workspace_tab_click_command(*click, workspace);
    ASSERT_TRUE(invocation);
    ASSERT_TRUE(backend.execute_command(std::move(*invocation), command).ok());
    EXPECT_EQ(workspace.find_tab("first")->subtabs.size(), 1);
    EXPECT_EQ(workspace.find_tab("first")->subtabs.front().id, "two");
    click = captured;
    EXPECT_FALSE(take_workspace_tab_click_command(*click, workspace));
    // Capture a real existing subtab, then switch the displayed owner.
    markup.clear();
    append_workspace_subtabs_markup(markup, *workspace.active_tab());
    document->SetInnerRML(markup);
    tabs.clear();
    document->GetElementsByClassName(tabs, "workspace_subtab");
    ASSERT_EQ(tabs.size(), 1);
    click = capture_workspace_tab_click(document, tabs.front(), {-100, -100}, workspace);
    ASSERT_TRUE(click);
    workspace.open_tab("second", "Second", WorkspaceTabKind::generic);
    EXPECT_FALSE(take_workspace_tab_click_command(*click, workspace));
    document->SetInnerRML("<button id='target' class='workspace_subtab' data-tab='first' data-subtab='missing'>Missing</button>");
    click = capture_workspace_tab_click(document, document->GetElementById("target"), {-100, -100}, workspace);
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, WorkspaceTabClickKind::none);
    EXPECT_FALSE(take_workspace_tab_click_command(*click, workspace));
}

TEST_F(ClientBrowserWorkspace, NativeScrollCapturesDirectionAndUsesFreshLayoutForBothStrips)
{
    for (const auto& strip : {kWorkspaceTabScrollStrip, kObjectWorkbenchTabScrollStrip}) {
        SCOPED_TRACE(strip.viewport_id);
        const auto button_class = std::string{strip.viewport_id} == "workspace_tabs"
            ? "workspace_tab_scroll_button"
            : "object_workbench_tab_scroll_button";
        const auto render = [&](int width, bool disabled) {
            document->SetInnerRML("<button id='" + std::string{strip.next_id} + "' class='" + button_class
                + (disabled ? " disabled" : "") + "'>Next</button><div id='" + strip.viewport_id
                + "' class='" + strip.viewport_id + "' style='position:absolute;left:0px;top:60px;display:block;height:50px;width:" + std::to_string(width) + "px;overflow-x:auto;'><div id='" + strip.track_id
                + "' class='" + strip.track_id + "' style='width:600px;'><span class='" + strip.tab_class + "' style='display:inline-block;width:200px;'>One</span>"
                + "<span class='" + strip.tab_class + "' style='display:inline-block;width:200px;'>Two</span>"
                + "<span class='" + strip.tab_class + "' style='display:inline-block;width:200px;'>Three</span></div></div>");
            context->Update();
        };
        render(100, false);
        const auto old_target = tab_scroll_target(document, strip, true);
        auto click = capture_tab_scroll_click(document->GetElementById(strip.next_id), strip, button_class);
        ASSERT_TRUE(click);
        EXPECT_TRUE(click->enabled);
        EXPECT_TRUE(click->forward);
        render(250, false);
        const auto expected = tab_scroll_target(document, strip, true);
        EXPECT_NE(expected, old_target);
        float scroll = 0;
        EXPECT_TRUE(apply_tab_scroll_click(*click, document, strip, scroll));
        EXPECT_EQ(scroll, expected);
        EXPECT_GT(scroll, 0);
        EXPECT_FALSE(apply_tab_scroll_click(*click, document, strip, scroll));
        render(100, true);
        click = capture_tab_scroll_click(document->GetElementById(strip.next_id), strip, button_class);
        ASSERT_TRUE(click);
        EXPECT_FALSE(apply_tab_scroll_click(*click, document, strip, scroll));
        EXPECT_EQ(scroll, expected);
    }
}

TEST_F(ClientBrowserWorkspace, NativeDialogSelectionPrecedesReleaseAndMalformedControlsStillRelease)
{
    WorkspaceState workspace;
    workspace.open_or_replace_tab("dialog", "Dialog", WorkspaceTabKind::dialog, "test_data/user/development/alue_ranger.dlg");
    DialogViewState view;
    load_dialog_view(view, workspace.active_tab()->detail, workspace.active_tab_id());
    ASSERT_EQ(view.document.status, DialogDocumentStatus::ready);
    ASSERT_GT(view.document.rows.size(), 1);
    document->SetInnerRML(dialog_view_markup(view));
    context->Update();
    ASSERT_TRUE(sync_dialog_view(document, view, true));
    Rml::ElementList rows;
    document->GetElementsByClassName(rows, "dialog_row");
    ASSERT_GT(rows.size(), 1);
    context->Update();
    context->ProcessMouseMove(static_cast<int>(rows[1]->GetAbsoluteLeft() + rows[1]->GetClientWidth() / 2),
        static_cast<int>(rows[1]->GetAbsoluteTop() + rows[1]->GetClientHeight() / 2), 0);
    context->ProcessMouseButtonDown(0, 0);
    const auto selected = select_dialog_view_clicked_row(rows[1], view, workspace);
    ASSERT_TRUE(selected);
    ASSERT_TRUE(*selected);
    EXPECT_EQ(view.list.selected(), 1);
    class ReleaseRecorder final : public Rml::EventListener {
    public:
        ReleaseRecorder(DialogViewState& source, Rml::ElementDocument& target)
            : view{source}
            , document{target}
        {
        }
        void ProcessEvent(Rml::Event&) override
        {
            ++calls;
            EXPECT_EQ(view.list.selected(), 1);
            document.SetInnerRML("<p>Replacement during release</p>");
        }
        DialogViewState& view;
        Rml::ElementDocument& document;
        int calls = 0;
    } recorder{view, *document};
    context->AddEventListener("mouseup", &recorder);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    ClientInputDispatchState dispatch;
    EXPECT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset, ClientRmlForwardPhase::after_native,
        context, nullptr, event)
            .performed);
    EXPECT_EQ(recorder.calls, 1);
    EXPECT_EQ(dispatch.forwarding_phase, ClientRmlForwardPhase::after_native);
    context->RemoveEventListener("mouseup", &recorder);
    for (const auto* key : {"", "-1", "1tail", "2147483648"}) {
        document->SetInnerRML("<button id='target' class='dialog_row' style='position:absolute;left:20px;top:20px;width:80px;height:40px;' data-key='"
            + std::string{key} + "'>Select</button>");
        context->Update();
        auto* target = document->GetElementById("target");
        context->ProcessMouseMove(40, 40, 0);
        context->ProcessMouseButtonDown(0, 0);
        EXPECT_TRUE(target->IsPseudoClassSet("active"));
        const auto result = select_dialog_view_clicked_row(target, view, workspace);
        ASSERT_TRUE(result);
        EXPECT_FALSE(*result);
        dispatch = {};
        EXPECT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset, ClientRmlForwardPhase::before_native,
            context, nullptr, event)
                .performed);
        EXPECT_FALSE(target->IsPseudoClassSet("active"));
        EXPECT_EQ(view.list.selected(), 1);
    }
    document->SetInnerRML("<button id='target' class='dialog_row' data-key='0'>Select</button><p id='other'>Other</p>");
    EXPECT_FALSE(select_dialog_view_clicked_row(document->GetElementById("other"), view, workspace));
    workspace.open_tab("other", "Other", WorkspaceTabKind::generic);
    const auto rejected = select_dialog_view_clicked_row(document->GetElementById("target"), view, workspace);
    ASSERT_TRUE(rejected);
    EXPECT_FALSE(*rejected);
    EXPECT_EQ(view.list.selected(), 1);
}

TEST_F(ClientBrowserWorkspace, NativeTilePaletteUsesCurrentFolderAndRejectsStaleRowsAfterRelease)
{
    KernelServiceScope kernel;
    auto* tileset = nw::kernel::tilesets().load("ttr01");
    ASSERT_NE(tileset, nullptr);
    auto* area = nw::kernel::objects().make<nw::Area>();
    ASSERT_NE(area, nullptr);
    area->tileset = tileset;
    area->tileset_resref = nw::Resref{"ttr01"};
    area->width = area->height = 1;
    area->tiles.resize(1);
    AreaTileEditorState editor;
    ASSERT_TRUE(reset_area_tile_editor(editor, area->handle()));
    const auto render = [&] {
        std::string markup;
        append_area_tile_palette_markup(markup, editor);
        document->SetInnerRML("<div id='object_workbench' class='object_workbench' style='position:absolute;width:600px;height:600px;'>" + markup + "</div>");
        context->Update();
        ASSERT_TRUE(sync_area_tile_palette_window(document, editor, area->handle(), true, AreaTilePointerModifier::none, true));
        context->Update();
    };
    render();
    Rml::ElementList rows;
    document->GetElementsByClassName(rows, "area_tile_palette_row");
    ASSERT_FALSE(rows.empty());
    auto click = capture_area_tile_palette_click(rows.front(), editor);
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, AreaTilePaletteClickKind::folder);
    const auto folder = click->row;
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    ClientInputDispatchState dispatch;
    ASSERT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset, ClientRmlForwardPhase::before_native, context, nullptr, event).performed);
    document->SetInnerRML("<p>Replacement</p>");
    ASSERT_EQ(apply_area_tile_palette_click(*click, editor, area->handle()), AreaTilePaletteClickEffect::folder_changed);
    EXPECT_EQ(editor.palette.current_folder, folder);
    EXPECT_EQ(apply_area_tile_palette_click(*click, editor, area->handle()), AreaTilePaletteClickEffect::none);
    reset_area_tile_palette_folder_view(editor);
    render();
    rows.clear();
    document->GetElementsByClassName(rows, "area_tile_palette_row");
    const auto action = std::ranges::find_if(rows, [&](Rml::Element* row) {
        const auto key = row->GetAttribute<int>("data-key", -1);
        return key >= 0 && static_cast<size_t>(key) < editor.palette.rows.size()
            && editor.palette.rows[static_cast<size_t>(key)].kind == AreaTilePaletteRowKind::action;
    });
    ASSERT_NE(action, rows.end());
    const auto captured = capture_area_tile_palette_click(*action, editor);
    ASSERT_TRUE(captured);
    ASSERT_EQ(captured->kind, AreaTilePaletteClickKind::action);
    for (const int partition : {0, 1, 2, 3, 4}) {
        auto input = *captured;
        if (partition == 0) { ++input.resource_generation; }
        if (partition == 1) { input.folder = editor.palette.root_folder; }
        if (partition == 2) { input.query = "changed"; }
        if (partition == 3) { ++input.brush.value; }
        if (partition == 4) { input.row = UINT32_MAX; }
        EXPECT_EQ(apply_area_tile_palette_click(input, editor, area->handle()), AreaTilePaletteClickEffect::none);
        EXPECT_EQ(editor.selected_row, -1);
    }
    click = *captured;
    document->SetInnerRML("<p>Replacement</p>");
    EXPECT_EQ(apply_area_tile_palette_click(*click, editor, area->handle()), AreaTilePaletteClickEffect::selected);
    EXPECT_EQ(editor.selected_row, static_cast<int32_t>(captured->row));
    EXPECT_EQ(editor.group_orientation, 0);
    EXPECT_FALSE(editor.cursor_update_pending);
    document->SetInnerRML("<button id='area_tile_editor_back'>Back</button>");
    click = capture_area_tile_palette_click(document->GetElementById("area_tile_editor_back"), editor);
    ASSERT_TRUE(click);
    EXPECT_EQ(apply_area_tile_palette_click(*click, editor, area->handle()), AreaTilePaletteClickEffect::folder_changed);
    click = capture_area_tile_palette_click(document->GetElementById("area_tile_editor_back"), editor);
    ASSERT_TRUE(click);
    EXPECT_EQ(apply_area_tile_palette_click(*click, editor, area->handle()), AreaTilePaletteClickEffect::unavailable);
    EXPECT_EQ(editor.feedback, "Tile palette navigation is unavailable");
    for (const char* key : {"", "-1", "0tail", "2147483648"}) {
        document->SetInnerRML("<button id='target' class='area_tile_palette_row' data-key='" + std::string{key} + "'>Select</button>");
        click = capture_area_tile_palette_click(document->GetElementById("target"), editor);
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, AreaTilePaletteClickKind::none);
        EXPECT_EQ(apply_area_tile_palette_click(*click, editor, area->handle()), AreaTilePaletteClickEffect::none);
    }
    for (const auto& [name, expected] : std::array{
             std::pair{"properties", AreaWorkspaceSurface::properties},
             std::pair{"objects", AreaWorkspaceSurface::objects},
             std::pair{"tiles", AreaWorkspaceSurface::tiles}}) {
        document->SetInnerRML("<button id='target' class='area_workspace_tab' data-area-surface='" + std::string{name} + "'>Surface</button>");
        const auto request = capture_area_workspace_surface_click(document->GetElementById("target"));
        ASSERT_TRUE(request);
        EXPECT_EQ(request->surface, expected);
    }
    document->SetInnerRML("<button id='target' class='area_workspace_tab' data-area-surface='unknown'>Surface</button>");
    const auto request = capture_area_workspace_surface_click(document->GetElementById("target"));
    ASSERT_TRUE(request);
    EXPECT_FALSE(request->surface);
    EXPECT_FALSE(capture_area_workspace_surface_click(nullptr));
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

TEST_F(ClientShellView, NativeShellClicksConsumeOnceAndOutputKeysUseOwnedUtf8Text)
{
    KernelServiceScope services;
    ShellController shell;
    ShellViewState view;
    WorkspaceState workspace;
    ToolsetBackend backend;
    backend.bind(nullptr, &shell, &workspace);
    const auto unbind = create_scope_exit([] { script_command_host().bind(nullptr, nullptr); });
    shell.set_output_panel_visible(true);
    refresh_bottom_dock_view(document, shell, {});
    shell.append_output("info", "alpha β");
    refresh_output_view(document, view, shell);
    context->Update();
    auto* toggle = document->GetElementById("output_info");
    ASSERT_NE(toggle, nullptr);
    ASSERT_TRUE(toggle->IsVisible(true));
    context->ProcessMouseMove(static_cast<int>(toggle->GetAbsoluteLeft() + toggle->GetClientWidth() / 2),
        static_cast<int>(toggle->GetAbsoluteTop() + toggle->GetClientHeight() / 2), 0);
    context->ProcessMouseButtonDown(0, 0);
    ASSERT_TRUE(toggle->IsPseudoClassSet("active"));
    auto click = capture_shell_ui_click(toggle);
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, ShellUiClickKind::output_channel);
    const bool before = shell.output_channel_visible("info");
    auto command = take_shell_ui_click_command(*click);
    ASSERT_TRUE(command);
    CommandContext command_context{.active_tab_id = workspace.active_tab_id(), .source = CommandSource::widget, .workspace = &workspace};
    ASSERT_TRUE(backend.execute_command(std::move(*command), command_context).ok());
    EXPECT_NE(shell.output_channel_visible("info"), before);
    EXPECT_FALSE(take_shell_ui_click_command(*click));
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    ClientInputDispatchState dispatch{.native_handled = true};
    ASSERT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, nullptr, event)
            .performed);
    EXPECT_FALSE(toggle->IsPseudoClassSet("active"));
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, nullptr, event)
            .performed);
    EXPECT_NE(shell.output_channel_visible("info"), before);

    auto* dock = document->GetElementById("bottom_tab_terminal");
    ASSERT_NE(dock, nullptr);
    auto dock_click = capture_shell_ui_click(dock);
    ASSERT_TRUE(dock_click);
    ASSERT_EQ(dock_click->kind, ShellUiClickKind::dock);
    command = take_shell_ui_click_command(*dock_click);
    ASSERT_TRUE(command);
    ASSERT_TRUE(backend.execute_command(std::move(*command), command_context).ok());
    EXPECT_TRUE(shell.terminal_visible());
    EXPECT_FALSE(take_shell_ui_click_command(*dock_click));
    dock->SetAttribute("data-widget", "");
    dock_click = capture_shell_ui_click(dock);
    ASSERT_TRUE(dock_click);
    EXPECT_FALSE(take_shell_ui_click_command(*dock_click));
    toggle->SetId("output_unknown");
    click = capture_shell_ui_click(toggle);
    ASSERT_TRUE(click);
    EXPECT_FALSE(take_shell_ui_click_command(*click));
    EXPECT_FALSE(capture_shell_ui_click(nullptr));

    shell.set_output_panel_visible(true);
    shell.toggle_output_channel("info");
    refresh_bottom_dock_view(document, shell, {});
    refresh_output_view(document, view, shell);
    context->Update();
    auto* output = document->GetElementById("output_list");
    ASSERT_NE(output, nullptr);
    ASSERT_TRUE(output->Focus());
    ASSERT_NE(view.output_selection.text.find("β"), std::string::npos);
    const auto full_text = view.output_selection.text;
    SDL_KeyboardEvent key{};
    key.key = SDLK_A;
    key.mod = SDL_KMOD_CTRL;
    auto result = handle_shell_output_key(key, context, view, shell);
    EXPECT_TRUE(result.handled);
    EXPECT_FALSE(result.clipboard);
    EXPECT_EQ(view.output_selection.anchor, 0u);
    EXPECT_EQ(view.output_selection.focus, full_text.size());
    key.key = SDLK_C;
    result = handle_shell_output_key(key, context, view, shell);
    ASSERT_TRUE(result.clipboard);
    EXPECT_EQ(*result.clipboard, full_text);
    view.output_selection.text = "replacement";
    EXPECT_EQ(*result.clipboard, full_text);
    key.repeat = true;
    EXPECT_FALSE(handle_shell_output_key(key, context, view, shell).handled);
    key.repeat = false;
    key.mod = SDL_KMOD_CTRL | SDL_KMOD_ALT;
    EXPECT_FALSE(handle_shell_output_key(key, context, view, shell).handled);
    key.mod = SDL_KMOD_GUI;
    view.output_selection.anchor = std::numeric_limits<size_t>::max();
    view.output_selection.focus = std::numeric_limits<size_t>::max();
    result = handle_shell_output_key(key, context, view, shell);
    EXPECT_TRUE(result.handled);
    EXPECT_FALSE(result.clipboard);
    output->SetProperty("display", "none");
    context->Update();
    EXPECT_FALSE(handle_shell_output_key(key, context, view, shell).handled);
    EXPECT_FALSE(handle_shell_output_key(key, nullptr, view, shell).handled);
}

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

TEST_F(ClientObjectWorkbench, EditorWheelObjectCommandsKeepOneUndoAndExistingNumericPolicy)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    nw::kernel::runtime().add_module_path("stdlib/toolset");
    workspace.open_tab("area:test", "Test Area", WorkspaceTabKind::area);
    nw::GffBuilder builder{nw::Sound::serial_id};
    builder.top.add_field("TemplateResRef", nw::Resref{"test_sound"});
    builder.top.add_field("MinDistance", 1.0f);
    builder.top.add_field("MaxDistance", 10.0f);
    builder.top.add_field("Elevation", 0.0f);
    builder.top.add_field("Positional", uint8_t{1});
    builder.build();
    nw::ResourceData data;
    data.bytes = builder.to_byte_array();
    nw::Gff gff{std::move(data)};
    ASSERT_TRUE(gff.valid());
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    ASSERT_TRUE(nw::deserialize(sound, gff.toplevel(), nw::SerializationProfile::blueprint));
    activate(sound->handle());
    command.source = CommandSource::renderer;
    const auto radius = [&] { return nwn1::sound_toolset_visual_state(sound->handle())->distance_max; };
    ASSERT_FLOAT_EQ(radius(), 10);
    auto result = apply_area_object_wheel_action({EditorWheelActionKind::sound_radius, 1}, backend, command, sound->handle());
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->ok()) << result->message;
    EXPECT_FLOAT_EQ(radius(), 11);
    ASSERT_EQ(workspace.active_tab()->undo_stack.size(), 1);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_FLOAT_EQ(radius(), 10);
    result = apply_area_object_wheel_action({EditorWheelActionKind::sound_radius, -1e20f}, backend, command, sound->handle());
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->ok()) << result->message;
    EXPECT_FLOAT_EQ(radius(), 1);
    ASSERT_EQ(workspace.active_tab()->undo_stack.size(), 1);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_FLOAT_EQ(radius(), 10);
    EXPECT_FALSE(apply_area_object_wheel_action({EditorWheelActionKind::sound_radius, 1e20f}, backend, command, sound->handle()));
    EXPECT_FLOAT_EQ(radius(), 10);
    EXPECT_TRUE(workspace.active_tab()->undo_stack.empty());

    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    ASSERT_NE(nw::kernel::objects().components().get_or_create_spatial(actor->handle()), nullptr);
    const auto* spatial = nw::kernel::objects().components().find_spatial(actor->handle());
    ASSERT_NE(spatial, nullptr);
    const auto before = *spatial;
    result = apply_area_object_wheel_action({EditorWheelActionKind::object_scale, 1}, backend, command, actor->handle());
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->ok()) << result->message;
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(actor->handle())->scale, before.scale * 1.1f);
    ASSERT_EQ(workspace.active_tab()->undo_stack.size(), 1);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(actor->handle())->scale, before.scale);
    result = apply_area_object_wheel_action({EditorWheelActionKind::object_rotate, 1}, backend, command, actor->handle());
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->ok()) << result->message;
    EXPECT_NE(nw::kernel::objects().components().find_spatial(actor->handle())->orientation, before.orientation);
    ASSERT_EQ(workspace.active_tab()->undo_stack.size(), 1);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(actor->handle())->orientation, before.orientation);
    result = apply_area_object_wheel_action({EditorWheelActionKind::object_scale, std::numeric_limits<float>::max()}, backend, command, actor->handle());
    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, CommandStatus::rejected);
    EXPECT_EQ(nw::kernel::objects().components().find_spatial(actor->handle())->scale, before.scale);
    EXPECT_TRUE(workspace.active_tab()->undo_stack.empty());
    EXPECT_FALSE(apply_area_object_wheel_action({EditorWheelActionKind::camera_zoom, 1}, backend, command, actor->handle()));
    EXPECT_FALSE(apply_area_object_wheel_action({EditorWheelActionKind::object_scale, std::numeric_limits<float>::quiet_NaN()}, backend, command, actor->handle()));
    EXPECT_TRUE(workspace.active_tab()->undo_stack.empty());
}

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

TEST_F(ClientObjectWorkbench, FocusedIntegerKeysAdjustBoundedTextAndCommitOneUndo)
{
    auto* door = nw::kernel::objects().make<nw::Door>();
    ASSERT_NE(door, nullptr);
    nw::kernel::runtime().init_object_propsets(door->handle());
    activate(door->handle());
    const auto row = std::ranges::find_if(view.object_details.rows, [](const ObjectDetailsRow& candidate) {
        return candidate.kind == ObjectDetailsRowKind::value && candidate.editor == ObjectDetailsEditorKind::integer
            && candidate.edit_value < candidate.edit_max;
    });
    ASSERT_NE(row, view.object_details.rows.end());
    const auto index = static_cast<uint32_t>(row - view.object_details.rows.begin());
    const auto before = row->edit_value;
    document->SetInnerRML("<input id='target' class='object_details_integer' type='text' data-row='" + std::to_string(index)
        + "' data-current='" + std::to_string(before) + "' data-min='" + std::to_string(row->edit_min)
        + "' data-max='" + std::to_string(row->edit_max) + "' value='" + std::to_string(before) + "'/>");
    context->Update();
    auto* input = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("target"));
    ASSERT_NE(input, nullptr);
    ASSERT_TRUE(input->Focus());
    SDL_KeyboardEvent key{};
    key.key = SDLK_UP;
    key.repeat = true;
    const auto handle = [&] { return handle_object_workbench_field_key(key, context, document, view, workspace, backend, shell, command); };
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::handled);
    EXPECT_EQ(input->GetValue(), std::to_string(before + 1));
    EXPECT_EQ(workspace.undo_count(), 0);
    key.mod = SDL_KMOD_CTRL;
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::none);
    EXPECT_EQ(input->GetValue(), std::to_string(before + 1));
    key.mod = SDL_KMOD_NONE;
    key.key = SDLK_RETURN;
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::none);
    EXPECT_EQ(workspace.undo_count(), 0);
    key.repeat = false;
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::handled);
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_NE(context->GetFocusElement(), input);
    ObjectDetailsSnapshot current;
    build_object_details(nw::kernel::runtime(), door->handle(), current);
    ASSERT_LT(index, current.rows.size());
    EXPECT_EQ(current.rows[index].edit_value, before + 1);
    ASSERT_TRUE(workspace.undo(command).ok());
    build_object_details(nw::kernel::runtime(), door->handle(), current);
    EXPECT_EQ(current.rows[index].edit_value, before);
    ASSERT_TRUE(input->Focus());
    key.key = SDLK_UP;
    input->SetValue(std::to_string(row->edit_max));
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::handled);
    EXPECT_EQ(input->GetValue(), std::to_string(row->edit_max));
    for (const char* invalid : {"2147483648", "0tail", ""}) {
        input->SetValue(invalid);
        EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::handled);
        EXPECT_EQ(input->GetValue(), invalid);
    }
    EXPECT_EQ(workspace.undo_count(), 0);
    key.key = SDLK_ESCAPE;
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::handled);
    EXPECT_NE(context->GetFocusElement(), input);
    EXPECT_EQ(handle_object_workbench_field_key(key, nullptr, document, view, workspace, backend, shell, command), ObjectWorkbenchFieldKeyEffect::none);
}

TEST_F(ClientObjectWorkbench, FocusedSoundKeysUseExistingOptionsAndOneUndo)
{
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    nw::kernel::runtime().init_object_propsets(sound->handle());
    activate(sound->handle());
    const auto row = std::ranges::find(view.object_details.rows, ObjectDetailsEditorKind::sound_position, &ObjectDetailsRow::editor);
    ASSERT_NE(row, view.object_details.rows.end());
    const auto index = static_cast<uint32_t>(row - view.object_details.rows.begin());
    const auto before = row->edit_value;
    document->SetInnerRML("<div id='object_workbench' class='object_workbench' style='position:absolute;width:600px;height:600px;'>"
                          "<button id='object_details_sound_position_field_"
        + std::to_string(index) + "' class='object_details_sound_position_field' data-row='" + std::to_string(index)
        + "'>Position</button><div id='object_details_combobox_popup' class='combobox_options combobox_popup object_details_combobox_popup'></div></div>");
    context->Update();
    auto* field = document->GetElementById("object_details_sound_position_field_" + std::to_string(index));
    ASSERT_NE(field, nullptr);
    ASSERT_TRUE(field->Focus());
    SDL_KeyboardEvent key{};
    key.key = SDLK_RETURN;
    const auto handle = [&] { return handle_object_workbench_field_key(key, context, document, view, workspace, backend, shell, command); };
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::handled);
    ASSERT_TRUE(view.object_details_combobox.is_active());
    EXPECT_EQ(view.object_details_combobox.selected_key(), before);
    EXPECT_EQ(workspace.undo_count(), 0);
    key.key = before == 2 ? SDLK_UP : SDLK_DOWN;
    key.repeat = true;
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::handled);
    const auto desired = before == 2 ? before - 1 : before + 1;
    EXPECT_EQ(view.object_details_combobox.selected_key(), desired);
    key.key = SDLK_RETURN;
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::none);
    key.repeat = false;
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::content_changed);
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_FALSE(view.object_details_combobox.is_active());
    ObjectDetailsSnapshot current;
    build_object_details(nw::kernel::runtime(), sound->handle(), current);
    ASSERT_LT(index, current.rows.size());
    EXPECT_EQ(current.rows[index].edit_value, desired);
    ASSERT_TRUE(workspace.undo(command).ok());
    build_object_details(nw::kernel::runtime(), sound->handle(), current);
    EXPECT_EQ(current.rows[index].edit_value, before);
    field->SetAttribute("data-row", "0tail");
    EXPECT_EQ(handle(), ObjectWorkbenchFieldKeyEffect::none);
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientObjectWorkbench, NativeSoundComboOwnsPropertyAndOptionsAndCommitsOnce)
{
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    auto& runtime = nw::kernel::runtime();
    runtime.init_object_propsets(sound->handle());
    activate(sound->handle());
    const auto row = std::ranges::find(view.object_details.rows, ObjectDetailsEditorKind::sound_position, &ObjectDetailsRow::editor);
    ASSERT_NE(row, view.object_details.rows.end());
    const auto index = static_cast<uint32_t>(row - view.object_details.rows.begin());
    const auto before = row->edit_value;
    const auto field_id = "object_details_sound_position_field_" + std::to_string(index);
    document->SetInnerRML("<div id='object_workbench' class='object_workbench' style='position:absolute;left:0px;top:0px;width:600px;height:600px;'>"
                          "<div id='property_tree_rows' style='height:500px;width:600px;overflow:auto;'></div>"
                          "<div id='property_tree_count'></div><div id='object_details_combobox_popup' class='combobox_options combobox_popup object_details_combobox_popup'></div></div>");
    context->Update();
    ASSERT_TRUE(sync_object_details_window(document, view, workspace, true));
    context->Update();
    auto* field = document->GetElementById(field_id);
    ASSERT_NE(field, nullptr);
    auto click = capture_object_workbench_combo_click(field, view, workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, ObjectWorkbenchComboKind::sound_open);
    EXPECT_EQ(click->property->current, before);
    const auto apply = [&](ObjectWorkbenchComboClick& input) {
        return apply_object_workbench_combo_click(input, document, view, workspace, backend, shell, command);
    };
    document->SetInnerRML("<div id='object_workbench' class='object_workbench' style='position:absolute;left:0px;top:0px;width:600px;height:600px;'>"
                          "<button id='"
        + field_id + "' class='object_details_sound_position_field' data-row='" + std::to_string(index)
        + "' style='width:200px;height:40px;'>Placement</button><div id='object_details_combobox_popup' class='combobox_options combobox_popup object_details_combobox_popup'></div></div>");
    context->Update();
    ASSERT_EQ(apply(*click), ObjectWorkbenchComboEffect::sound_opened);
    EXPECT_EQ(apply(*click), ObjectWorkbenchComboEffect::none);
    ASSERT_TRUE(sync_object_details_combobox(document, view, workspace, true));
    EXPECT_TRUE(focus_object_workbench_combo_field(document, *click, view, workspace));
    context->Update();
    ASSERT_TRUE(sync_object_details_combobox(document, view, workspace, true));
    context->Update();
    EXPECT_EQ(view.object_details_combobox.size(), 3);
    Rml::ElementList options;
    document->GetElementsByClassName(options, "combobox_option");
    ASSERT_EQ(options.size(), 3);
    const auto desired = (before + 1) % 3;
    const auto target = std::ranges::find_if(options, [&](Rml::Element* option) {
        return option->GetAttribute<Rml::String>("data-key", "") == std::to_string(desired);
    });
    ASSERT_NE(target, options.end());
    context->ProcessMouseMove(static_cast<int>((*target)->GetAbsoluteLeft() + (*target)->GetClientWidth() / 2),
        static_cast<int>((*target)->GetAbsoluteTop() + (*target)->GetClientHeight() / 2), 0);
    context->ProcessMouseButtonDown(0, 0);
    click = capture_object_workbench_combo_click(*target, view, workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, ObjectWorkbenchComboKind::sound_select);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    ClientInputDispatchState dispatch;
    ASSERT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset, ClientRmlForwardPhase::before_native, context, nullptr, event).performed);
    document->SetInnerRML("<p>Replacement after release</p>");
    ASSERT_EQ(apply(*click), ObjectWorkbenchComboEffect::sound_selected);
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_FALSE(view.object_details_combobox.is_active());
    EXPECT_EQ(apply(*click), ObjectWorkbenchComboEffect::none);
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset, ClientRmlForwardPhase::before_native, context, nullptr, event).performed);
    ObjectDetailsSnapshot current;
    build_object_details(runtime, sound->handle(), current);
    ASSERT_LT(index, current.rows.size());
    EXPECT_EQ(current.rows[index].edit_value, desired);
    ASSERT_TRUE(workspace.undo(command).ok());
    build_object_details(runtime, sound->handle(), current);
    EXPECT_EQ(current.rows[index].edit_value, before);
    ASSERT_TRUE(open_object_details_sound_position_combobox(document, view, workspace, index));
    document->SetInnerRML("<div id='object_details_combobox_popup'><button id='target' class='combobox_option' data-key='"
        + std::to_string(desired) + "'>Select</button></div>");
    click = capture_object_workbench_combo_click(document->GetElementById("target"), view, workspace,
        backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, ObjectWorkbenchComboKind::sound_select);
    const auto propset = runtime.find_propset_ref(row->propset_type, sound->handle());
    const auto* definition = runtime.get_struct_def(row->propset_type);
    ASSERT_NE(definition, nullptr);
    ASSERT_TRUE(runtime.write_struct_value_field(propset, definition, definition->field_index("positional"),
        nw::smalls::Value::make_int(desired == 0 ? 0 : 1)));
    ASSERT_TRUE(runtime.write_struct_value_field(propset, definition, definition->field_index("random_position"),
        nw::smalls::Value::make_int(desired == 2 ? 1 : 0)));
    EXPECT_EQ(object_mutation_state().epoch, click->mutation_epoch);
    const auto logs = shell.output_lines.size();
    EXPECT_EQ(apply(*click), ObjectWorkbenchComboEffect::none);
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_EQ(shell.output_lines.size(), logs);
}

TEST_F(ClientObjectWorkbench, NativeSpellComboKeepsFilterToggleAndRejectsChangedSourceFacts)
{
    ASSERT_NE(nw::kernel::load_module("test_data/user/modules/DockerDemo.mod"), nullptr);
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path("stdlib/core");
    runtime.add_module_path("stdlib/nwn1");
    runtime.add_module_path("stdlib/toolset");
    ASSERT_NE(runtime.load_module("toolset.ui"), nullptr);
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/wizard_pm.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::spells;
    auto& creature = view.creature_view;
    rebuild_active_creature_spells(creature, actor->handle());
    ASSERT_EQ(creature.creature_spells.status, CreatureSpellViewStatus::ready);
    const auto target = object_workbench_target(view, workspace);
    const auto render = [&] {
        std::string markup;
        append_creature_spell_markup(markup, creature, target);
        append_creature_workbench_overlay_markup(markup, creature, target);
        document->SetInnerRML("<div id='object_workbench' class='object_workbench' style='position:absolute;left:0px;top:0px;height:500px;width:600px;'>" + markup + "</div>");
        context->Update();
    };
    const auto apply = [&](ObjectWorkbenchComboClick& click) {
        return apply_object_workbench_combo_click(click, document, view, workspace, backend, shell, command);
    };
    render();
    Rml::ElementList fields;
    document->GetElementsByClassName(fields, "creature_spell_filter_field");
    const auto field = std::ranges::find_if(fields, [](Rml::Element* element) {
        return element->GetAttribute<Rml::String>("data-filter", "") == "level";
    });
    ASSERT_NE(field, fields.end());
    auto click = capture_object_workbench_combo_click(*field, view, workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, ObjectWorkbenchComboKind::spell_open);
    document->SetInnerRML("<p>Replacement</p>");
    ASSERT_EQ(apply(*click), ObjectWorkbenchComboEffect::spell_opened);
    EXPECT_TRUE(creature.creature_spell_combobox.popup_visible());
    render();
    ASSERT_TRUE(sync_creature_spell_filter_window(document, creature, target, true));
    context->Update();
    ASSERT_TRUE(sync_creature_spell_filter_window(document, creature, target, true));
    context->Update();
    EXPECT_EQ(creature.creature_spell_combobox.size(), 11);
    Rml::ElementList options;
    document->GetElementsByClassName(options, "combobox_option");
    ASSERT_FALSE(options.empty());
    auto option = std::ranges::find_if(options, [](Rml::Element* element) {
        return element->GetAttribute<Rml::String>("data-key", "") == "2";
    });
    ASSERT_NE(option, options.end());
    const auto captured = capture_object_workbench_combo_click(*option, view, workspace,
        backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(captured);
    ASSERT_EQ(captured->kind, ObjectWorkbenchComboKind::spell_select);
    for (const int partition : {0, 1, 2, 3, 4, 5}) {
        auto input = *captured;
        if (partition == 0) { ++input.module_generation; }
        if (partition == 1) { ++input.resource_generation; }
        if (partition == 2) { ++input.mutation_epoch; }
        if (partition == 3) { ++input.selected_class; }
        if (partition == 4) { creature.creature_spell_query = "changed"; }
        if (partition == 5) { creature.creature_spell_combobox.hide_popup(); }
        EXPECT_EQ(apply(input), ObjectWorkbenchComboEffect::none);
        creature.creature_spell_query.clear();
        (void)creature.creature_spell_combobox.show_popup();
    }
    click = *captured;
    document->SetInnerRML("<p>Replacement</p>");
    ASSERT_EQ(apply(*click), ObjectWorkbenchComboEffect::spell_selected);
    EXPECT_EQ(creature.creature_spell_level, 2);
    EXPECT_EQ(creature.creature_spell_filter_field, CreatureSpellFilterField::none);
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_TRUE(shell.output_lines.empty());
    render();
    fields.clear();
    document->GetElementsByClassName(fields, "creature_spell_filter_field");
    auto level_field = std::ranges::find_if(fields, [](Rml::Element* element) {
        return element->GetAttribute<Rml::String>("data-filter", "") == "level";
    });
    ASSERT_NE(level_field, fields.end());
    for (const bool visible : {true, false, true}) {
        click = capture_object_workbench_combo_click(*level_field, view, workspace, backend.module_generation(), nw::kernel::resman().generation());
        ASSERT_TRUE(click);
        ASSERT_EQ(apply(*click), ObjectWorkbenchComboEffect::spell_opened);
        EXPECT_EQ(creature.creature_spell_combobox.popup_visible(), visible);
    }
    click = capture_object_workbench_combo_click(*level_field, view, workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    workspace.open_tab("other", "Other", WorkspaceTabKind::preview);
    EXPECT_EQ(apply(*click), ObjectWorkbenchComboEffect::none);
}

TEST_F(ClientObjectWorkbench, NativeComboRejectsMalformedPressedControlsAndChangedOwner)
{
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(sound, nullptr);
    nw::kernel::runtime().init_object_propsets(sound->handle());
    activate(sound->handle());
    for (const auto* key : {"", "-1", "0tail", "2147483648"}) {
        document->SetInnerRML("<button id='target' class='object_details_sound_position_field' style='position:absolute;left:20px;top:20px;width:80px;height:40px;' data-row='"
            + std::string{key} + "'>Placement</button>");
        context->Update();
        auto* target = document->GetElementById("target");
        context->ProcessMouseMove(40, 40, 0);
        context->ProcessMouseButtonDown(0, 0);
        EXPECT_TRUE(target->IsPseudoClassSet("active"));
        auto click = capture_object_workbench_combo_click(target, view, workspace, backend.module_generation(), nw::kernel::resman().generation());
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, ObjectWorkbenchComboKind::none);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_LEFT;
        ClientInputDispatchState dispatch;
        EXPECT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset, ClientRmlForwardPhase::before_native, context, nullptr, event).performed);
        EXPECT_FALSE(target->IsPseudoClassSet("active"));
        EXPECT_EQ(apply_object_workbench_combo_click(*click, document, view, workspace, backend, shell, command), ObjectWorkbenchComboEffect::none);
    }
    const auto row = std::ranges::find(view.object_details.rows, ObjectDetailsEditorKind::sound_position, &ObjectDetailsRow::editor);
    ASSERT_NE(row, view.object_details.rows.end());
    const auto index = static_cast<uint32_t>(row - view.object_details.rows.begin());
    document->SetInnerRML("<button id='target' class='object_details_sound_position_field' data-row='" + std::to_string(index) + "'>Placement</button>");
    auto click = capture_object_workbench_combo_click(document->GetElementById("target"), view, workspace,
        backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, ObjectWorkbenchComboKind::sound_open);
    auto* other = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(other, nullptr);
    nw::kernel::runtime().init_object_propsets(other->handle());
    activate(other->handle());
    EXPECT_EQ(apply_object_workbench_combo_click(*click, document, view, workspace, backend, shell, command), ObjectWorkbenchComboEffect::none);
    EXPECT_FALSE(focus_object_workbench_combo_field(document, *click, view, workspace));
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_TRUE(shell.output_lines.empty());
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

TEST_F(ClientAppearanceView, CatalogKeysCommitRealChoicesOnceAndEscapeKeepsPriority)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    auto* sound = nw::kernel::objects().make<nw::Sound>();
    ASSERT_NE(actor, nullptr);
    ASSERT_NE(sound, nullptr);
    nw::kernel::runtime().init_object_propsets(sound->handle());
    const auto generation = nw::kernel::resman().generation();
    for (const bool sound_choice : {false, true}) {
        SCOPED_TRACE(sound_choice);
        const auto object = sound_choice ? sound->handle() : actor->handle();
        activate(object);
        view.object_workbench_surface = sound_choice ? ObjectWorkbenchSurface::sounds : ObjectWorkbenchSurface::appearance;
        auto& appearance = view.appearance_view;
        std::string markup;
        if (sound_choice) {
            appearance.sound_resource_selector_open = true;
            rebuild_sound_catalog(appearance, generation, true);
            appearance.sound_catalog_query = "al_pl_whispersm";
            rebuild_sound_catalog(appearance, generation, true);
            ASSERT_EQ(appearance.sound_catalog_matches.size(), 1);
            append_sound_resource_selector_markup(markup, appearance);
        } else {
            rebuild_active_appearances(appearance, backend.module_generation(), object);
            appearance.appearance_selector_open = true;
            appearance.appearance_query = "Bodak";
            rebuild_active_appearances(appearance, backend.module_generation(), object);
            ASSERT_FALSE(appearance.appearance_matches.empty());
            append_appearance_selector_markup(markup, appearance);
        }
        const auto before_appearance = object_appearance(nw::kernel::runtime(), actor->handle());
        int32_t desired_appearance = -1;
        const auto before_sound = snapshot_sound_resources(nw::kernel::runtime(), sound->handle());
        ASSERT_TRUE(before_sound);
        document->SetInnerRML("<div id='object_workbench' class='object_workbench' style='position:absolute;width:600px;height:600px;'>" + markup + "</div>");
        context->Update();
        auto* search = document->GetElementById(sound_choice ? "sound_catalog_search" : "appearance_search");
        ASSERT_NE(search, nullptr);
        ASSERT_TRUE(search->Focus());
        SDL_KeyboardEvent key{};
        key.key = SDLK_DOWN;
        key.repeat = true;
        const auto handle = [&] {
            return handle_appearance_selector_key(key, context, document, appearance, object_workbench_target(view, workspace), generation, backend, shell, command);
        };
        EXPECT_EQ(handle(), AppearanceSelectorKeyEffect::handled);
        if (sound_choice) {
            EXPECT_EQ(appearance.sound_catalog_list.selected(), 0);
        } else {
            const auto selected = appearance.appearance_list.selected();
            ASSERT_GE(selected, 0);
            ASSERT_LT(static_cast<size_t>(selected), appearance.appearance_matches.size());
            desired_appearance = active_appearance_catalog(appearance).rows[appearance.appearance_matches[static_cast<size_t>(selected)]].id;
        }
        EXPECT_EQ(workspace.undo_count(), 0);
        key.mod = SDL_KMOD_CTRL;
        EXPECT_EQ(handle(), AppearanceSelectorKeyEffect::none);
        key.mod = SDL_KMOD_NONE;
        key.key = SDLK_RETURN;
        EXPECT_EQ(handle(), AppearanceSelectorKeyEffect::none);
        key.repeat = false;
        EXPECT_EQ(handle(), AppearanceSelectorKeyEffect::content_changed);
        EXPECT_EQ(workspace.undo_count(), 1);
        if (sound_choice) {
            const auto after = snapshot_sound_resources(nw::kernel::runtime(), sound->handle());
            ASSERT_TRUE(after);
            ASSERT_EQ(after->size(), before_sound->size() + 1);
            EXPECT_EQ(after->back().view(), "al_pl_whispersm");
            EXPECT_FALSE(appearance.sound_resource_selector_open);
        } else {
            EXPECT_EQ(object_appearance(nw::kernel::runtime(), actor->handle()), desired_appearance);
            EXPECT_FALSE(appearance.appearance_selector_open);
        }
        EXPECT_EQ(handle(), AppearanceSelectorKeyEffect::none);
        EXPECT_EQ(workspace.undo_count(), 1);
        ASSERT_TRUE(workspace.undo(command).ok());
        EXPECT_EQ(object_appearance(nw::kernel::runtime(), actor->handle()), before_appearance);
        EXPECT_EQ(snapshot_sound_resources(nw::kernel::runtime(), sound->handle()), before_sound);
    }
    auto& appearance = view.appearance_view;
    appearance.color_editor_channel = 0;
    appearance.appearance_selector_open = true;
    appearance.sound_resource_selector_open = true;
    const auto close = [&] { return close_appearance_selector_for_escape(appearance, object_workbench_target(view, workspace), backend.module_generation()); };
    EXPECT_TRUE(close());
    EXPECT_EQ(appearance.color_editor_channel, -1);
    EXPECT_TRUE(appearance.appearance_selector_open);
    EXPECT_TRUE(appearance.sound_resource_selector_open);
    EXPECT_TRUE(close());
    EXPECT_FALSE(appearance.appearance_selector_open);
    EXPECT_TRUE(appearance.sound_resource_selector_open);
    EXPECT_TRUE(close());
    EXPECT_FALSE(appearance.sound_resource_selector_open);
    EXPECT_FALSE(close());
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientAppearanceView, NativeCatalogControlsOwnSemanticSelectionThroughDomReplacement)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    auto& appearance = view.appearance_view;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    const auto apply = [&](AppearanceCatalogClick& click) {
        return apply_appearance_catalog_click(click, appearance, object_workbench_target(view, workspace), workspace, backend, shell, command);
    };
    std::string markup;
    append_appearance_catalog_field_markup(markup, appearance, AppearanceEditorField::appearance,
        object_appearance(nw::kernel::runtime(), actor->handle()));
    document->SetInnerRML(markup);
    Rml::ElementList fields;
    document->GetElementsByClassName(fields, "appearance_catalog_field");
    ASSERT_FALSE(fields.empty());
    auto click = capture_appearance_catalog_click(fields.front(), appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, AppearanceCatalogClickKind::open);
    document->SetInnerRML("<p>Replacement during shared selector close</p>");
    ASSERT_EQ(apply(*click), AppearanceCatalogClickEffect::opened);
    EXPECT_EQ(apply(*click), AppearanceCatalogClickEffect::none);
    appearance.appearance_query = "Bodak";
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    ASSERT_FALSE(appearance.appearance_matches.empty());
    const auto before = object_appearance(nw::kernel::runtime(), actor->handle());
    const auto desired = active_appearance_catalog(appearance).rows[appearance.appearance_matches.front()].id;
    ASSERT_NE(before, std::optional{desired});
    markup.clear();
    append_appearance_selector_markup(markup, appearance);
    document->SetInnerRML(markup);
    context->Update();
    ASSERT_TRUE(sync_appearance_window(document, appearance, object_workbench_target(view, workspace), true));
    auto* rows = document->GetElementById("appearance_rows");
    ASSERT_NE(rows, nullptr);
    ASSERT_GT(rows->GetNumChildren(), 0);
    click = capture_appearance_catalog_click(rows->GetChild(0), appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, AppearanceCatalogClickKind::select);
    EXPECT_EQ(click->selected, desired);
    EXPECT_EQ(click->query, "Bodak");
    document->SetInnerRML("<p>Replacement during SDK release</p>");
    ASSERT_EQ(apply(*click), AppearanceCatalogClickEffect::refresh);
    EXPECT_EQ(object_appearance(nw::kernel::runtime(), actor->handle()), desired);
    EXPECT_FALSE(appearance.appearance_selector_open);
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_EQ(apply(*click), AppearanceCatalogClickEffect::none);
    EXPECT_EQ(workspace.undo_count(), 1);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(object_appearance(nw::kernel::runtime(), actor->handle()), before);
    appearance.appearance_selector_open = true;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    markup.clear();
    append_appearance_selector_markup(markup, appearance);
    document->SetInnerRML(markup);
    auto* back = document->GetElementById("appearance_selector_back");
    ASSERT_NE(back, nullptr);
    EXPECT_FALSE(capture_appearance_catalog_click(back, appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation()));
    click = capture_appearance_back_click(back, appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(apply(*click), AppearanceCatalogClickEffect::refresh);
    EXPECT_FALSE(appearance.appearance_selector_open);
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientAppearanceView, NativeCatalogCyclesWrapAndRejectChangedNonGenericDoorStyles)
{
    load_fixture();
    auto* placeable = nw::kernel::objects().make<nw::Placeable>();
    auto* door = nw::kernel::objects().make<nw::Door>();
    ASSERT_NE(placeable, nullptr);
    ASSERT_NE(door, nullptr);
    auto& runtime = nw::kernel::runtime();
    for (const auto object : {placeable->handle(), door->handle()}) {
        SCOPED_TRACE(object.to_ull());
        runtime.init_object_propsets(object);
        activate(object);
        view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
        auto& appearance = view.appearance_view;
        rebuild_active_appearances(appearance, backend.module_generation(), object);
        const auto& catalog = active_appearance_catalog(appearance);
        ASSERT_EQ(catalog.status, AppearanceCatalogStatus::ready);
        ASSERT_GE(catalog.rows.size(), 2);
        // Use real catalog IDs to establish the first-row boundary.
        ASSERT_TRUE(commit_active_appearance_selection(appearance, backend, shell, command, catalog.rows.front().id));
        rebuild_active_appearances(appearance, backend.module_generation(), object);
        std::string markup;
        if (object.type == nw::ObjectType::door) {
            append_door_appearance_markup(markup, appearance);
        } else {
            append_placeable_appearance_markup(markup, appearance);
        }
        document->SetInnerRML(markup);
        const auto prefix = object.type == nw::ObjectType::door ? "door" : "placeable";
        const auto capture = [&](const char* direction) {
            return capture_appearance_catalog_click(document->GetElementById(std::string{prefix} + "_appearance_" + direction),
                appearance, object_workbench_target(view, workspace), workspace, backend.module_generation(), nw::kernel::resman().generation());
        };
        const auto apply = [&](AppearanceCatalogClick& click) {
            return apply_appearance_catalog_click(click, appearance, object_workbench_target(view, workspace), workspace, backend, shell, command);
        };
        auto click = capture("previous");
        ASSERT_TRUE(click);
        EXPECT_EQ(click->selected, catalog.rows.back().id);
        const auto count = workspace.undo_count();
        ASSERT_EQ(apply(*click), AppearanceCatalogClickEffect::refresh);
        EXPECT_EQ(workspace.undo_count(), count + 1);
        click = capture("next");
        ASSERT_TRUE(click);
        EXPECT_EQ(click->selected, catalog.rows.front().id);
        ASSERT_EQ(apply(*click), AppearanceCatalogClickEffect::refresh);
        if (object.type != nw::ObjectType::door) { continue; }
        auto value = nw::smalls::Value::make_object(object);
        value.type_id = runtime.object_subtype_for_tag(object.type);
        const auto types = runtime.execute_script("nwn1.doors", "count_doortypes", {});
        ASSERT_TRUE(types.ok());
        std::vector<int32_t> styles;
        for (int32_t row = 1; row < types.value.data.ival && styles.size() < 2; ++row) {
            const auto exists = runtime.execute_script("nwn1.doors", "appearance_exists",
                {nw::smalls::Value::make_int(row), nw::smalls::Value::make_int(0)});
            if (exists.ok() && exists.value.data.bval) { styles.push_back(row); }
        }
        ASSERT_EQ(styles.size(), 2);
        for (const auto style : styles) {
            const auto write = runtime.execute_script("nwn1.doors", "set_appearance",
                {value, nw::smalls::Value::make_int(style), nw::smalls::Value::make_int(0)});
            ASSERT_TRUE(write.ok());
            ASSERT_TRUE(write.value.data.bval);
            if (style == styles.front()) {
                click = capture("next");
                ASSERT_TRUE(click);
                EXPECT_EQ(click->selected, catalog.rows.front().id);
            }
        }
        EXPECT_EQ(object_mutation_state().epoch, click->mutation_epoch);
        const auto undo_count = workspace.undo_count();
        EXPECT_EQ(apply(*click), AppearanceCatalogClickEffect::none);
        EXPECT_EQ(workspace.undo_count(), undo_count);
        click = capture("previous");
        ASSERT_TRUE(click);
        EXPECT_EQ(click->selected, catalog.rows.back().id);
        ASSERT_EQ(apply(*click), AppearanceCatalogClickEffect::refresh);
        EXPECT_EQ(door_appearance(runtime, object), (ObjectAppearanceSelectors{0, catalog.rows.back().id}));
    }
}

TEST_F(ClientAppearanceView, NativeCatalogSelectionRejectsChangedFactsAndMalformedPresses)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    auto& appearance = view.appearance_view;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    appearance.appearance_selector_open = true;
    const auto desired = active_appearance_catalog(appearance).rows.front().id;
    document->SetInnerRML("<button id='target' class='appearance_row' data-key='" + std::to_string(desired) + "'>Select</button>");
    const auto captured = capture_appearance_catalog_click(document->GetElementById("target"), appearance,
        object_workbench_target(view, workspace), workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(captured);
    ASSERT_EQ(captured->kind, AppearanceCatalogClickKind::select);
    const auto apply = [&](AppearanceCatalogClick& click) {
        return apply_appearance_catalog_click(click, appearance, object_workbench_target(view, workspace), workspace, backend, shell, command);
    };
    for (const int partition : {0, 1, 2, 3, 4, 5, 6}) {
        SCOPED_TRACE(partition);
        auto click = *captured;
        if (partition == 0) { ++click.module_generation; }
        if (partition == 1) { ++click.resource_generation; }
        if (partition == 2) { ++click.mutation_epoch; }
        if (partition == 3) { click.source_values->front() += 1; }
        if (partition == 4) { appearance.appearance_query = "changed"; }
        if (partition == 5) { appearance.appearance_editor_field = AppearanceEditorField::wings; }
        if (partition == 6) { appearance.appearance_selector_open = false; }
        EXPECT_EQ(apply(click), AppearanceCatalogClickEffect::none);
        appearance.appearance_query.clear();
        appearance.appearance_editor_field = AppearanceEditorField::appearance;
        appearance.appearance_selector_open = true;
    }
    for (const auto* key : {"", "-1", "0tail", "2147483648"}) {
        document->SetInnerRML("<button id='target' class='appearance_row' style='position:absolute;left:20px;top:20px;width:80px;height:40px;' data-key='"
            + std::string{key} + "'>Select</button>");
        context->Update();
        auto* target = document->GetElementById("target");
        context->ProcessMouseMove(40, 40, 0);
        context->ProcessMouseButtonDown(0, 0);
        EXPECT_TRUE(target->IsPseudoClassSet("active"));
        auto click = capture_appearance_catalog_click(target, appearance, object_workbench_target(view, workspace),
            workspace, backend.module_generation(), nw::kernel::resman().generation());
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, AppearanceCatalogClickKind::none);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_LEFT;
        ClientInputDispatchState dispatch;
        EXPECT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset, click->release_phase, context, nullptr, event).performed);
        EXPECT_FALSE(target->IsPseudoClassSet("active"));
        EXPECT_EQ(apply(*click), AppearanceCatalogClickEffect::none);
    }
    auto* replacement = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(replacement, nullptr);
    activate(replacement->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    rebuild_active_appearances(appearance, backend.module_generation(), replacement->handle());
    auto click = *captured;
    EXPECT_EQ(apply(click), AppearanceCatalogClickEffect::none);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    appearance.appearance_selector_open = true;
    click = *captured;
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    EXPECT_EQ(apply(click), AppearanceCatalogClickEffect::none);
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_TRUE(shell.output_lines.empty());
}

TEST_F(ClientAppearanceView, NativeColorChannelPrecedesSdkReleaseAndOwnedPaletteSelectionCommitsOnce)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    auto& appearance = view.appearance_view;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    const auto rows = creature_color_editor_rows(nw::kernel::runtime(), actor->handle());
    ASSERT_EQ(rows.size(), 4);
    ASSERT_TRUE(open_color_editor(appearance, actor->handle(), rows[0].color));
    std::string markup;
    append_creature_color_selector_markup(markup, appearance, object_workbench_target(view, workspace));
    document->SetInnerRML(markup);
    context->Update();
    Rml::ElementList channels;
    document->GetElementsByClassName(channels, "creature_color_channel");
    ASSERT_EQ(channels.size(), rows.size());
    auto* channel = channels[1];
    const Rml::Vector2f channel_point{channel->GetAbsoluteLeft() + channel->GetClientWidth() / 2,
        channel->GetAbsoluteTop() + channel->GetClientHeight() / 2};
    context->ProcessMouseMove(static_cast<int>(channel_point.x), static_cast<int>(channel_point.y), 0);
    context->ProcessMouseButtonDown(0, 0);
    auto click = capture_color_editor_click(channel, channel_point, appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, ColorEditorClickKind::channel);
    ASSERT_EQ(click->release_phase, ClientRmlForwardPhase::after_native);
    EXPECT_EQ(appearance.color_editor_channel, rows[0].color);
    const auto apply = [&](ColorEditorClick& input) {
        return apply_color_editor_click(input, appearance, object_workbench_target(view, workspace), workspace, backend, shell, command);
    };
    ASSERT_TRUE(apply(*click));
    EXPECT_EQ(appearance.color_editor_channel, rows[1].color);
    class ReleaseRecorder final : public Rml::EventListener {
    public:
        ReleaseRecorder(AppearanceViewState& input_state, Rml::ElementDocument& input_doc)
            : state{input_state}
            , doc{input_doc}
        {
        }
        void ProcessEvent(Rml::Event&) override
        {
            ++releases;
            channel = state.color_editor_channel;
            doc.SetInnerRML("<p>Replacement during SDK release</p>");
        }
        AppearanceViewState& state;
        Rml::ElementDocument& doc;
        int releases = 0;
        int channel = -1;
    } recorder{appearance, *document};
    context->AddEventListener("mouseup", &recorder);
    const auto remove = create_scope_exit([&] { context->RemoveEventListener("mouseup", &recorder); });
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    ClientInputDispatchState dispatch;
    EXPECT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset, click->release_phase, context, nullptr, event).performed);
    EXPECT_EQ(recorder.releases, 1);
    EXPECT_EQ(recorder.channel, rows[1].color);
    EXPECT_EQ(dispatch.forwarding_phase, ClientRmlForwardPhase::after_native);
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset, click->release_phase, context, nullptr, event).performed);
    EXPECT_FALSE(apply(*click));
    markup.clear();
    append_creature_color_selector_markup(markup, appearance, object_workbench_target(view, workspace));
    document->SetInnerRML(markup);
    context->Update();
    auto* palette = document->GetElementById("creature_color_palette");
    ASSERT_NE(palette, nullptr);
    ASSERT_GT(palette->GetClientWidth(), 0);
    ASSERT_GT(palette->GetClientHeight(), 0);
    const int desired = (rows[1].value + 17) % (kPltPaletteColumns * kPltPaletteRows);
    const Rml::Vector2f point{palette->GetAbsoluteLeft() + palette->GetClientLeft()
            + palette->GetClientWidth() * ((static_cast<float>(desired % kPltPaletteColumns) + 0.5f) / kPltPaletteColumns),
        palette->GetAbsoluteTop() + palette->GetClientTop()
            + palette->GetClientHeight() * ((static_cast<float>(desired / kPltPaletteColumns) + 0.5f) / kPltPaletteRows)};
    click = capture_color_editor_click(palette, point, appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, ColorEditorClickKind::select);
    EXPECT_EQ(click->selected, desired);
    EXPECT_EQ(click->channel, rows[1].color);
    EXPECT_EQ(click->source_value, rows[1].value);
    EXPECT_EQ(click->palette, rows[1].palette);
    EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
    const auto before = editable_creature_colors(nw::kernel::runtime(), actor->handle());
    document->SetInnerRML("<p>Palette DOM replaced before native application</p>");
    ASSERT_TRUE(apply(*click));
    EXPECT_EQ(workspace.undo_count(), 1);
    EXPECT_EQ(editable_creature_colors(nw::kernel::runtime(), actor->handle())[rows[1].color], desired);
    EXPECT_FALSE(apply(*click));
    EXPECT_EQ(workspace.undo_count(), 1);
    ASSERT_TRUE(workspace.undo(command).ok());
    EXPECT_EQ(editable_creature_colors(nw::kernel::runtime(), actor->handle()), before);
}

TEST_F(ClientAppearanceView, NativeColorFieldClosesSharedSelectorBeforeOpeningAfterDomReplacement)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    auto& appearance = view.appearance_view;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    const auto rows = creature_color_editor_rows(nw::kernel::runtime(), actor->handle());
    ASSERT_GE(rows.size(), 2);
    ASSERT_TRUE(open_color_editor(appearance, actor->handle(), rows[1].color));
    appearance.appearance_selector_open = true;
    document->SetInnerRML("<div class='smalls_selector active'><button class='smalls_selector_close'>Close</button></div>"
                          "<button id='target' class='creature_color_field' data-color='"
        + std::to_string(rows[0].color) + "'>Color</button>");
    auto click = capture_color_editor_click(document->GetElementById("target"), {}, appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(click);
    ASSERT_EQ(click->kind, ColorEditorClickKind::field);
    EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
    class CloseRecorder final : public Rml::EventListener {
    public:
        CloseRecorder(AppearanceViewState& input_state, Rml::ElementDocument& input_doc)
            : state{input_state}
            , doc{input_doc}
        {
        }
        void ProcessEvent(Rml::Event&) override
        {
            ++closes;
            old_channel = state.color_editor_channel;
            old_selector = state.appearance_selector_open;
            doc.SetInnerRML("<p>Replacement during shared selector close</p>");
        }
        AppearanceViewState& state;
        Rml::ElementDocument& doc;
        int closes = 0;
        int old_channel = -1;
        bool old_selector = false;
    } recorder{appearance, *document};
    context->AddEventListener("click", &recorder);
    const auto remove = create_scope_exit([&] { context->RemoveEventListener("click", &recorder); });
    ASSERT_TRUE(close_active_smalls_selector(document));
    EXPECT_EQ(recorder.closes, 1);
    EXPECT_EQ(recorder.old_channel, rows[1].color);
    EXPECT_TRUE(recorder.old_selector);
    ASSERT_TRUE(apply_color_editor_click(*click, appearance, object_workbench_target(view, workspace), workspace, backend, shell, command));
    EXPECT_EQ(appearance.color_editor_channel, rows[0].color);
    EXPECT_FALSE(appearance.appearance_selector_open);
    EXPECT_FALSE(apply_color_editor_click(*click, appearance, object_workbench_target(view, workspace), workspace, backend, shell, command));
    EXPECT_EQ(workspace.undo_count(), 0);
}

TEST_F(ClientAppearanceView, NativeColorSelectionRejectsIndependentSemanticAndOwnerChanges)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    auto& appearance = view.appearance_view;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    const auto rows = creature_color_editor_rows(nw::kernel::runtime(), actor->handle());
    ASSERT_GE(rows.size(), 2);
    ASSERT_TRUE(open_color_editor(appearance, actor->handle(), rows[0].color));
    std::string markup;
    append_creature_color_selector_markup(markup, appearance, object_workbench_target(view, workspace));
    document->SetInnerRML(markup);
    context->Update();
    auto* palette = document->GetElementById("creature_color_palette");
    ASSERT_NE(palette, nullptr);
    const Rml::Vector2f point{palette->GetAbsoluteLeft() + palette->GetClientLeft() + 1,
        palette->GetAbsoluteTop() + palette->GetClientTop() + 1};
    const auto captured = capture_color_editor_click(palette, point, appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(captured);
    ASSERT_EQ(captured->kind, ColorEditorClickKind::select);
    const auto apply = [&](ColorEditorClick& input) {
        return apply_color_editor_click(input, appearance, object_workbench_target(view, workspace), workspace, backend, shell, command);
    };
    const auto before = editable_creature_colors(nw::kernel::runtime(), actor->handle());
    const auto output_count = shell.output_lines.size();
    auto click = *captured;
    ASSERT_TRUE(open_color_editor(appearance, actor->handle(), rows[1].color));
    EXPECT_FALSE(apply(click));
    ASSERT_TRUE(open_color_editor(appearance, actor->handle(), rows[0].color));
    for (const int partition : {0, 1, 2, 3, 4, 5}) {
        SCOPED_TRACE(partition);
        click = *captured;
        if (partition == 0) { ++click.module_generation; }
        if (partition == 1) { ++click.resource_generation; }
        if (partition == 2) { ++click.mutation_epoch; }
        if (partition == 3) { ++click.source_value; }
        if (partition == 4) { ++click.palette; }
        if (partition == 5) { click.selected = kPltPaletteColumns * kPltPaletteRows; }
        EXPECT_FALSE(apply(click));
    }
    EXPECT_EQ(editable_creature_colors(nw::kernel::runtime(), actor->handle()), before);
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_EQ(shell.output_lines.size(), output_count);
    // A real script write bypasses the toolset epoch; the fresh provider-value
    // comparison must reject the old palette request independently of that stamp.
    auto object = nw::smalls::Value::make_object(actor->handle());
    object.type_id = nw::kernel::runtime().object_subtype_for_tag(actor->handle().type);
    const auto changed = (rows[0].value + 1) % (kPltPaletteColumns * kPltPaletteRows);
    const auto write = nw::kernel::runtime().execute_script("nwn1.creature", "set_color",
        {object, nw::smalls::Value::make_int(static_cast<int32_t>(rows[0].color)), nw::smalls::Value::make_int(changed)});
    ASSERT_TRUE(write.ok());
    ASSERT_TRUE(write.value.data.bval);
    EXPECT_EQ(object_mutation_state().epoch, captured->mutation_epoch);
    click = *captured;
    EXPECT_FALSE(apply(click));
    EXPECT_EQ(editable_creature_colors(nw::kernel::runtime(), actor->handle())[rows[0].color], changed);
    EXPECT_EQ(workspace.undo_count(), 0);
    const int desired = (changed + 1) % (kPltPaletteColumns * kPltPaletteRows);
    const Rml::Vector2f fresh_point{palette->GetAbsoluteLeft() + palette->GetClientLeft()
            + palette->GetClientWidth() * ((static_cast<float>(desired % kPltPaletteColumns) + 0.5f) / kPltPaletteColumns),
        palette->GetAbsoluteTop() + palette->GetClientTop()
            + palette->GetClientHeight() * ((static_cast<float>(desired / kPltPaletteColumns) + 0.5f) / kPltPaletteRows)};
    const auto fresh_capture = capture_color_editor_click(palette, fresh_point, appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation());
    ASSERT_TRUE(fresh_capture);
    ASSERT_EQ(fresh_capture->kind, ColorEditorClickKind::select);
    auto* replacement = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(replacement, nullptr);
    ASSERT_NE(replacement->handle(), actor->handle());
    activate(replacement->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    rebuild_active_appearances(appearance, backend.module_generation(), replacement->handle());
    ASSERT_TRUE(open_color_editor(appearance, replacement->handle(), rows[0].color));
    const auto replacement_before = editable_creature_colors(nw::kernel::runtime(), replacement->handle());
    click = *fresh_capture;
    click.mutation_epoch = object_mutation_state().epoch;
    EXPECT_FALSE(apply(click));
    EXPECT_EQ(editable_creature_colors(nw::kernel::runtime(), replacement->handle()), replacement_before);
    EXPECT_EQ(editable_creature_colors(nw::kernel::runtime(), actor->handle())[rows[0].color], changed);
    EXPECT_EQ(workspace.undo_count(), 0);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    ASSERT_TRUE(open_color_editor(appearance, actor->handle(), rows[0].color));
    click = *fresh_capture;
    click.mutation_epoch = object_mutation_state().epoch;
    workspace.open_tab("second", "Second", WorkspaceTabKind::preview);
    command.active_tab_id = workspace.active_tab_id();
    EXPECT_FALSE(apply(click));
    EXPECT_EQ(shell.output_lines.size(), output_count);
}

TEST_F(ClientAppearanceView, RejectedColorControlsKeepSdkReleaseAndCannotIssueCommands)
{
    load_fixture();
    auto* actor = nw::kernel::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(actor, nullptr);
    activate(actor->handle());
    view.object_workbench_surface = ObjectWorkbenchSurface::appearance;
    auto& appearance = view.appearance_view;
    rebuild_active_appearances(appearance, backend.module_generation(), actor->handle());
    for (const auto* key : {"", "-1", "0tail", "2147483648"}) {
        SCOPED_TRACE(key);
        document->SetInnerRML("<button id='target' class='creature_color_field' style='position:absolute;left:20px;top:20px;width:80px;height:40px;' data-color='"
            + std::string{key} + "'>Color</button>");
        context->Update();
        auto* target = document->GetElementById("target");
        ASSERT_NE(target, nullptr);
        context->ProcessMouseMove(40, 40, 0);
        context->ProcessMouseButtonDown(0, 0);
        EXPECT_TRUE(target->IsPseudoClassSet("active"));
        auto click = capture_color_editor_click(target, {40, 40}, appearance, object_workbench_target(view, workspace),
            workspace, backend.module_generation(), nw::kernel::resman().generation());
        ASSERT_TRUE(click);
        EXPECT_EQ(click->kind, ColorEditorClickKind::none);
        EXPECT_EQ(click->release_phase, ClientRmlForwardPhase::before_native);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_LEFT;
        ClientInputDispatchState dispatch;
        EXPECT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset, click->release_phase, context, nullptr, event).performed);
        EXPECT_FALSE(target->IsPseudoClassSet("active"));
        EXPECT_FALSE(apply_color_editor_click(*click, appearance, object_workbench_target(view, workspace), workspace, backend, shell, command));
    }
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_TRUE(shell.output_lines.empty());
    document->SetInnerRML("<button id='unmatched'>Other</button>");
    EXPECT_FALSE(capture_color_editor_click(document->GetElementById("unmatched"), {}, appearance, object_workbench_target(view, workspace),
        workspace, backend.module_generation(), nw::kernel::resman().generation()));
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
