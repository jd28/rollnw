#include "area_map.hpp"
#include "command_bus.hpp"
#include "forward_plus_debug.hpp"
#include "project.hpp"
#include "resource_document.hpp"
#include "script_commands.hpp"
#include "shell_controller.hpp"
#include "terminal.hpp"
#include "workspace.hpp"

#include "nw/formats/Image.hpp"
#include "nw/kernel/Kernel.hpp"
#include "nw/objects/Area.hpp"
#include "nw/objects/Module.hpp"
#include "nw/objects/ObjectManager.hpp"
#include "nw/resources/Erf.hpp"
#include "nw/resources/ResourceManager.hpp"
#include "nw/serialization/Gff.hpp"
#include "nw/serialization/GffBuilder.hpp"
#include "nw/smalls/Smalls.hpp"
#include "nw/smalls/runtime.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nw::toolset {

namespace {

CommandResult test_result(CommandStatus status = CommandStatus::success, std::string message = {})
{
    CommandResult result;
    result.status = status;
    result.message = std::move(message);
    result.output_channel = CommandOutputChannel::info;
    return result;
}

CommandContext test_context(WorkspaceState* workspace = nullptr)
{
    CommandContext context;
    context.workspace = workspace;
    if (workspace) {
        context.active_tab_id = workspace->active_tab_id();
    }
    return context;
}

CommandSpec spec(std::string id, std::vector<std::string> aliases = {})
{
    CommandSpec out;
    out.id = std::move(id);
    out.title = out.id;
    out.description = out.id;
    out.category = "test";
    out.aliases = std::move(aliases);
    return out;
}

std::filesystem::path docker_demo_module_path()
{
    const std::filesystem::path repo_root_path = "tests/test_data/user/modules/DockerDemo.mod";
    if (std::filesystem::exists(repo_root_path)) {
        return repo_root_path;
    }
    return "test_data/user/modules/DockerDemo.mod";
}

std::filesystem::path development_resource_path(std::string_view filename)
{
    const std::filesystem::path repo_root_path = std::filesystem::path{"tests/test_data/user/development"} / std::string{filename};
    if (std::filesystem::exists(repo_root_path)) {
        return repo_root_path;
    }
    return std::filesystem::path{"test_data/user/development"} / std::string{filename};
}

std::filesystem::path dialog_tlk_path()
{
    const std::filesystem::path repo_root_path = "tests/test_data/root/lang/en/data/dialog.tlk";
    if (std::filesystem::exists(repo_root_path)) {
        return repo_root_path;
    }
    return "test_data/root/lang/en/data/dialog.tlk";
}

void update_imported_module_metadata(const std::filesystem::path& root,
    const std::vector<std::string>& haks,
    std::string tlk = {})
{
    const auto module_path = root / "shared" / "module.ifo.json";
    std::ifstream input{module_path};
    ASSERT_TRUE(input);

    nlohmann::json module;
    input >> module;
    module["haks"] = haks;
    module["tlk"] = std::move(tlk);

    std::ofstream output{module_path, std::ios::binary};
    ASSERT_TRUE(output);
    output << module.dump(2) << '\n';
}

} // namespace

TEST(ClientCommandBus, RejectsDuplicatesAndResolvesAliases)
{
    CommandBus bus;
    std::string error;

    EXPECT_TRUE(bus.register_command(spec("test.alpha", {"alpha"}), [](const CommandInvocation&, CommandContext&) { return test_result(); }, &error));

    EXPECT_FALSE(bus.register_command(spec("test.alpha"), [](const CommandInvocation&, CommandContext&) { return test_result(); }, &error));
    EXPECT_NE(error.find("duplicate command id"), std::string::npos);

    EXPECT_FALSE(bus.register_command(spec("test.beta", {"alpha"}), [](const CommandInvocation&, CommandContext&) { return test_result(); }, &error));
    EXPECT_NE(error.find("duplicate command alias"), std::string::npos);

    EXPECT_TRUE(bus.has_command("alpha"));
    EXPECT_EQ(bus.resolve_id("alpha"), "test.alpha");

    auto result = bus.execute("alpha", {}, test_context());
    EXPECT_TRUE(result.ok());
}

TEST(ClientCommandBus, HiddenCommandsAreFilteredByDefault)
{
    CommandBus bus;
    auto visible = spec("test.visible");
    auto hidden = spec("test.hidden");
    hidden.flags = CommandFlags::hidden;

    EXPECT_TRUE(bus.register_command(std::move(visible),
        [](const CommandInvocation&, CommandContext&) { return test_result(); }));
    EXPECT_TRUE(bus.register_command(std::move(hidden),
        [](const CommandInvocation&, CommandContext&) { return test_result(); }));

    EXPECT_EQ(bus.list_commands().size(), 1);
    EXPECT_EQ(bus.list_commands(true).size(), 2);
}

TEST(ClientTerminal, ParsesQuotedArgsAndDispatchesAliases)
{
    TerminalDispatcher terminal;
    const auto parsed = terminal.parse("open \"path with spaces.mod\" 'area one'");
    ASSERT_TRUE(parsed.error.empty());
    ASSERT_FALSE(parsed.empty);
    ASSERT_EQ(parsed.invocation.command_id, "open");
    ASSERT_EQ(parsed.invocation.args.size(), 2);
    EXPECT_EQ(command_arg_string(parsed.invocation.args, 0), "path with spaces.mod");
    EXPECT_EQ(command_arg_string(parsed.invocation.args, 1), "area one");

    CommandBus bus;
    EXPECT_TRUE(bus.register_command(spec("toolset.open", {"open", "toolset.open_module"}),
        [](const CommandInvocation& invocation, CommandContext&) {
            EXPECT_EQ(invocation.command_id, "toolset.open");
            EXPECT_EQ(command_arg_string(invocation.args, 0), "path with spaces.mod");
            return test_result(CommandStatus::success, "opened");
        }));

    const auto result = terminal.execute(bus, "open \"path with spaces.mod\"", test_context());
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.message, "opened");
}

TEST(ClientTerminal, CompletesCommandsFromBusSpecs)
{
    CommandBus bus;
    TerminalDispatcher terminal;

    EXPECT_TRUE(bus.register_command(spec("toolset.open", {"open", "toolset.open_module"}),
        [](const CommandInvocation&, CommandContext&) { return test_result(); }));
    EXPECT_TRUE(bus.register_command(spec("toolset.open_recent", {"recent"}),
        [](const CommandInvocation&, CommandContext&) { return test_result(); }));
    EXPECT_TRUE(bus.register_command(spec("command.undo", {"undo"}),
        [](const CommandInvocation&, CommandContext&) { return test_result(); }));

    auto alias = terminal.complete(bus, "op");
    EXPECT_TRUE(alias.completed);
    EXPECT_FALSE(alias.ambiguous);
    EXPECT_EQ(alias.replacement, "toolset.open ");
    EXPECT_EQ(alias.cursor_byte_position, alias.replacement.size());

    auto canonical = terminal.complete(bus, "toolset.op");
    EXPECT_TRUE(canonical.completed);
    EXPECT_TRUE(canonical.ambiguous);
    EXPECT_EQ(canonical.replacement, "toolset.open");
    EXPECT_EQ(canonical.cursor_byte_position, canonical.replacement.size());
    ASSERT_EQ(canonical.candidates.size(), 2);
    EXPECT_EQ(canonical.candidates[0].id, "toolset.open");
    EXPECT_EQ(canonical.candidates[1].id, "toolset.open_recent");

    const std::string line_with_args = "toolset.op path/to/module.mod";
    auto at_end_of_args = terminal.complete(bus, line_with_args);
    EXPECT_FALSE(at_end_of_args.completed);
    EXPECT_TRUE(at_end_of_args.candidates.empty());

    auto at_command = terminal.complete(bus, line_with_args, std::string_view("toolset.op").size());
    EXPECT_TRUE(at_command.completed);
    EXPECT_EQ(at_command.replacement, "toolset.open path/to/module.mod");
    EXPECT_EQ(at_command.cursor_byte_position, std::string_view("toolset.open").size());
}

