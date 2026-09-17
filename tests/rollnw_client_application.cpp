#include "../tools/client/client_metrics.hpp"
#include "../tools/client/client_preferences.hpp"

#include <nw/render/viewer/session.hpp>

#include <SDL3/SDL.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>

namespace nw::toolset {

class ClientCli : public ::testing::Test {
protected:
    std::string run(std::initializer_list<const char*> arguments, int expected_exit)
    {
        std::vector<const char*> args{ROLLNW_TEST_CLIENT_EXECUTABLE};
        args.insert(args.end(), arguments);
        args.push_back(nullptr);
        std::unique_ptr<SDL_Environment, decltype(&SDL_DestroyEnvironment)> environment{
            SDL_CreateEnvironment(true), SDL_DestroyEnvironment};
        const auto properties = SDL_CreateProperties();
        const bool configured = environment && properties
            && SDL_SetEnvironmentVariable(environment.get(), "SDL_VIDEODRIVER", "client-cli-invalid-driver", true)
            && SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data())
            && SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment.get())
            && SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        std::unique_ptr<SDL_Process, decltype(&SDL_DestroyProcess)> process{
            configured ? SDL_CreateProcessWithProperties(properties) : nullptr, SDL_DestroyProcess};
        SDL_DestroyProperties(properties);
        EXPECT_NE(process, nullptr) << SDL_GetError();
        if (!process) { return {}; }
        size_t size = 0;
        int exit_code = -1;
        std::unique_ptr<void, decltype(&SDL_free)> output{
            SDL_ReadProcess(process.get(), &size, &exit_code), SDL_free};
        EXPECT_TRUE(SDL_WaitProcess(process.get(), true, &exit_code)) << SDL_GetError();
        EXPECT_EQ(exit_code, expected_exit);
        return output ? std::string{static_cast<const char*>(output.get()), size} : std::string{};
    }
};

TEST_F(ClientCli, InformationAndUsagePathsDoNotInitializeVideo)
{
    EXPECT_TRUE(run({"--version"}, 0).starts_with("rollnw-client "));
    const auto info = nlohmann::json::parse(run({"--build-info"}, 0));
    EXPECT_EQ(info["schema"], 1);
    EXPECT_EQ(info["tool"], "rollnw-client");
    EXPECT_TRUE(run({"--help"}, 0).starts_with("Usage:\n"));
    run({"init"}, 2);
    run({"import"}, 2);
    run({"import", "--json", "--legacy", "missing.mod"}, 2);
    run({"import", "--unknown", "missing.mod"}, 2);
    run({"blueprint-update"}, 2);
}

TEST_F(ClientCli, InitializesProjectWithLiteralSpacesWithoutVideo)
{
    const std::filesystem::path path{"tmp/client_cli/project ; $ with spaces"};
    std::filesystem::remove_all(path.parent_path());
    const auto text = path.string();
    EXPECT_TRUE(run({"init", text.c_str()}, 0).starts_with("Initialized rollnw client project:"));
    EXPECT_TRUE(is_project_directory(path));
    EXPECT_TRUE(run({"init", text.c_str()}, 0).starts_with("rollnw client project already initialized:"));
}

class ClientPreferences : public ::testing::Test {
protected:
    void SetUp() override
    {
        path = std::filesystem::path{"tmp/client_preferences"}
            / ::testing::UnitTest::GetInstance()->current_test_info()->name()
            / "preferences.json";
        std::filesystem::remove_all(path.parent_path());
    }

    std::filesystem::path path;
};

TEST_F(ClientPreferences, RoundTripRetainsUnrelatedKeysAndDropsLegacyFields)
{
    DockLayout docks;
    docks.pane(DockRegion::left).size_px = 411;
    docks.pane(DockRegion::bottom).visible = true;
    std::vector<RecentProjectEntry> recent{{"Example", "/example project", "not persisted"}};
    ASSERT_TRUE(save_ui_preferences(path, docks, recent));
    {
        nlohmann::json prefs;
        std::ifstream input{path};
        input >> prefs;
        prefs["other"] = "preserved";
        prefs["left_dock_width_px"] = 12;
        prefs["bottom_dock_height_px"] = 12;
        prefs["terminal_height_px"] = 12;
        std::ofstream output{path};
        output << prefs;
    }
    ASSERT_TRUE(save_ui_preferences(path, docks, recent));
    DockLayout loaded;
    std::vector<RecentProjectEntry> projects;
    load_ui_preferences(path, loaded, projects);
    EXPECT_EQ(loaded.pane(DockRegion::left).size_px, 411);
    EXPECT_TRUE(loaded.pane(DockRegion::bottom).visible);
    ASSERT_EQ(projects.size(), 1u);
    EXPECT_EQ(projects[0].name, "Example");
    EXPECT_EQ(projects[0].path, "/example project");
    EXPECT_TRUE(projects[0].error.empty());
    nlohmann::json prefs;
    std::ifstream input{path};
    input >> prefs;
    EXPECT_EQ(prefs["other"], "preserved");
    EXPECT_FALSE(prefs.contains("left_dock_width_px"));
    EXPECT_FALSE(prefs.contains("bottom_dock_height_px"));
    EXPECT_FALSE(prefs.contains("terminal_height_px"));
}

