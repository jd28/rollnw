#include "../tools/client/client_frame.hpp"
#include "../tools/client/client_metrics.hpp"
#include "../tools/client/client_preferences.hpp"
#include "test_nwn_root.hpp"

#include <nw/formats/Image.hpp>
#include <nw/render/viewer/session.hpp>
#include <nw/resources/Erf.hpp>

#include <SDL3/SDL.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>

namespace nw::toolset {

class ClientCli : public ::testing::Test {
protected:
    std::string run(std::initializer_list<const char*> arguments, int expected_exit,
        const std::filesystem::path& working_directory = {})
    {
        std::vector<const char*> args{ROLLNW_TEST_CLIENT_EXECUTABLE};
        args.insert(args.end(), arguments);
        args.push_back(nullptr);
        std::unique_ptr<SDL_Environment, decltype(&SDL_DestroyEnvironment)> environment{
            SDL_CreateEnvironment(true), SDL_DestroyEnvironment};
        const auto install = nw::test::dedicated_server_root();
        EXPECT_TRUE(install.has_value());
        if (!install) { return {}; }
        const auto install_text = install->string();
        const auto user_text = std::filesystem::absolute("test_data/user").string();
        const auto directory_text = working_directory.string();
        const auto properties = SDL_CreateProperties();
        const bool configured = environment && properties
            && SDL_SetEnvironmentVariable(environment.get(), "SDL_VIDEODRIVER", "client-cli-invalid-driver", true)
            && SDL_SetEnvironmentVariable(environment.get(), "NWN_ROOT", install_text.c_str(), true)
            && SDL_SetEnvironmentVariable(environment.get(), "NWN_HOME", user_text.c_str(), true)
            && SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data())
            && SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment.get())
            && (working_directory.empty()
                || SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, directory_text.c_str()))
            && SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP)
            && SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
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

TEST_F(ClientCli, ImportsProjectsFromDifferentWorkingDirectoriesWithoutVideo)
{
    namespace fs = std::filesystem;
    const auto root = fs::absolute("tmp/client_cli_import_directories");
    fs::remove_all(root);
    fs::create_directories(root / "unrelated");
    const auto module_path = root / "module ; $ with spaces.mod";
    nw::Erf source{"test_data/user/modules/DockerDemo.mod"};
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(source.add("test_data/user/development/nw_chicken.utc"));
    ASSERT_TRUE(source.save_as(module_path));
    const auto module = module_path.string();
    const std::array directories{
        fs::path{ROLLNW_TEST_SOURCE_DIR},
        fs::path{ROLLNW_TEST_CLIENT_EXECUTABLE}.parent_path(),
        fs::current_path(),
        root / "unrelated",
    };
    for (size_t index = 0; index < directories.size(); ++index) {
        SCOPED_TRACE(directories[index].string());
        const auto destination = root / std::to_string(index);
        const auto destination_text = destination.string();
        run({"import", "--json", module.c_str(), destination_text.c_str()}, 0, directories[index]);
        EXPECT_TRUE(is_project_directory(destination));
        EXPECT_TRUE(fs::is_regular_file(destination / "shared/module.ifo.json"));
        EXPECT_TRUE(fs::is_regular_file(destination / "shared/areas/start.caf.json"));
        EXPECT_TRUE(fs::is_regular_file(destination / ".rollnw/cache/area_maps/start.png"));
        nlohmann::json module_data;
        std::ifstream{destination / "shared/module.ifo.json"} >> module_data;
        EXPECT_EQ(module_data["$type"], "IFO");
        nlohmann::json area;
        std::ifstream{destination / "shared/areas/start.caf.json"} >> area;
        EXPECT_EQ(area["$type"], "CAF");
        EXPECT_EQ(area["tiles"].size(), 16u);
        nlohmann::json blueprint;
        std::ifstream{destination / "shared/blueprints/creatures/nw_chicken.utc.json"} >> blueprint;
        EXPECT_EQ(blueprint["nwn1.propsets.CreatureAppearance"]["appearance"], 31);
        EXPECT_EQ(blueprint["object"]["resref"], "nw_chicken");
        nw::Image map{destination / ".rollnw/cache/area_maps/start.png"};
        ASSERT_TRUE(map.valid());
        EXPECT_EQ(map.width(), 128u);
        EXPECT_EQ(map.height(), 128u);
        const auto legacy = root / ("legacy_" + std::to_string(index));
        const auto legacy_text = legacy.string();
        run({"import", "--legacy", module.c_str(), legacy_text.c_str()}, 0, directories[index]);
        EXPECT_TRUE(is_project_directory(legacy));
        EXPECT_TRUE(fs::is_regular_file(legacy / "shared/module.ifo"));
        EXPECT_TRUE(fs::is_regular_file(legacy / "shared/areas/start.are"));
        EXPECT_TRUE(fs::is_regular_file(legacy / "shared/blueprints/creatures/nw_chicken.utc"));
    }
}