TEST(ClientTerminal, HandlesEmptyUnknownAndUnterminatedInput)
{
    TerminalDispatcher terminal;
    EXPECT_TRUE(terminal.parse("   ").empty);

    CommandBus bus;
    auto unknown = terminal.execute(bus, "missing", test_context());
    EXPECT_EQ(unknown.status, CommandStatus::unknown_command);

    auto unterminated = terminal.parse("open \"unterminated");
    EXPECT_FALSE(unterminated.error.empty());
}

TEST(ClientDockLayout, ActivatesBottomWidgets)
{
    DockLayout docks;
    const auto& left = docks.pane(DockRegion::left);
    const auto& right = docks.pane(DockRegion::right);
    const auto& bottom = docks.pane(DockRegion::bottom);

    EXPECT_FALSE(left.visible);
    EXPECT_EQ(left.size_px, 360);
    EXPECT_EQ(left.min_size_px, 260);
    EXPECT_EQ(left.max_size_px, 640);
    EXPECT_EQ(left.active_widget, "project_navigator");
    EXPECT_TRUE(docks.contains_widget(DockRegion::left, "project_navigator"));
    EXPECT_TRUE(docks.contains_widget(DockRegion::left, "area_navigator"));

    EXPECT_FALSE(right.visible);
    EXPECT_EQ(right.size_px, 320);
    EXPECT_EQ(right.active_widget, "inspector");

    EXPECT_FALSE(bottom.visible);
    EXPECT_EQ(bottom.active_widget, "output");
    EXPECT_TRUE(docks.contains_widget(DockRegion::bottom, "terminal"));

    EXPECT_TRUE(docks.activate_widget(DockRegion::bottom, "terminal"));
    EXPECT_TRUE(docks.pane(DockRegion::bottom).visible);
    EXPECT_EQ(docks.pane(DockRegion::bottom).active_widget, "terminal");
    EXPECT_FALSE(docks.activate_widget(DockRegion::bottom, "missing"));
}

TEST(ClientShellController, ClearTerminalDropsBufferedLines)
{
    ShellController shell;
    shell.terminal_dirty = false;

    shell.append_terminal("cmd", "> test");
    shell.append_terminal("error", "Unknown command");
    ASSERT_EQ(shell.terminal_lines.size(), 2);

    shell.terminal_dirty = false;
    shell.clear_terminal();

    EXPECT_TRUE(shell.terminal_lines.empty());
    EXPECT_TRUE(shell.terminal_dirty);
}

TEST(ClientShellController, OutputChannelsToggleIndependently)
{
    ShellController shell;

    EXPECT_TRUE(shell.output_channel_visible("info"));
    EXPECT_TRUE(shell.output_channel_visible("warn"));
    EXPECT_TRUE(shell.output_channel_visible("error"));
    EXPECT_TRUE(shell.output_channel_visible("script"));

    shell.output_dirty = false;
    EXPECT_TRUE(shell.toggle_output_channel("warn"));
    EXPECT_FALSE(shell.output_channel_visible("warn"));
    EXPECT_TRUE(shell.output_channel_visible("info"));
    EXPECT_TRUE(shell.output_dirty);

    shell.output_dirty = false;
    EXPECT_TRUE(shell.toggle_output_channel("warn"));
    EXPECT_TRUE(shell.output_channel_visible("warn"));
    EXPECT_TRUE(shell.output_dirty);

    shell.output_dirty = false;
    EXPECT_FALSE(shell.toggle_output_channel("all"));
    EXPECT_FALSE(shell.output_dirty);
}

TEST(ClientShellController, OutputFollowTailTracksScrollPositionAcrossAppends)
{
    ShellController shell;

    EXPECT_TRUE(shell.output_follows_tail());

    shell.observe_output_scroll(59.0f, 160.0f, 100.0f);
    EXPECT_TRUE(shell.output_follows_tail());

    shell.observe_output_scroll(40.0f, 160.0f, 100.0f);
    EXPECT_FALSE(shell.output_follows_tail());
    shell.append_output("info", "new while detached");
    EXPECT_FALSE(shell.output_follows_tail());

    shell.observe_output_scroll(60.0f, 160.0f, 100.0f);
    EXPECT_TRUE(shell.output_follows_tail());
    shell.append_output("info", "new while following");
    EXPECT_TRUE(shell.output_follows_tail());
}

TEST(ClientShellController, OutputFollowTailRejectsInvalidGeometry)
{
    ShellController shell;

    shell.observe_output_scroll(-1.0f, 160.0f, 100.0f);
    EXPECT_FALSE(shell.output_follows_tail());

    shell.observe_output_scroll(0.0f, 80.0f, 100.0f);
    EXPECT_TRUE(shell.output_follows_tail());
}

TEST(ClientShellController, TerminalAndOutputShareBottomDock)
{
    ShellController shell;

    EXPECT_FALSE(shell.bottom_dock_visible());
    EXPECT_FALSE(shell.output_panel_visible());
    EXPECT_FALSE(shell.terminal_visible());

    shell.set_terminal_visible(true);
    EXPECT_TRUE(shell.bottom_dock_visible());
    EXPECT_TRUE(shell.terminal_visible());
    EXPECT_FALSE(shell.output_panel_visible());

    shell.set_output_panel_visible(true);
    EXPECT_TRUE(shell.bottom_dock_visible());
    EXPECT_TRUE(shell.output_panel_visible());
    EXPECT_FALSE(shell.terminal_visible());

    shell.set_output_panel_visible(false);
    EXPECT_FALSE(shell.bottom_dock_visible());
    EXPECT_FALSE(shell.output_panel_visible());

    EXPECT_TRUE(shell.activate_bottom_dock_widget("terminal"));
    EXPECT_TRUE(shell.bottom_dock_visible());
    EXPECT_TRUE(shell.terminal_visible());
    EXPECT_FALSE(shell.activate_bottom_dock_widget("missing"));
}

TEST(ClientForwardPlusDebug, ParsesLabelsAndCyclesModes)
{
    using nw::render::ForwardPlusDebugMode;

    EXPECT_STREQ(forward_plus_debug_mode_label(ForwardPlusDebugMode::off), "off");
    EXPECT_STREQ(forward_plus_debug_mode_label(ForwardPlusDebugMode::cluster_light_count), "cluster-lights");
    EXPECT_STREQ(forward_plus_debug_mode_label(ForwardPlusDebugMode::depth_slice), "depth-slices");

    EXPECT_EQ(next_forward_plus_debug_mode(ForwardPlusDebugMode::off),
        ForwardPlusDebugMode::cluster_light_count);
    EXPECT_EQ(next_forward_plus_debug_mode(ForwardPlusDebugMode::cluster_light_count),
        ForwardPlusDebugMode::depth_slice);
    EXPECT_EQ(next_forward_plus_debug_mode(ForwardPlusDebugMode::depth_slice),
        ForwardPlusDebugMode::off);

    EXPECT_EQ(parse_forward_plus_debug_mode("off"), ForwardPlusDebugMode::off);
    EXPECT_EQ(parse_forward_plus_debug_mode("cluster_lights"), ForwardPlusDebugMode::cluster_light_count);
    EXPECT_EQ(parse_forward_plus_debug_mode("DEPTH-SLICES"), ForwardPlusDebugMode::depth_slice);
    EXPECT_FALSE(parse_forward_plus_debug_mode("invalid").has_value());
}

