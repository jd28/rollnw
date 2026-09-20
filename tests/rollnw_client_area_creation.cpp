#include "area_creation.hpp"
#include "object_document.hpp"

#include <nw/formats/Tileset.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/TilesetRegistry.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>

#ifdef ROLLNW_TEST_CLIENT_EXECUTABLE
#include "project.hpp"
#include "rml_smalls_bridge.hpp"
#include "toolset_backend.hpp"
#include "workspace.hpp"
#endif

namespace nw::toolset {
namespace {

#ifdef ROLLNW_TEST_CLIENT_EXECUTABLE
class KernelServiceScope {
public:
    KernelServiceScope() { kernel::services().start(); }
    ~KernelServiceScope()
    {
        kernel::services().shutdown();
        kernel::services().start();
    }
};
#endif

TEST(ClientAreaCreationTopology, CanonicalGroundUsesSetOrderAndTopology)
{
    Tileset tileset;
    tileset.default_terrain = 3;
    tileset.tiles.resize(4);
    tileset.tile_topologies.resize(4);
    tileset.grouped_tiles.resize(4, 0);
    for (auto& topology : tileset.tile_topologies) {
        topology.valid = true;
        topology.terrain.fill(3);
        topology.crosser.fill(-1);
    }
    tileset.grouped_tiles[0] = 1;
    tileset.tile_topologies[1].terrain[2] = 4;
    tileset.tile_topologies[2].crosser[0] = 0;
    tileset.tile_topologies[3].height.fill(2);

    AreaTile ground{.id = 99, .height = 7, .orientation = 2};
    ASSERT_TRUE(canonical_area_ground_tile(tileset, ground));
    EXPECT_EQ(ground.id, 3);
    EXPECT_EQ(ground.height, 0);
    EXPECT_EQ(ground.orientation, 0);

    tileset.tile_topologies[3].height[1] = 3;
    EXPECT_FALSE(canonical_area_ground_tile(tileset, ground));
}

class ClientAreaCreation : public testing::Test {
protected:
    std::filesystem::path project{"tmp/client_area_creation"};
    Resref tileset;

    void SetUp() override
    {
        project /= ::testing::UnitTest::GetInstance()
                       ->current_test_info()
                       ->name();
        ASSERT_NE(kernel::load_module(
                      "test_data/user/modules/DockerDemo.mod", false),
            nullptr);
        for (const auto& [resref, value] : kernel::tilesets().tileset_map_) {
            AreaTile ground;
            if (canonical_area_ground_tile(value, ground)) {
                tileset = Resref{resref};
                break;
            }
        }
        ASSERT_FALSE(tileset.empty());

        std::filesystem::remove_all(project);
        std::filesystem::create_directories(project / "shared/areas");
        std::ofstream{project / "shared/module.ifo.json"} << "{}";
        auto& resources = kernel::resman();
        resources.unfreeze();
        ASSERT_TRUE(resources.load_module(project / "shared"));
        resources.build_registry();
    }