TEST_F(ClientPreferences, MissingMalformedAndInvalidValuesPreserveExistingPolicy)
{
    DockLayout docks;
    docks.pane(DockRegion::left).size_px = 411;
    std::vector<RecentProjectEntry> recent{{"Default", "/default"}};
    load_ui_preferences(path, docks, recent);
    EXPECT_EQ(docks.pane(DockRegion::left).size_px, 411);
    EXPECT_EQ(recent.size(), 1u);
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream output{path};
        output << "invalid JSON";
    }
    load_ui_preferences(path, docks, recent);
    EXPECT_EQ(docks.pane(DockRegion::left).size_px, 411);
    EXPECT_EQ(recent.size(), 1u);
    {
        std::ofstream output{path};
        output << R"({"ui":{"docks":{"left":{"size_px":-10,"visible":"wrong","active_widget":"missing"}}},"projects":{"recent":[{"path":"/one","name":"One"},{"path":"/one"},{"path":""},7]}})";
    }
    const auto widget = docks.pane(DockRegion::left).active_widget;
    load_ui_preferences(path, docks, recent);
    EXPECT_EQ(docks.pane(DockRegion::left).size_px, 0);
    EXPECT_EQ(docks.pane(DockRegion::left).active_widget, widget);
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0].path, "/one");
    EXPECT_FALSE(save_ui_preferences({}, docks, recent));
    // A regular file cannot be used as the parent of the destination.
    EXPECT_FALSE(save_ui_preferences(path / "blocked.json", docks, recent));
}

TEST(ClientMetrics, PreservesFrameSmoothingAndRejectsNegativeTimes)
{
    ClientMetricsState metrics;
    EXPECT_EQ(format_viewer_fps_rml(metrics, true, nw::render::ForwardPlusDebugMode::off), "-- FPS");
    update_viewer_frame_metrics(metrics, 0.02f);
    update_viewer_frame_metrics(metrics, 0.04f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_frame_seconds, 0.04f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_smoothed_seconds, 0.022f);
    update_viewer_frame_metrics(metrics, -1.0f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_frame_seconds, 0.04f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_smoothed_seconds, 0.022f);
    update_viewer_render_metrics(metrics, 0.01f, 0.002f, 0.003f, 0.004f,
        0.005f, 0.006f, 0.007f, 0.008f, 0.009f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_work_seconds, 0.01f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_sync_seconds, 0.002f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_draw_seconds, 0.003f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_ui_seconds, 0.004f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_view_seconds, 0.005f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_hud_seconds, 0.006f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_overlay_seconds, 0.007f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_palette_seconds, 0.008f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_present_seconds, 0.009f);
}

TEST(ClientMetrics, KeepsLastCountersWhenDelayedGpuSnapshotIsUnavailable)
{
    ClientMetricsState metrics;
    nw::render::viewer::ViewerFrameStats viewer;
    viewer.model_count = 17;
    viewer.total_command_stats.draw_count = 23;
    viewer.forward_plus_light_count = 11;
    update_viewer_internal_metrics(metrics, &viewer);
    update_viewer_internal_metrics(metrics, nullptr);
    EXPECT_EQ(metrics.viewer_fps_model_count, 17u);
    EXPECT_EQ(metrics.viewer_fps_draw_count, 23u);
    EXPECT_EQ(metrics.viewer_fps_forward_plus_light_count, 11u);

    ClientGpuFrameStats gpu;
    gpu.ui_seconds = 0.004f;
    gpu.viewport_seconds = 0.008f;
    gpu.timer_count = 4;
    gpu.command_stats.dropped_draw_count = 3;
    update_client_gpu_metrics(metrics, &gpu);
    update_client_gpu_metrics(metrics, nullptr);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_editor_gpu_ui_seconds, 0.004f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_editor_gpu_viewport_seconds, 0.008f);
    EXPECT_EQ(metrics.viewer_fps_editor_gpu_timer_count, 4u);
    EXPECT_EQ(metrics.viewer_fps_dropped_draw_count, 3u);

    update_viewer_frame_metrics(metrics, 0.02f);
    const auto markup = format_viewer_fps_rml(metrics, true, nw::render::ForwardPlusDebugMode::off);
    EXPECT_NE(markup.find("50.0 FPS"), std::string::npos);
    EXPECT_NE(markup.find("<br/>"), std::string::npos);
    EXPECT_NE(markup.find("4.00"), std::string::npos);
    EXPECT_NE(markup.find("8.00"), std::string::npos);
}

} // namespace nw::toolset