TEST_F(ClientCli, ImportsRelativePathsAndDefaultDestinationsWithoutChangingWorkingDirectory)
{
    namespace fs = std::filesystem;
    const auto original_directory = fs::current_path();
    const auto root = fs::absolute("tmp/client_cli_relative_imports");
    fs::remove_all(root);
    fs::create_directories(root / "sources");
    fs::copy_file("test_data/user/modules/DockerDemo.mod", root / "sources/module ; $ with spaces.mod");
    run({"import", "--json", "sources/module ; $ with spaces.mod", "explicit project ; $"}, 0, root);
    EXPECT_TRUE(is_project_directory(root / "explicit project ; $"));
    run({"import", "--json", "sources/module ; $ with spaces.mod"}, 0, root);
    EXPECT_TRUE(is_project_directory(root / "module ; $ with spaces"));
    EXPECT_EQ(fs::current_path(), original_directory);
}

TEST_F(ClientCli, ImportFailuresReturnOperationalOrUsageExitsWithoutVideo)
{
    namespace fs = std::filesystem;
    const auto root = fs::absolute("tmp/client_cli_import_failures");
    fs::remove_all(root);
    fs::create_directories(root);
    std::ofstream{root / "invalid.mod"} << "Not a module";
    for (const auto* format : {"--json", "--legacy"}) {
        EXPECT_NE(run({"import", format, "missing.mod", "project"}, 1, root).find("Module file does not exist"), std::string::npos);
        run({"import", format, "invalid.mod", "project"}, 1, root);
    }
    run({"import", "--json", "--legacy", "missing.mod"}, 2, root);
    EXPECT_FALSE(fs::exists(root / "project"));
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

TEST_F(ClientPreferences, ToolsetLanguagePersistsWithoutReplacingOtherPreferences)
{
    DockLayout docks;
    std::vector<RecentProjectEntry> recent{{"Example", "/example"}};
    ASSERT_TRUE(save_ui_preferences(path, docks, recent));
    ASSERT_TRUE(save_toolset_language_preference(path, LanguageID::french));
    DockLayout loaded;
    std::vector<RecentProjectEntry> projects;
    LanguageID language = LanguageID::english;
    load_ui_preferences(path, loaded, projects, &language);
    EXPECT_EQ(language, LanguageID::french);
    ASSERT_EQ(projects.size(), 1u);
    EXPECT_EQ(projects[0].path, "/example");
    ASSERT_TRUE(save_ui_preferences(path, docks, recent));
    language = LanguageID::english;
    load_ui_preferences(path, loaded, projects, &language);
    EXPECT_EQ(language, LanguageID::french);
    EXPECT_FALSE(save_toolset_language_preference(path, LanguageID::invalid));
    {
        nlohmann::json prefs;
        std::ifstream input{path};
        input >> prefs;
        prefs["ui"]["toolset_language"] = "unknown";
        std::ofstream output{path};
        output << prefs;
    }
    language = LanguageID::english;
    load_ui_preferences(path, loaded, projects, &language);
    EXPECT_EQ(language, LanguageID::english);
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

TEST(ClientMetrics, PreservesBothHudLayoutsAndOwnsUnavailableSnapshotHistory)
{
    ClientMetricsState metrics;
    nw::render::viewer::ViewerFrameStats viewer;
    viewer.tick_seconds = .001f;
    viewer.area_prepare_seconds = .000125f;
    viewer.total_render_seconds = .009f;
    viewer.gpu_shadow_seconds = .002f;
    viewer.gpu_opaque_seconds = .003f;
    viewer.gpu_timer_count = 2;
    viewer.model_count = 17;
    viewer.particle_system_count = 4;
    viewer.render_model_animation_sample_stats.input_count = 19;
    viewer.render_model_animation_sample_stats.sampled_count = 18;
    viewer.prepared_model_surface_stats.draw_count = 21;
    viewer.prepared_render_model_skin_table_stats.matrix_count = 22;
    viewer.area_cache_record_count = 23;
    viewer.area_cache_nonempty_chunk_count = 2;
    viewer.area_cache_chunk_count = 3;
    viewer.area_frame_visible_record_count = 24;
    viewer.area_frame_visible_chunk_count = 2;
    viewer.area_frame_uses_cached_draw_lists = true;
    viewer.forward_plus_light_count = 11;
    viewer.forward_plus_cluster_count = 12;
    viewer.forward_plus_active_cluster_count = 10;
    viewer.forward_plus_upload_bytes = 2048;
    viewer.shadow_resolution = 1024;
    viewer.shadow_caster_model_count = 13;
    viewer.main_pass_count = 3;
    viewer.total_command_stats.draw_count = 4294967299ULL;
    viewer.total_command_stats.indirect_draw_call_count = 7;
    viewer.total_command_stats.uniform_allocation_bytes = 3072;
    viewer.shadow_command_stats.draw_count = 14;
    viewer.transparent_command_stats.draw_count = 15;
    viewer.particle_command_stats.draw_count = 16;
    viewer.tile_grid_index_count = 12672;
    viewer.tile_grid_command_stats.draw_count = 1;
    ClientGpuFrameStats gpu;
    gpu.ui_seconds = .004f;
    gpu.viewport_seconds = .008f;
    gpu.total_seconds = .012f;
    gpu.timer_count = 4;
    gpu.command_stats.descriptor_ring_capacity_bytes = 4096;
    gpu.command_stats.descriptor_ring_required_bytes = 2048;
    gpu.command_stats.descriptor_allocation_failure_count = 1;
    gpu.command_stats.resource_bind_failure_count = 2;
    gpu.command_stats.dropped_draw_count = 3;
    update_viewer_frame_metrics(metrics, .02f);
    update_viewer_render_metrics(metrics, .01f, .002f, .003f, .004f, .005f, .006f, .007f, .008f, .009f);
    update_viewer_internal_metrics(metrics, &viewer);
    update_client_gpu_metrics(metrics, &gpu);
    const auto markup = format_viewer_fps_rml(metrics, true, nw::render::ForwardPlusDebugMode::off);
    const std::string compact = R"RML(50.0 FPS frame 20.0 | view 5.0 ui 4.0 present 9.0 ms<br/>gpu vp 8.00 pass 5.00 ui 4.00 ov 0.00 pal 0.00 ms<br/>vis 24 chunks 2 lights 11 | draws 4294967299 ind 7 grid 12672/1)RML";
    const std::string verbose = R"RML(50.0 FPS | frame 20.0 ms<br/>work 10.0 sync 2.0 cpu-draw 3.0 present 9.0 ms<br/>ui 4.0 view 5.0 hud 6.0 overlay 7.0 palette 8.0 ms<br/>view total 9.0 tick 1.0 setup 0.0 prep 0.125 shadow 0.0 particles 0.0 debug 0.0 ms<br/>passes opaque 0.0 water 0.0 trans 0.0 ms | models 17 ps 4 lights 0/0 c0.00 i0.00 lit 0/0/0 lc0 c0.00 i0.00 sh 0 pass 3<br/>shadow res 1024 casters 13 no-caster 0 submitted 0 culled 0<br/>rmodel samples in 19 ok 18 dis 0 miss 0 badskel 0 fail 0 | surf 21 rm 0 skin 0 assign 0 entries 0 mats 22 bind 0 invalid 0<br/>gpu total 5.00 opaque 3.00 shadow 2.00 water 0.00 trans 0.00 ps 0.00 debug 0.00 ms timers 2<br/>gpu editor total 12.00 ui 4.00 viewport 8.00 overlay 0.00 palette 0.00 ms timers 4<br/>area cache rec 23 static 0 dyn 0 prep draws 0 lights 0 max 0 chunks 2/3 max 0 pass 0/0/0 sh 0<br/>area frame vis 24 static 0 dyn 0 prep surf 0 chunks 2 lists 0/0/0 sh 0 cached 1<br/>f+ on lights 11 clusters 10/12 refs 0 max 0 ov 0/0 upload 2.0 KB tile 0 z 0 dbg off<br/>submit draws 4294967299 ind 7 inst 0 idx 0.0M sh 14 trans 15 ps 16 grid 12672/1 | pipe 0/0 res 0/0 ubos 0 3.0 KB desc 2.0/4.0 KB fail 1/2 drop 3)RML";
    // The environment-selected layout is cached once per process. Execute this
    // production case in separate compact/verbose processes; accept both here.
    EXPECT_TRUE(markup == compact || markup == verbose) << markup;
    viewer = {};
    gpu = {};
    update_viewer_internal_metrics(metrics, nullptr);
    update_client_gpu_metrics(metrics, nullptr);
    EXPECT_EQ(format_viewer_fps_rml(metrics, true, nw::render::ForwardPlusDebugMode::off), markup);

    // A present negative timing sample replaces counters but retains the last
    // valid timing history. The original owned input is no longer available.
    viewer.tick_seconds = -1.0f;
    viewer.gpu_shadow_seconds = -1.0f;
    viewer.gpu_opaque_seconds = -1.0f;
    viewer.gpu_water_seconds = -1.0f;
    viewer.gpu_transparent_seconds = -1.0f;
    viewer.gpu_particles_seconds = -1.0f;
    viewer.gpu_debug_seconds = -1.0f;
    gpu.ui_seconds = -1.0f;
    gpu.viewport_seconds = -1.0f;
    gpu.total_seconds = -1.0f;
    update_viewer_internal_metrics(metrics, &viewer);
    update_client_gpu_metrics(metrics, &gpu);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_tick_seconds, .001f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_tick_smoothed_seconds, .001f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_gpu_shadow_seconds, .002f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_gpu_opaque_seconds, .003f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_gpu_total_seconds, .005f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_editor_gpu_ui_seconds, .004f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_editor_gpu_viewport_seconds, .008f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_editor_gpu_total_seconds, .012f);
    update_viewer_internal_metrics(metrics, nullptr);
    update_client_gpu_metrics(metrics, nullptr);
    const auto without_timers = format_viewer_fps_rml(metrics, true, nw::render::ForwardPlusDebugMode::off);
    EXPECT_EQ(without_timers.find("gpu"), std::string::npos);
    EXPECT_EQ(without_timers.find("4294967299"), std::string::npos);
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
    EXPECT_EQ(metrics.viewer_stats.model_count, 17u);
    EXPECT_EQ(metrics.viewer_stats.total_command_stats.draw_count, 23u);
    EXPECT_EQ(metrics.viewer_stats.forward_plus_light_count, 11u);

    ClientGpuFrameStats gpu;
    gpu.ui_seconds = 0.004f;
    gpu.viewport_seconds = 0.008f;
    gpu.timer_count = 4;
    gpu.command_stats.dropped_draw_count = 3;
    update_client_gpu_metrics(metrics, &gpu);
    update_client_gpu_metrics(metrics, nullptr);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_editor_gpu_ui_seconds, 0.004f);
    EXPECT_FLOAT_EQ(metrics.viewer_fps_editor_gpu_viewport_seconds, 0.008f);
    EXPECT_EQ(metrics.editor_gpu_stats.timer_count, 4u);
    EXPECT_EQ(metrics.editor_gpu_stats.command_stats.dropped_draw_count, 3u);

    update_viewer_frame_metrics(metrics, 0.02f);
    const auto markup = format_viewer_fps_rml(metrics, true, nw::render::ForwardPlusDebugMode::off);
    EXPECT_NE(markup.find("50.0 FPS"), std::string::npos);
    EXPECT_NE(markup.find("<br/>"), std::string::npos);
    EXPECT_NE(markup.find("4.00"), std::string::npos);
    EXPECT_NE(markup.find("8.00"), std::string::npos);
}

TEST(ClientApplicationFrames, ActualSdkSamplesFollowTheRawCounterContract)
{
    std::array<ClientFrameClock, 1> clocks{{{SDL_GetTicks(), SDL_GetPerformanceCounter()}}};
    const auto previous = clocks.front();
    const std::array samples{ClientFrameSample{SDL_GetTicks(), SDL_GetPerformanceCounter(), SDL_GetPerformanceFrequency()}};
    ASSERT_GT(samples.front().frequency, 0);
    std::array<ClientFrameDelta, 1> deltas{};
    ASSERT_TRUE(advance_client_frames(clocks, samples, deltas));
    const float expected = samples.front().counter > previous.counter
        ? static_cast<float>(static_cast<double>(samples.front().counter - previous.counter) / static_cast<double>(samples.front().frequency))
        : 0;
    EXPECT_EQ(deltas.front().raw_seconds, expected);
    EXPECT_GE(deltas.front().camera_milliseconds, 0);
    EXPECT_LE(deltas.front().camera_milliseconds, 100);
    EXPECT_EQ(clocks.front().counter, samples.front().counter);
    EXPECT_EQ(clocks.front().ticks, samples.front().ticks);
}

} // namespace nw::toolset