TEST(ClientProject, InitializesProjectSkeleton)
{
    const std::filesystem::path root = "tmp/client_project_init";
    std::filesystem::remove_all(root);

    const auto init = initialize_project(root, "Example");
    ASSERT_TRUE(init.ok) << init.message;
    EXPECT_TRUE(init.initialized);
    EXPECT_TRUE(is_project_directory(root));
    EXPECT_TRUE(std::filesystem::exists(root / "rollnw.json"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "shared"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "shared" / "areas"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "shared" / "blueprints" / "creatures"));
    EXPECT_TRUE(std::filesystem::is_directory(root / ".rollnw" / "cache"));

    std::ifstream input{root / "rollnw.json"};
    ASSERT_TRUE(input);
    nlohmann::json manifest;
    input >> manifest;
    EXPECT_EQ(manifest["format"], "rollnw.module");
    EXPECT_EQ(manifest["version"], 1);
    EXPECT_EQ(manifest["name"], "Example");

    const auto second_init = initialize_project(root, "Ignored");
    EXPECT_TRUE(second_init.ok);
    EXPECT_FALSE(second_init.initialized);
}

TEST(ClientProject, JsonImportAutoInitializesTargetDirectory)
{
    const std::filesystem::path root = "tmp/client_project_import";
    std::filesystem::remove_all(root);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto module_path = docker_demo_module_path();
    const auto result = import_module_project(module_path, root, options);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.initialized);
    EXPECT_GT(result.resource_count, 0);
    EXPECT_EQ(result.area_map_count, 1);
    EXPECT_LE(result.area_map_degraded_count, result.area_map_count);
    EXPECT_EQ(result.area_map_failure_count, 0);
    EXPECT_TRUE(is_project_directory(root));
    EXPECT_TRUE(std::filesystem::exists(root / "shared" / "module.ifo.json"));
    EXPECT_TRUE(std::filesystem::exists(root / "shared" / "areas" / "start.caf.json"));
    const auto area_map_path = project_area_map_path(root, "start");
    EXPECT_TRUE(std::filesystem::exists(area_map_path));
    const nw::Image area_map{area_map_path};
    ASSERT_TRUE(area_map.valid());
    EXPECT_EQ(area_map.width(), 128);
    EXPECT_EQ(area_map.height(), 128);
    EXPECT_FALSE(std::filesystem::exists(root / "shared" / "areas" / "start.are"));
    EXPECT_FALSE(std::filesystem::exists(root / "shared" / "areas" / "start.git"));
    EXPECT_FALSE(std::filesystem::exists(root / "shared" / "areas" / "start.gic"));
    EXPECT_TRUE(std::filesystem::exists(root / "shared" / "palettes" / "creaturepalcus.itp"));
    EXPECT_TRUE(std::filesystem::exists(root / "shared" / "factions" / "repute.fac.json"));
    EXPECT_FALSE(std::filesystem::exists(root / "shared" / "module.ifo.gffjson"));
    EXPECT_FALSE(std::filesystem::exists(root / "shared" / "palettes" / "creaturepalcus.itp.json"));

    std::ifstream input{root / "rollnw.json"};
    ASSERT_TRUE(input);
    nlohmann::json manifest;
    input >> manifest;
    EXPECT_EQ(manifest["module"], "shared/module.ifo.json");

    const auto import_again = import_module_project(module_path, root, options);
    EXPECT_TRUE(import_again.ok);
    EXPECT_FALSE(import_again.initialized);
}

TEST(ClientProject, AreaMapOutputKeepsWholeLargeAreasWithinBound)
{
    const auto current_max = area_map_output_size(32, 20);
    ASSERT_TRUE(current_max);
    EXPECT_EQ(current_max->width, 1024);
    EXPECT_EQ(current_max->height, 640);
    EXPECT_EQ(current_max->tile_pixels, 32);

    const auto larger = area_map_output_size(128, 64);
    ASSERT_TRUE(larger);
    EXPECT_EQ(larger->width, 2048);
    EXPECT_EQ(larger->height, 1024);
    EXPECT_EQ(larger->tile_pixels, 16);

    const auto largest_supported = area_map_output_size(2048, 2);
    ASSERT_TRUE(largest_supported);
    EXPECT_EQ(largest_supported->width, 2048);
    EXPECT_EQ(largest_supported->height, 2);
    EXPECT_EQ(largest_supported->tile_pixels, 1);

    EXPECT_FALSE(area_map_output_size(0, 8));
    EXPECT_FALSE(area_map_output_size(2049, 8));
}

TEST(ClientProject, JsonImportWritesManagedCreaturePropsets)
{
    const std::filesystem::path root = "tmp/client_project_import_creature";
    const std::filesystem::path module_path = "tmp/client_project_import_creature.mod";
    std::filesystem::remove_all(root);
    std::filesystem::remove(module_path);

    nw::Erf module{docker_demo_module_path()};
    ASSERT_TRUE(module.valid());
    ASSERT_TRUE(module.add(development_resource_path("pl_agent_001.utc")));
    ASSERT_TRUE(module.save_as(module_path));

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(module_path, root, options);
    ASSERT_TRUE(result.ok) << result.message;

    std::ifstream input{root / "shared" / "blueprints" / "creatures" / "pl_agent_001.utc.json"};
    ASSERT_TRUE(input);
    nlohmann::json creature;
    input >> creature;
    EXPECT_TRUE(creature.contains("components"));
    EXPECT_TRUE(creature.contains("nwn1.propsets.CreatureAppearance"));
    EXPECT_FALSE(creature.contains("appearance"));
    EXPECT_FALSE(creature.contains("common"));
}

TEST(ClientProject, JsonImportLoadsThroughResourceManager)
{
    const std::filesystem::path root = "tmp/client_project_import_loadable";
    std::filesystem::remove_all(root);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(std::filesystem::exists(root / "shared" / "module.ifo.json"));

    auto* module = nw::kernel::load_module(root, false);
    ASSERT_TRUE(module);
    EXPECT_TRUE(nw::kernel::resman().contains(nw::Resource{nw::StringView{"module"}, nw::ResourceType::ifo}));
    ASSERT_TRUE(module->areas.is<nw::Vector<nw::Resref>>());
    const auto& areas = module->areas.as<nw::Vector<nw::Resref>>();
    ASSERT_EQ(areas.size(), 1);
    EXPECT_EQ(areas[0].view(), "start");

    auto* area = nw::kernel::objects().make_area(areas[0]);
    ASSERT_NE(area, nullptr);
    EXPECT_FALSE(area->tiles.empty());
    area->clear();
    nw::kernel::objects().destroy(area->handle());
}

TEST(ClientProject, JsonImportRejectsIncompleteModuleHakStack)
{
    const std::filesystem::path root = "tmp/client_project_import_missing_hak";
    const std::filesystem::path module_path = "tmp/client_project_import_missing_hak.mod";
    std::filesystem::remove_all(root);
    std::filesystem::remove(module_path);

    nw::Erf module{docker_demo_module_path()};
    ASSERT_TRUE(module.valid());

    const nw::Resource module_resource{nw::StringView{"module"}, nw::ResourceType::ifo};
    nw::Gff module_gff{module.demand(module_resource)};
    ASSERT_TRUE(module_gff.valid());

    nw::Module module_metadata;
    ASSERT_TRUE(nw::deserialize(&module_metadata, module_gff.toplevel()));
    module_metadata.haks.push_back("missing_client_import_hak");
    const auto serialized_module = nw::serialize(&module_metadata);
    ASSERT_TRUE(module.add(module_resource, serialized_module.to_byte_array()));
    ASSERT_TRUE(module.save_as(module_path));

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(module_path, root, options);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.resource_count, 0);
    EXPECT_NE(result.message.find("Failed to open all module haks"), std::string::npos);
}