    NewAreaRequest request(std::string resref = "created_area") const
    {
        return {
            .directory = "shared/areas",
            .resref = std::move(resref),
            .name = "  Created Area  ",
            .tileset = tileset,
            .width = 3,
            .height = 2,
        };
    }
};

TEST_F(ClientAreaCreation, PreparesPublishesAndReloadsCompleteCaf)
{
    const std::array requests{request()};
    auto prepared = prepare_new_areas(project, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    ASSERT_EQ(prepared.rows.size(), 1u);
    const auto& row = prepared.rows[0];
    EXPECT_EQ(row.request.resref, "created_area");
    EXPECT_EQ(row.request.name, "Created Area");
    EXPECT_EQ(row.relative_path.generic_string(),
        "shared/areas/created_area.caf.json");
    EXPECT_FALSE(std::filesystem::exists(row.target));

    const auto json = nlohmann::json::parse(row.bytes);
    EXPECT_EQ(json.at("$type"), "CAF");
    EXPECT_EQ(json.at("$version"), Area::json_archive_version);
    EXPECT_EQ(json.at("width"), 3);
    EXPECT_EQ(json.at("height"), 2);
    ASSERT_EQ(json.at("tiles").size(), 6u);
    const auto tile_id = json.at("tiles").front().at("id");
    for (const auto& tile : json.at("tiles")) {
        EXPECT_EQ(tile.at("id"), tile_id);
    }

    const auto results = publish_new_areas(prepared);
    ASSERT_EQ(results.size(), 1u);
    ASSERT_TRUE(results[0].saved) << results[0].error;
    ASSERT_TRUE(results[0].published) << results[0].error;
    EXPECT_TRUE(std::filesystem::is_regular_file(row.target));
    EXPECT_TRUE(kernel::resman().contains(row.resource));

    ObjectDocument owner;
    auto* reloaded = kernel::objects().make_area(row.resource.resref);
    ASSERT_NE(reloaded, nullptr);
    ASSERT_TRUE(owner.adopt(reloaded->handle()));
    EXPECT_EQ(reloaded->width, 3);
    EXPECT_EQ(reloaded->height, 2);
    EXPECT_EQ(reloaded->tiles.size(), 6u);
    EXPECT_EQ(reloaded->name.get(LanguageID::english), "Created Area");
    EXPECT_EQ(reloaded->tileset_resref, tileset);
}

TEST_F(ClientAreaCreation, InvalidBatchRejectsWithoutWriting)
{
    auto invalid = request("invalid_area");
    invalid.width = kMinimumNewAreaDimension - 1;
    const std::array invalid_requests{invalid};
    auto prepared = prepare_new_areas(project, invalid_requests);
    EXPECT_FALSE(prepared.ok());
    EXPECT_TRUE(prepared.rows.empty());
    EXPECT_FALSE(std::filesystem::exists(
        project / "shared/areas/invalid_area.caf.json"));

    const std::array duplicate_requests{
        request("duplicate_area"), request("DUPLICATE_AREA")};
    prepared = prepare_new_areas(project, duplicate_requests);
    EXPECT_FALSE(prepared.ok());
    EXPECT_TRUE(prepared.rows.empty());
    EXPECT_FALSE(std::filesystem::exists(
        project / "shared/areas/duplicate_area.caf.json"));

    auto outside = request("outside_area");
    outside.directory = project.parent_path();
    const std::array outside_requests{outside};
    prepared = prepare_new_areas(project, outside_requests);
    EXPECT_FALSE(prepared.ok());
    EXPECT_TRUE(prepared.rows.empty());
}

TEST_F(ClientAreaCreation, ExistingResourceRejectsBeforePublication)
{
    const std::array requests{request("existing_area")};
    auto prepared = prepare_new_areas(project, requests);
    ASSERT_TRUE(prepared.ok()) << prepared.error;
    ASSERT_TRUE(publish_new_areas(prepared)[0].published);

    auto duplicate = prepare_new_areas(project, requests);
    EXPECT_FALSE(duplicate.ok());
    EXPECT_TRUE(duplicate.rows.empty());
}

#ifdef ROLLNW_TEST_CLIENT_EXECUTABLE
TEST(ClientAreaCreationCommands, FormCreatesOpensAndIndexesArea)
{
    KernelServiceScope services;
    const std::filesystem::path project
        = "tmp/client_area_creation_command";
    std::filesystem::remove_all(project);
    ProjectImportOptions options;
    options.format = ProjectImportFormat::json;
    const auto imported = import_module_project(
        "test_data/user/modules/DockerDemo.mod", project, options);
    ASSERT_TRUE(imported.ok) << imported.message;

    RmlSmallsBridge bridge;
    WorkspaceState workspace;
    ToolsetBackend backend;
    backend.bind(&bridge, nullptr, &workspace);
    ASSERT_TRUE(backend.open_project(project.string()).ok());
    auto form = backend.execute_command("area.new", {}, {});
    ASSERT_TRUE(form.prompt) << form.message;
    ASSERT_EQ(form.prompt->fields.size(), 6u);
    EXPECT_EQ(form.prompt->fields[1].value, "shared/areas");
    EXPECT_EQ(form.prompt->fields[4].value, "4");
    EXPECT_EQ(form.prompt->fields[5].value, "4");
    ASSERT_FALSE(form.prompt->fields[3].value.empty());

    const std::vector<std::string_view> values{
        "command_area",
        "shared/areas",
        "Command Area",
        form.prompt->fields[3].value,
        "3",
        "2",
    };
    const auto created
        = backend.execute_command("area.create", values, {});
    ASSERT_TRUE(created.ok()) << created.message;
    ASSERT_NE(workspace.active_tab(), nullptr);
    EXPECT_EQ(workspace.active_tab_id(), "area");
    EXPECT_EQ(workspace.active_tab()->detail,
        "shared/areas/command_area.caf.json");
    EXPECT_TRUE(std::filesystem::is_regular_file(project
        / "shared/areas/command_area.caf.json"));
    const auto areas = backend.list_areas("Command Area");
    ASSERT_EQ(areas.size(), 1u);
    EXPECT_EQ(areas[0].resref, "command_area");

    auto* live = kernel::objects().make_area(Resref{"command_area"});
    ASSERT_NE(live, nullptr);
    const auto old_area = live->handle();
    ASSERT_TRUE(workspace.active_tab()->document.adopt(old_area));
    workspace.active_tab()->dirty = true;
    const std::vector<std::string_view> second_values{
        "second_area",
        "shared/areas",
        "Second Area",
        form.prompt->fields[3].value,
        "2",
        "2",
    };
    const auto pending
        = backend.execute_command("area.create", second_values, {});
    ASSERT_TRUE(pending.prompt);
    ASSERT_EQ(pending.prompt->actions.size(), 3u);
    EXPECT_FALSE(std::filesystem::exists(project
        / "shared/areas/second_area.caf.json"));
    std::vector<std::string_view> discard_args;
    for (const auto& arg : pending.prompt->actions[1].args) {
        discard_args.push_back(arg);
    }
    const auto discarded = backend.execute_command(
        pending.prompt->actions[1].command_id, discard_args, {});
    ASSERT_TRUE(discarded.ok()) << discarded.message;
    EXPECT_TRUE(std::filesystem::is_regular_file(project
        / "shared/areas/second_area.caf.json"));
    EXPECT_FALSE(kernel::objects().valid(old_area));
    EXPECT_EQ(workspace.active_tab()->detail,
        "shared/areas/second_area.caf.json");
}
#endif

} // namespace
} // namespace nw::toolset