TEST(ClientProject, NativeProjectDoesNotFallBackToLegacyAreaResources)
{
    const std::filesystem::path root = "tmp/client_project_no_area_fallback";
    std::filesystem::remove_all(root);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;

    const auto area_dir = root / "shared" / "areas";
    std::filesystem::remove(area_dir / "start.caf.json");
    nw::Erf legacy{docker_demo_module_path()};
    ASSERT_TRUE(legacy.valid());
    ASSERT_EQ(legacy.extract(std::regex{"start\\.(are|git|gic)"}, area_dir), 3);

    auto* module = nw::kernel::load_module(root, false);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(nw::kernel::resman().module_format(), nw::ModuleResourceFormat::native_json);
    EXPECT_EQ(nw::kernel::objects().make_area(nw::Resref{"start"}), nullptr);

    {
        std::ofstream malformed{area_dir / "start.caf.json", std::ios::binary};
        ASSERT_TRUE(malformed);
        malformed << "{}\n";
    }
    module = nw::kernel::load_module(root, false);
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(nw::kernel::objects().make_area(nw::Resref{"start"}), nullptr);
    EXPECT_EQ(nw::kernel::load_module(root, true), nullptr);
}

TEST(ClientProject, KernelLoadUsesProjectHakRoot)
{
    const std::filesystem::path root = "tmp/client_project_kernel_local_hak";
    std::filesystem::remove_all(root);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;
    update_imported_module_metadata(root, {"project_dep"});

    std::filesystem::create_directories(root / "hak" / "project_dep");
    {
        std::ofstream out{root / "hak" / "project_dep" / "build.txt", std::ios::binary};
        ASSERT_TRUE(out);
        out << "project hak\n";
    }

    const auto load_options = nw::kernel::module_load_options_for_project(root);
    auto* module = nw::kernel::load_module(root, false, load_options);
    ASSERT_TRUE(module);

    const auto data = nw::kernel::resman().demand(
        nw::Resource{nw::StringView{"build"}, nw::ResourceType::txt});
    EXPECT_EQ(data.bytes.string_view(), "project hak\n");
}

TEST(ClientProject, KernelLoadProjectHakAcceptsExtensionAndCaseVariants)
{
    const std::filesystem::path root = "tmp/client_project_kernel_hak_extension_case";
    std::filesystem::remove_all(root);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;
    update_imported_module_metadata(root, {"project_dep.hak"});

    std::filesystem::create_directories(root / "hak");
    std::filesystem::copy_file("test_data/user/hak/hak_with_description.hak",
        root / "hak" / "PROJECT_DEP.HAK",
        std::filesystem::copy_options::overwrite_existing);

    const auto load_options = nw::kernel::module_load_options_for_project(root);
    auto* module = nw::kernel::load_module(root, false, load_options);
    ASSERT_TRUE(module);

    const auto data = nw::kernel::resman().demand(
        nw::Resource{nw::StringView{"build"}, nw::ResourceType::txt});
    EXPECT_GT(data.bytes.size(), 0);
}

TEST(ClientProject, KernelLoadProjectHakWinsOverFallbackRoot)
{
    const std::filesystem::path root = "tmp/client_project_kernel_hak_priority";
    const std::filesystem::path fallback = "tmp/client_project_kernel_hak_priority_fallback";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(fallback);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;
    update_imported_module_metadata(root, {"priority_dep"});

    std::filesystem::create_directories(root / "hak" / "priority_dep");
    std::filesystem::create_directories(fallback / "hak" / "priority_dep");
    {
        std::ofstream out{root / "hak" / "priority_dep" / "build.txt", std::ios::binary};
        ASSERT_TRUE(out);
        out << "project\n";
    }
    {
        std::ofstream out{fallback / "hak" / "priority_dep" / "build.txt", std::ios::binary};
        ASSERT_TRUE(out);
        out << "fallback\n";
    }

    nw::kernel::ModuleLoadOptions load_options;
    load_options.hak_roots = {root / "hak", fallback / "hak"};
    auto* module = nw::kernel::load_module(root, false, load_options);
    ASSERT_TRUE(module);

    const auto data = nw::kernel::resman().demand(
        nw::Resource{nw::StringView{"build"}, nw::ResourceType::txt});
    EXPECT_EQ(data.bytes.string_view(), "project\n");
}

TEST(ClientProject, KernelLoadUsesProjectTlkRoot)
{
    const std::filesystem::path root = "tmp/client_project_kernel_local_tlk";
    std::filesystem::remove_all(root);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;
    update_imported_module_metadata(root, {}, "project_dialog");

    std::filesystem::create_directories(root / "tlk");
    std::filesystem::copy_file(dialog_tlk_path(),
        root / "tlk" / "project_dialog.tlk",
        std::filesystem::copy_options::overwrite_existing);

    const auto load_options = nw::kernel::module_load_options_for_project(root);
    auto* module = nw::kernel::load_module(root, false, load_options);
    ASSERT_TRUE(module);

    EXPECT_EQ(nw::kernel::strings().get(0x01001000), "Stay here and don't move until I return.");
}

TEST(ClientProject, KernelLoadProjectTlkAcceptsExtensionAndCaseVariants)
{
    const std::filesystem::path root = "tmp/client_project_kernel_tlk_extension_case";
    std::filesystem::remove_all(root);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;
    update_imported_module_metadata(root, {}, "project_dialog.tlk");

    std::filesystem::create_directories(root / "tlk");
    std::filesystem::copy_file(dialog_tlk_path(),
        root / "tlk" / "Project_Dialog.TLK",
        std::filesystem::copy_options::overwrite_existing);

    const auto load_options = nw::kernel::module_load_options_for_project(root);
    auto* module = nw::kernel::load_module(root, false, load_options);
    ASSERT_TRUE(module);

    EXPECT_EQ(nw::kernel::strings().get(0x01001000), "Stay here and don't move until I return.");
}

TEST(ClientProject, KernelLoadCustomTlkUsesFallbackRoot)
{
    const std::filesystem::path root = "tmp/client_project_kernel_tlk_fallback";
    const std::filesystem::path fallback = "tmp/client_project_kernel_tlk_fallback_root";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(fallback);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;
    update_imported_module_metadata(root, {}, "fallback_dialog");

    std::filesystem::create_directories(root / "tlk");
    std::filesystem::create_directories(fallback / "tlk");
    std::filesystem::copy_file(dialog_tlk_path(),
        fallback / "tlk" / "fallback_dialog.tlk",
        std::filesystem::copy_options::overwrite_existing);

    nw::kernel::ModuleLoadOptions load_options;
    load_options.tlk_roots = {root / "tlk", fallback / "tlk"};
    auto* module = nw::kernel::load_module(root, false, load_options);
    ASSERT_TRUE(module);

    EXPECT_EQ(nw::kernel::strings().get(0x01001000), "Stay here and don't move until I return.");
}

TEST(ClientProject, LegacyImportKeepsBinaryGffResources)
{
    const std::filesystem::path root = "tmp/client_project_import_legacy";
    std::filesystem::remove_all(root);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::legacy;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.initialized);
    EXPECT_TRUE(is_project_directory(root));
    EXPECT_TRUE(std::filesystem::exists(root / "shared" / "module.ifo"));
    EXPECT_TRUE(std::filesystem::exists(root / "shared" / "areas" / "start.are"));
    EXPECT_TRUE(std::filesystem::exists(root / "shared" / "areas" / "start.git"));
    EXPECT_TRUE(std::filesystem::exists(root / "shared" / "areas" / "start.gic"));
    EXPECT_FALSE(std::filesystem::exists(root / "shared" / "module.ifo.json"));
    EXPECT_FALSE(std::filesystem::exists(root / "shared" / "areas" / "start.are.gffjson"));

    const auto summary = load_project_module_summary(root);
    EXPECT_TRUE(summary.ok) << summary.message;
    EXPECT_TRUE(summary.haks.empty());
}

TEST(ClientProject, ReadsModuleHakListFromJsonMetadata)
{
    const std::filesystem::path root = "tmp/client_project_haks";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "shared");

    {
        std::ofstream manifest{root / "rollnw.json", std::ios::binary};
        ASSERT_TRUE(manifest);
        manifest << R"({
  "format": "rollnw.module",
  "version": 1,
  "name": "Hak Demo",
  "module": "shared/module.ifo.json"
})" << '\n';
    }

    {
        std::ofstream module{root / "shared" / "module.ifo.json", std::ios::binary};
        ASSERT_TRUE(module);
        module << R"({
  "$type": "IFO",
  "$version": 1,
  "haks": ["cep2_top_v25", "project_overrides"]
})" << '\n';
    }

    const auto summary = load_project_module_summary(root);
    ASSERT_TRUE(summary.ok) << summary.message;
    ASSERT_EQ(summary.haks.size(), 2);
    EXPECT_EQ(summary.haks[0], "cep2_top_v25");
    EXPECT_EQ(summary.haks[1], "project_overrides");
}

TEST(ClientProject, BuildsResourceAwareProjectTree)
{
    const std::filesystem::path root = "tmp/client_project_tree";
    std::filesystem::remove_all(root);

    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto result = import_module_project(docker_demo_module_path(), root, options);
    ASSERT_TRUE(result.ok) << result.message;

    std::filesystem::create_directories(root / "shared" / "blueprints" / "creatures");
    std::filesystem::copy_file(development_resource_path("pl_agent_001.utc.json"),
        root / "shared" / "blueprints" / "creatures" / "pl_agent_001.utc.json",
        std::filesystem::copy_options::overwrite_existing);
    const auto write_named_creature = [&](std::string_view filename, std::string_view name) {
        std::ifstream input{development_resource_path("pl_agent_001.utc.json")};
        nlohmann::json creature;
        input >> creature;
        creature["nwn1.propsets.CreatureDescriptor"]["name_first"]["strings"][0]["string"]
            = std::string{name};
        std::ofstream output{
            root / "shared" / "blueprints" / "creatures" / std::string{filename}, std::ios::binary};
        output << creature.dump(2) << '\n';
    };
    write_named_creature("a_late.utc.json", "Zed");
    write_named_creature("z_early.utc.json", "Aaron");

    std::filesystem::create_directories(root / "shared" / "scripts");
    {
        std::ofstream source{root / "shared" / "scripts" / "source.nss", std::ios::binary};
        source << "void main() {}\n";
    }
    {
        std::ofstream smalls{root / "shared" / "scripts" / "tool.smalls", std::ios::binary};
        smalls << "(module tool)\n";
    }
    {
        std::ofstream compiled{root / "shared" / "scripts" / "compiled.ncs", std::ios::binary};
        compiled << "compiled";
    }

    const auto tree = load_project_tree(root);
    ASSERT_TRUE(tree.ok) << tree.message;
    EXPECT_GT(tree.node_count, 0);
    EXPECT_EQ(tree.root.label, "DockerDemo");

    const auto find_node = [](const ProjectTreeNode& root_node, std::string_view relative_path) -> const ProjectTreeNode* {
        const auto visit = [&](auto&& self, const ProjectTreeNode& node) -> const ProjectTreeNode* {
            if (node.relative_path.generic_string() == relative_path) {
                return &node;
            }
            for (const auto& child : node.children) {
                if (const auto* found = self(self, child)) {
                    return found;
                }
            }
            return nullptr;
        };
        return visit(visit, root_node);
    };

    const auto* module = find_node(tree.root, "shared/module.ifo.json");
    ASSERT_TRUE(module);
    EXPECT_EQ(module->kind, ProjectTreeNodeKind::resource);
    EXPECT_EQ(module->resource_type, "ifo");

    const auto* area = find_node(tree.root, "shared/areas/start.caf.json");
    ASSERT_TRUE(area);
    EXPECT_EQ(area->kind, ProjectTreeNodeKind::area);
    EXPECT_EQ(area->resource_type, "area");
    EXPECT_EQ(area->label, "Start");
    EXPECT_EQ(area->detail, "start.caf.json");
    EXPECT_TRUE(project_resource_is_area("shared/areas/start.caf.json"));
    EXPECT_TRUE(project_resource_is_area("shared/areas/start.git"));
    EXPECT_TRUE(project_resource_is_area("shared/areas/start.gic"));
    EXPECT_EQ(project_resource_display_name(root, "shared/areas/start.caf.json"), "Start");
    EXPECT_FALSE(find_node(tree.root, "shared/areas/start.git"));
    EXPECT_FALSE(find_node(tree.root, "shared/areas/start.gic"));

    const auto* creature = find_node(tree.root, "shared/blueprints/creatures/pl_agent_001.utc.json");
    ASSERT_TRUE(creature);
    EXPECT_EQ(creature->kind, ProjectTreeNodeKind::resource);
    EXPECT_EQ(creature->resource_type, "utc");
    EXPECT_EQ(creature->label, "Agent");
    EXPECT_EQ(creature->detail, "pl_agent_001.utc.json");
    EXPECT_FALSE(project_resource_is_area("shared/blueprints/creatures/pl_agent_001.utc.json"));
    EXPECT_TRUE(project_resource_is_dialog("shared/conversations/test.dlg.json"));
    EXPECT_TRUE(project_resource_is_dialog("shared/conversations/test.dlg"));
    EXPECT_FALSE(project_resource_is_dialog("shared/blueprints/creatures/pl_agent_001.utc.json"));
    EXPECT_TRUE(project_resource_is_preview_blueprint("shared/blueprints/creatures/pl_agent_001.utc.json"));
    EXPECT_TRUE(project_resource_is_preview_blueprint("shared/blueprints/items/sword.uti.json"));
    EXPECT_TRUE(project_resource_is_preview_blueprint("shared/blueprints/doors/door.utd.json"));
    EXPECT_TRUE(project_resource_is_preview_blueprint("shared/blueprints/placeables/chest.utp.json"));
    EXPECT_FALSE(project_resource_is_preview_blueprint("shared/areas/start.caf.json"));
    EXPECT_EQ(project_resource_display_name(root, "shared/blueprints/creatures/pl_agent_001.utc.json"), "Agent");
    EXPECT_TRUE(std::filesystem::exists(root / ".rollnw" / "cache" / "project_tree_labels.json"));

    const auto* creature_folder = find_node(tree.root, "shared/blueprints/creatures");
    ASSERT_TRUE(creature_folder);
    const auto child_index = [&](std::string_view relative_path) -> size_t {
        for (size_t i = 0; i < creature_folder->children.size(); ++i) {
            if (creature_folder->children[i].relative_path.generic_string() == relative_path) {
                return i;
            }
        }
        return creature_folder->children.size();
    };
    const size_t aaron_index = child_index("shared/blueprints/creatures/z_early.utc.json");
    const size_t agent_index = child_index("shared/blueprints/creatures/pl_agent_001.utc.json");
    const size_t zed_index = child_index("shared/blueprints/creatures/a_late.utc.json");
    ASSERT_LT(aaron_index, creature_folder->children.size());
    ASSERT_LT(agent_index, creature_folder->children.size());
    ASSERT_LT(zed_index, creature_folder->children.size());
    EXPECT_LT(aaron_index, agent_index);
    EXPECT_LT(agent_index, zed_index);

    EXPECT_TRUE(find_node(tree.root, "shared/scripts/source.nss"));
    EXPECT_TRUE(find_node(tree.root, "shared/scripts/tool.smalls"));
    EXPECT_FALSE(find_node(tree.root, "shared/scripts/compiled.ncs"));
    EXPECT_EQ(project_resource_display_name(root, "shared/module.ifo.json"), "module");
    EXPECT_EQ(project_resource_display_name(root, "shared/scripts/source.nss"), "source");
    EXPECT_EQ(project_resource_display_name(root, "shared/scripts/tool.smalls"), "tool");

    const auto filtered = load_project_tree(root, "start.caf");
    ASSERT_TRUE(filtered.ok) << filtered.message;
    ASSERT_EQ(filtered.root.children.size(), 1);
    EXPECT_EQ(filtered.root.children[0].relative_path.generic_string(), "shared");
    ASSERT_EQ(filtered.root.children[0].children.size(), 1);
    EXPECT_EQ(filtered.root.children[0].children[0].relative_path.generic_string(), "shared/areas");
    ASSERT_EQ(filtered.root.children[0].children[0].children.size(), 1);
    EXPECT_EQ(filtered.root.children[0].children[0].children[0].relative_path.generic_string(), "shared/areas/start.caf.json");
    EXPECT_EQ(filtered.root.children[0].children[0].children[0].kind, ProjectTreeNodeKind::area);
}

TEST(ClientResourceDocument, LoadsPropsetCreatureDocumentIdentity)
{
    const std::filesystem::path root = "tmp/client_resource_document_creature";
    std::filesystem::remove_all(root);

    const auto init = initialize_project(root, "Example");
    ASSERT_TRUE(init.ok) << init.message;
    std::filesystem::create_directories(root / "shared" / "blueprints" / "creatures");
    std::filesystem::copy_file(development_resource_path("pl_agent_001.utc.json"),
        root / "shared" / "blueprints" / "creatures" / "pl_agent_001.utc.json",
        std::filesystem::copy_options::overwrite_existing);

    const auto document = load_project_resource_document(root, "shared/blueprints/creatures/pl_agent_001.utc.json");
    ASSERT_TRUE(document.ok) << document.message;
    EXPECT_EQ(document.kind, ResourceDocumentKind::preview);
    EXPECT_EQ(document.title, "Agent");
    EXPECT_EQ(document.detail, "shared/blueprints/creatures/pl_agent_001.utc.json");
    EXPECT_EQ(document.resource_type, "utc");
    EXPECT_EQ(document.format, "JSON");
    EXPECT_TRUE(document.previewable);
    EXPECT_FALSE(document.area);
}

TEST(ClientResourceDocument, RejectsPathsOutsideProject)
{
    const std::filesystem::path root = "tmp/client_resource_document_escape/project";
    const std::filesystem::path outside = "tmp/client_resource_document_escape/outside.txt";
    std::filesystem::remove_all("tmp/client_resource_document_escape");

    const auto init = initialize_project(root, "Example");
    ASSERT_TRUE(init.ok) << init.message;
    {
        std::ofstream output{outside, std::ios::binary};
        ASSERT_TRUE(output);
        output << "outside\n";
    }

    const auto document = load_project_resource_document(root, "../outside.txt");
    EXPECT_FALSE(document.ok);
    EXPECT_NE(document.message.find("outside the current project"), std::string::npos);
}

TEST(ClientResourceDocument, AtomicallyReplacesExistingJson)
{
    const std::filesystem::path root = "tmp/client_resource_document_save";
    const auto target = root / "creature.utc.json";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output{target, std::ios::binary};
        ASSERT_TRUE(output);
        output << "{\"value\": 1}\n";
    }

    std::string error;
    ASSERT_TRUE(save_json_resource_document_atomic(target, nlohmann::json{{"value", 2}}, error)) << error;
    EXPECT_FALSE(std::filesystem::exists(target.string() + ".rollnw-client-save.tmp"));

    nlohmann::json saved;
    std::ifstream input{target};
    ASSERT_TRUE(input);
    input >> saved;
    EXPECT_EQ(saved, nlohmann::json({{"value", 2}}));
}

TEST(ClientWorkspace, UndoRedoAreScopedToActiveTab)
{
    CommandBus bus;
    WorkspaceState workspace;
    int value = 0;

    auto edit = spec("test.edit");
    edit.scope = CommandScope::workspace;
    EXPECT_TRUE(bus.register_command(std::move(edit),
        [&value](const CommandInvocation&, CommandContext&) {
            ++value;
            CommandResult result = test_result(CommandStatus::success, "edited");
            auto action = std::make_shared<CommandUndoAction>();
            action->label = "edit";
            action->undo = [&value](CommandContext&) {
                --value;
                return test_result(CommandStatus::success, "undone");
            };
            action->redo = [&value](CommandContext&) {
                ++value;
                return test_result(CommandStatus::success, "redone");
            };
            result.undo_action = std::move(action);
            return result;
        }));

    workspace.open_tab("a");
    EXPECT_TRUE(bus.execute("test.edit", {}, test_context(&workspace)).ok());
    EXPECT_EQ(value, 1);
    EXPECT_EQ(workspace.undo_count(), 1);

    workspace.open_tab("b");
    EXPECT_EQ(workspace.undo(test_context(&workspace)).status, CommandStatus::noop);
    EXPECT_EQ(value, 1);

    workspace.set_active_tab("a");
    EXPECT_TRUE(workspace.undo(test_context(&workspace)).ok());
    EXPECT_EQ(value, 0);
    EXPECT_EQ(workspace.redo_count(), 1);
    EXPECT_TRUE(workspace.redo(test_context(&workspace)).ok());
    EXPECT_EQ(value, 1);
}

TEST(ClientWorkspace, HomeTabIsListedAndNotClosable)
{
    WorkspaceState workspace;
    workspace.ensure_default_tabs();
    ASSERT_EQ(workspace.tabs().size(), 2);
    EXPECT_EQ(workspace.tabs()[0].id, "home");
    EXPECT_EQ(workspace.tabs()[0].kind, WorkspaceTabKind::home);
    EXPECT_FALSE(workspace.tabs()[0].closable);
    EXPECT_FALSE(workspace.tabs()[0].movable);
    EXPECT_EQ(workspace.tabs()[1].id, "area");
    EXPECT_EQ(workspace.tabs()[1].kind, WorkspaceTabKind::area);
    EXPECT_FALSE(workspace.tabs()[1].closable);
    EXPECT_FALSE(workspace.tabs()[1].movable);
    EXPECT_EQ(workspace.active_tab_id(), "home");

    EXPECT_FALSE(workspace.close_tab("home"));
    EXPECT_FALSE(workspace.close_tab("area"));
    ASSERT_EQ(workspace.tabs().size(), 2);
    EXPECT_EQ(workspace.active_tab_id(), "home");

    workspace.ensure_default_tabs("Project");
    ASSERT_EQ(workspace.tabs().size(), 2);
    EXPECT_EQ(workspace.tabs().front().title, "Project");
    EXPECT_EQ(workspace.tabs().front().kind, WorkspaceTabKind::home);
    EXPECT_FALSE(workspace.tabs().front().closable);
    EXPECT_FALSE(workspace.tabs().front().movable);

    workspace.open_tab("resource:foo", "foo", WorkspaceTabKind::resource);
    ASSERT_EQ(workspace.tabs().size(), 3);
    EXPECT_EQ(workspace.active_tab_id(), "resource:foo");
    EXPECT_TRUE(workspace.set_active_tab("home"));
    EXPECT_EQ(workspace.active_tab()->kind, WorkspaceTabKind::home);
}

TEST(ClientWorkspace, DirtyTabsRequireCloseConfirmation)
{
    WorkspaceState workspace;
    workspace.open_tab("resource:dirty", "Dirty", WorkspaceTabKind::resource);
    ASSERT_TRUE(workspace.set_tab_dirty("resource:dirty", true));

    const auto dirty_close = workspace.request_close_tab("resource:dirty");
    EXPECT_TRUE(dirty_close.needs_save_prompt());
    EXPECT_EQ(dirty_close.title, "Dirty");
    ASSERT_EQ(workspace.tabs().size(), 1);
    EXPECT_EQ(workspace.active_tab_id(), "resource:dirty");

    const auto forced_close = workspace.request_close_tab("resource:dirty", true);
    EXPECT_TRUE(forced_close.closed());
    EXPECT_TRUE(workspace.tabs().empty());
}

TEST(ClientCommands, CloseTabRejectsDirtyTabsUntilForced)
{
    WorkspaceState workspace;
    CommandBus bus;
    ASSERT_TRUE(bus.register_command(spec("workspace.close_tab"),
        [&workspace](const CommandInvocation& invocation, CommandContext&) {
            auto make_result = [](CommandStatus status, std::string message, CommandOutputChannel channel) {
                CommandResult result;
                result.status = status;
                result.message = std::move(message);
                result.output_channel = channel;
                return result;
            };

            std::string id;
            bool force = false;
            for (size_t i = 0; i < invocation.args.size(); ++i) {
                const std::string value = command_arg_string(invocation.args, i);
                if (value == "--force" || value == "force") {
                    force = true;
                } else if (id.empty()) {
                    id = value;
                }
            }
            if (id.empty()) {
                id = workspace.active_tab_id();
            }

            const auto close = workspace.request_close_tab(id, force);
            if (close.needs_save_prompt()) {
                auto result = make_result(CommandStatus::rejected, "Save changes before closing", CommandOutputChannel::warn);
                CommandPrompt prompt;
                prompt.id = "workspace.close_tab.save";
                prompt.title = "Save changes?";
                prompt.message = "Save changes before closing Dirty?";
                prompt.actions.push_back(
                    CommandPromptAction{"save", "Save", "workspace.save_and_close_tab", {close.tab_id}});
                prompt.actions.push_back(CommandPromptAction{"discard", "Discard", "workspace.close_tab", {close.tab_id, "--force"}});
                prompt.actions.push_back(CommandPromptAction{"cancel", "Cancel", {}, {}});
                result.prompt = std::move(prompt);
                return result;
            }
            if (!close.closed()) {
                return make_result(CommandStatus::noop, "No workspace tab closed", CommandOutputChannel::info);
            }
            return make_result(CommandStatus::success, "Closed tab", CommandOutputChannel::info);
        }));

    bool save_succeeds = true;
    int save_count = 0;
    ASSERT_TRUE(bus.register_command(spec("workspace.save_and_close_tab"),
        [&workspace, &save_succeeds, &save_count](const CommandInvocation& invocation, CommandContext&) {
            ++save_count;
            if (!save_succeeds) {
                return test_result(CommandStatus::failed, "save failed");
            }
            const std::string id = command_arg_string(invocation.args, 0);
            workspace.set_tab_dirty(id, false);
            if (!workspace.request_close_tab(id).closed()) {
                return test_result(CommandStatus::failed, "close failed");
            }
            return test_result(CommandStatus::success, "saved and closed");
        }));

    workspace.open_tab("resource:dirty", "Dirty", WorkspaceTabKind::resource);
    ASSERT_TRUE(workspace.set_tab_dirty("resource:dirty", true));

    auto result = bus.execute("workspace.close_tab",
        {CommandArg::positional_string("resource:dirty")},
        test_context(&workspace));
    EXPECT_EQ(result.status, CommandStatus::rejected);
    EXPECT_EQ(result.output_channel, CommandOutputChannel::warn);
    ASSERT_TRUE(result.prompt);
    EXPECT_EQ(result.prompt->id, "workspace.close_tab.save");
    ASSERT_EQ(result.prompt->actions.size(), 3);
    EXPECT_EQ(result.prompt->actions[0].command_id, "workspace.save_and_close_tab");
    EXPECT_EQ(result.prompt->actions[1].id, "discard");
    EXPECT_EQ(result.prompt->actions[1].command_id, "workspace.close_tab");
    ASSERT_EQ(result.prompt->actions[1].args.size(), 2);
    EXPECT_EQ(result.prompt->actions[1].args[0], "resource:dirty");
    EXPECT_EQ(result.prompt->actions[1].args[1], "--force");
    ASSERT_EQ(workspace.tabs().size(), 1);
    EXPECT_EQ(workspace.active_tab_id(), "resource:dirty");

    result = bus.execute("workspace.save_and_close_tab",
        {CommandArg::positional_string("resource:dirty")},
        test_context(&workspace));
    EXPECT_EQ(result.status, CommandStatus::success);
    EXPECT_EQ(save_count, 1);
    EXPECT_TRUE(workspace.tabs().empty());

    workspace.open_tab("resource:dirty", "Dirty", WorkspaceTabKind::resource);
    ASSERT_TRUE(workspace.set_tab_dirty("resource:dirty", true));
    save_succeeds = false;
    result = bus.execute("workspace.save_and_close_tab",
        {CommandArg::positional_string("resource:dirty")},
        test_context(&workspace));
    EXPECT_EQ(result.status, CommandStatus::failed);
    EXPECT_EQ(save_count, 2);
    ASSERT_EQ(workspace.tabs().size(), 1);
    EXPECT_TRUE(workspace.tabs().front().dirty);
}

TEST(ClientWorkspace, ReusesAreaTabAndClearsUndoWhenAreaChanges)
{
    WorkspaceState workspace;
    workspace.ensure_default_tabs();

    auto make_action = [] {
        CommandUndoAction action;
        action.label = "area edit";
        action.undo = [](CommandContext&) { return test_result(); };
        action.redo = [](CommandContext&) { return test_result(); };
        return action;
    };

    auto& first = workspace.open_or_replace_tab(
        "area", "Start", WorkspaceTabKind::area, "shared/areas/start.are", false, false);
    EXPECT_EQ(first.kind, WorkspaceTabKind::area);
    EXPECT_EQ(first.title, "Start");
    EXPECT_EQ(first.detail, "shared/areas/start.are");
    EXPECT_FALSE(first.closable);
    EXPECT_FALSE(first.movable);
    EXPECT_EQ(workspace.active_tab_id(), "area");
    workspace.push_undo(make_action());
    EXPECT_EQ(workspace.undo_count(), 1);

    workspace.open_or_replace_tab("area", "Start", WorkspaceTabKind::area, "shared/areas/start.are", false, false);
    ASSERT_EQ(workspace.tabs().size(), 2);
    EXPECT_EQ(workspace.undo_count(), 1);

    auto& second = workspace.open_or_replace_tab(
        "area", "Docks", WorkspaceTabKind::area, "shared/areas/docks.are", false, false);
    ASSERT_EQ(workspace.tabs().size(), 2);
    EXPECT_EQ(second.title, "Docks");
    EXPECT_EQ(second.detail, "shared/areas/docks.are");
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_EQ(workspace.redo_count(), 0);
}

TEST(ClientWorkspace, MovesTabsWithoutMovingHome)
{
    WorkspaceState workspace;
    workspace.ensure_default_tabs();
    workspace.open_tab("a");
    workspace.open_tab("b");
    workspace.open_tab("c");

    EXPECT_FALSE(workspace.move_tab("home", 3));
    EXPECT_FALSE(workspace.move_tab("area", 4));
    ASSERT_EQ(workspace.tabs().size(), 5);
    EXPECT_EQ(workspace.tabs()[0].id, "home");
    EXPECT_EQ(workspace.tabs()[1].id, "area");

    EXPECT_TRUE(workspace.move_tab("c", 1));
    ASSERT_EQ(workspace.tabs().size(), 5);
    EXPECT_EQ(workspace.tabs()[0].id, "home");
    EXPECT_EQ(workspace.tabs()[1].id, "area");
    EXPECT_EQ(workspace.tabs()[2].id, "c");
    EXPECT_EQ(workspace.tabs()[3].id, "a");
    EXPECT_EQ(workspace.tabs()[4].id, "b");
    EXPECT_EQ(workspace.active_tab_id(), "c");

    EXPECT_TRUE(workspace.move_tab("a", 0));
    ASSERT_EQ(workspace.tabs().size(), 5);
    EXPECT_EQ(workspace.tabs()[0].id, "home");
    EXPECT_EQ(workspace.tabs()[1].id, "area");
    EXPECT_EQ(workspace.tabs()[2].id, "a");
    EXPECT_EQ(workspace.tabs()[3].id, "c");
    EXPECT_EQ(workspace.tabs()[4].id, "b");

    EXPECT_TRUE(workspace.move_tab("a", 3));
    ASSERT_EQ(workspace.tabs().size(), 5);
    EXPECT_EQ(workspace.tabs()[0].id, "home");
    EXPECT_EQ(workspace.tabs()[1].id, "area");
    EXPECT_EQ(workspace.tabs()[2].id, "c");
    EXPECT_EQ(workspace.tabs()[3].id, "a");
    EXPECT_EQ(workspace.tabs()[4].id, "b");
}

TEST(ClientWorkspace, ManagesSubTabsInsideOwningTab)
{
    WorkspaceState workspace;
    workspace.open_tab("resource:demo", "demo", WorkspaceTabKind::resource);

    auto* design = workspace.open_subtab("resource:demo", "design", "Design", false);
    ASSERT_TRUE(design);
    EXPECT_FALSE(design->closable);
    EXPECT_EQ(workspace.active_subtab()->id, "design");

    auto* properties = workspace.open_subtab("resource:demo", "properties", "Properties");
    ASSERT_TRUE(properties);
    EXPECT_EQ(workspace.active_subtab()->id, "properties");
    ASSERT_EQ(workspace.active_tab()->subtabs.size(), 2);

    EXPECT_TRUE(workspace.set_active_subtab("resource:demo", "design"));
    EXPECT_EQ(workspace.active_subtab()->title, "Design");
    EXPECT_FALSE(workspace.close_subtab("resource:demo", "design"));

    EXPECT_TRUE(workspace.close_subtab("resource:demo", "properties"));
    ASSERT_EQ(workspace.active_tab()->subtabs.size(), 1);
    EXPECT_EQ(workspace.active_subtab()->id, "design");
}

TEST(ClientWorkspace, RedoClearsAfterNewEditAndGlobalCommandsDoNotPushUndo)
{
    CommandBus bus;
    WorkspaceState workspace;
    int value = 0;

    auto make_edit_handler = [&value](const CommandInvocation&, CommandContext&) {
        ++value;
        CommandResult result = test_result();
        auto action = std::make_shared<CommandUndoAction>();
        action->label = "edit";
        action->undo = [&value](CommandContext&) {
            --value;
            return test_result();
        };
        action->redo = [&value](CommandContext&) {
            ++value;
            return test_result();
        };
        result.undo_action = std::move(action);
        return result;
    };

    auto workspace_edit = spec("test.workspace_edit");
    workspace_edit.scope = CommandScope::workspace;
    EXPECT_TRUE(bus.register_command(std::move(workspace_edit), make_edit_handler));

    auto global_edit = spec("test.global_edit");
    global_edit.scope = CommandScope::global;
    EXPECT_TRUE(bus.register_command(std::move(global_edit), make_edit_handler));

    workspace.open_tab("main");
    EXPECT_TRUE(bus.execute("test.workspace_edit", {}, test_context(&workspace)).ok());
    EXPECT_TRUE(workspace.undo(test_context(&workspace)).ok());
    EXPECT_EQ(workspace.redo_count(), 1);

    EXPECT_TRUE(bus.execute("test.workspace_edit", {}, test_context(&workspace)).ok());
    EXPECT_EQ(workspace.redo_count(), 0);

    const size_t undo_count = workspace.undo_count();
    EXPECT_TRUE(bus.execute("test.global_edit", {}, test_context(&workspace)).ok());
    EXPECT_EQ(workspace.undo_count(), undo_count);
}

TEST(ClientScriptCommands, RegistersExecutesAndUsesUndoToken)
{
    auto& rt = nw::kernel::runtime();
    auto* script = rt.load_module_from_source("test.client_command_script", R"(
        type CommandResult {
            status: int;
            message: string;
            channel: string;
            undo_token: string;
        };

        fn execute(command_id: string, args: string): CommandResult {
            return CommandResult {
                status = 0,
                message = "execute:" + command_id + ":" + args,
                channel = "script",
                undo_token = "token:" + args
            };
        }

        fn undo(token: string): CommandResult {
            return CommandResult {
                status = 0,
                message = "undo:" + token,
                channel = "script",
                undo_token = ""
            };
        }

        fn redo(token: string): CommandResult {
            return CommandResult {
                status = 0,
                message = "redo:" + token,
                channel = "script",
                undo_token = ""
            };
        }
    )");
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);

    CommandBus bus;
    WorkspaceState workspace;
    workspace.open_tab("script");
    script_command_host().bind(&bus, &workspace);

    auto command = spec("script.bump", {"bump"});
    command.scope = CommandScope::workspace;
    ASSERT_TRUE(script_command_host().register_command("test.client_command_script",
        std::move(command),
        "execute",
        "undo",
        "redo"));

    EXPECT_TRUE(bus.has_command("bump"));
    CommandArgs args;
    args.push_back(CommandArg::positional_string("one"));
    const auto result = bus.execute("bump", std::move(args), test_context(&workspace));
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.message, "execute:script.bump:one");
    EXPECT_EQ(result.output_channel, CommandOutputChannel::script);
    EXPECT_EQ(workspace.undo_count(), 1);

    const auto undo = workspace.undo(test_context(&workspace));
    EXPECT_TRUE(undo.ok());
    EXPECT_EQ(undo.message, "undo:token:one");
    EXPECT_EQ(workspace.redo_count(), 1);

    const auto redo = workspace.redo(test_context(&workspace));
    EXPECT_TRUE(redo.ok());
    EXPECT_EQ(redo.message, "redo:token:one");
}

} // namespace nw::toolset
