#include "blueprint_operations.hpp"
#include "blueprint_update_job.hpp"
#include "project.hpp"
#include "workspace.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/resources/ResourceManager.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
namespace nw::toolset {
namespace {
std::string read(const fs::path& path)
{
    std::ifstream input{path};
    return {std::istreambuf_iterator<char>{input}, {}};
}

class ClientBlueprintJobs : public testing::Test {
protected:
    fs::path project;
    Resource source;
    std::vector<std::string> before;
    std::vector<fs::path> area_paths;

    void SetUp() override
    {
        project = fs::absolute(fs::path{"tmp/blueprint_jobs"} / testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(project);
        const auto imported = import_module_project("test_data/user/modules/DockerDemo.mod", project, {ProjectImportFormat::json});
        ASSERT_TRUE(imported.ok) << imported.message;
        ASSERT_NE(kernel::load_module(project, false, kernel::module_load_options_for_project(project)), nullptr);
        source = Resource{std::string_view{"job_item"}, ResourceType::uti};
        ObjectDocument item_owner;
        auto* item = kernel::objects().load_file<Item>("test_data/user/development/cloth028.uti");
        ASSERT_NE(item, nullptr);
        ASSERT_TRUE(item_owner.adopt(item->handle()));
        item->resref = source.resref;
        item->comment = "Saved blueprint value";
        const auto blueprint = project / "shared/blueprints/items/job_item.uti.json";
        fs::create_directories(blueprint.parent_path());
        std::ofstream{blueprint} << "{}";
        std::string error;
        ASSERT_TRUE(save_live_blueprint_json_atomic(item->handle(), blueprint, error)) << error;
        ASSERT_TRUE(kernel::resman().refresh_module_resources(error)) << error;
        ObjectDocument area_owner;
        auto* area = kernel::objects().make_area(Resref{"start"});
        ASSERT_NE(area, nullptr);
        ASSERT_TRUE(area_owner.adopt(area->handle()));
        auto* placed = kernel::objects().load<Item>(source.resref);
        ASSERT_NE(placed, nullptr);
        placed->comment = "Instance override";
        auto* spatial = kernel::objects().components().get_or_create_spatial(placed->handle());
        spatial->position = {5, 5, 0.5f};
        spatial->orientation = {0, 1, 0};
        spatial->area = area->handle().id;
        area->items.push_back(placed);
        for (const auto* name : {"start", "job_second"}) {
            const auto path = project / "shared/areas" / (std::string{name} + ".caf.json");
            if (!fs::exists(path)) { std::ofstream{path} << "{}"; }
            ASSERT_TRUE(save_live_area_json_atomic(area->handle(), path, error)) << error;
            area_paths.push_back(path);
            before.push_back(read(path));
        }
        ASSERT_TRUE(kernel::resman().refresh_module_resources(error)) << error;
    }

    fs::path prepare()
    {
        std::string error;
        const auto operation = create_blueprint_update_operation(project, source, {}, error);
        EXPECT_FALSE(operation.empty()) << error;
        EXPECT_TRUE(run_blueprint_update_operation(operation, "prepare", error)) << error;
        return operation;
    }

    std::optional<int> finish(BlueprintUpdateJob& job)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto result = job.poll()) { return result; }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        ADD_FAILURE() << "Blueprint worker did not finish within 60 seconds";
        return std::nullopt;
    }
};

TEST_F(ClientBlueprintJobs, IsolatedWorkerPreparesCommitsAndRestoresMultipleAreas)
{
    std::string error;
    const auto operation = create_blueprint_update_operation(project, source, {}, error);
    ASSERT_FALSE(operation.empty()) << error;
    const auto* runtime = &kernel::runtime();
    ObjectDocument live;
    auto* item = kernel::objects().make<Item>();
    ASSERT_TRUE(live.adopt(item->handle()));
    item->comment = "Keep live unsaved object";
    BlueprintUpdateJob job;
    for (const auto* phase : {"prepare", "commit", "restore"}) {
        const auto started = std::chrono::steady_clock::now();
        ASSERT_TRUE(job.start(ROLLNW_TEST_CLIENT_EXECUTABLE, operation, phase, error)) << error;
        EXPECT_FALSE(job.start(ROLLNW_TEST_CLIENT_EXECUTABLE, operation, phase, error));
        const auto result = finish(job);
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(*result, 0) << read(operation / (std::string{phase} + ".log"));
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        RecordProperty(std::string{phase} + "_milliseconds", milliseconds);
        EXPECT_EQ(&kernel::runtime(), runtime);
        ASSERT_TRUE(kernel::objects().valid(live.object()));
        EXPECT_EQ(item->comment, "Keep live unsaved object");
        if (std::string_view{phase} == "prepare") {
            EXPECT_EQ(read_blueprint_operation_progress(operation).stage, "ready");
            EXPECT_EQ(blueprint_operation_documents(operation).size(), 2u);
            for (size_t i = 0; i < area_paths.size(); ++i) {
                EXPECT_EQ(read(area_paths[i]), before[i]);
            }
        } else if (std::string_view{phase} == "commit") {
            EXPECT_EQ(read_blueprint_operation_progress(operation).stage, "saved");
            EXPECT_EQ(find_unfinished_blueprint_operations(project).size(), 1u);
            for (size_t i = 0; i < area_paths.size(); ++i) {
                EXPECT_NE(read(area_paths[i]), before[i]);
            }
            ASSERT_TRUE(finish_blueprint_update_operation(operation, error)) << error;
        }
    }
    EXPECT_TRUE(find_unfinished_blueprint_operations(project).empty());
    for (size_t i = 0; i < area_paths.size(); ++i) {
        EXPECT_EQ(read(area_paths[i]), before[i]);
    }
}

TEST_F(ClientBlueprintJobs, ChangedScopeAndCancellationPublishNothing)
{
    auto operation = prepare();
    ASSERT_FALSE(operation.empty());
    fs::copy_file(area_paths[0], project / "shared/areas/added.caf.json");
    std::string error;
    EXPECT_FALSE(run_blueprint_update_operation(operation, "commit", error));
    EXPECT_NE(error.find("document set changed"), std::string::npos) << error;
    for (size_t i = 0; i < area_paths.size(); ++i) {
        EXPECT_EQ(read(area_paths[i]), before[i]);
    }
    operation = create_blueprint_update_operation(project, source, {}, error);
    ASSERT_FALSE(operation.empty()) << error;
    ASSERT_TRUE(cancel_blueprint_update_operation(operation, error));
    EXPECT_FALSE(run_blueprint_update_operation(operation, "prepare", error));
    EXPECT_NE(error.find("Cancelled"), std::string::npos);
    for (size_t i = 0; i < area_paths.size(); ++i) {
        EXPECT_EQ(read(area_paths[i]), before[i]);
    }
}

TEST_F(ClientBlueprintJobs, RecoveryReconcilesEveryInterruptedCommitBoundaryAndPreservesLaterEdits)
{
    const auto operation = prepare();
    ASSERT_FALSE(operation.empty());
    auto manifest = nlohmann::json::parse(read(operation / "manifest.json"));
    const auto& rows = manifest.at("rows");
    ASSERT_EQ(rows.size(), 2u);
    std::string error;
    for (size_t boundary = 0; boundary <= rows.size(); ++boundary) {
        manifest["stage"] = "saving";
        std::ofstream{operation / "manifest.json"} << manifest.dump();
        for (size_t index = 0; index < rows.size(); ++index) {
            const auto target = project / rows[index].at("path").get<std::string>();
            std::ofstream{target} << read(operation / (std::to_string(index) + (index < boundary ? ".after" : ".before")));
        }
        EXPECT_EQ(find_unfinished_blueprint_operations(project).size(), 1u);
        ASSERT_TRUE(run_blueprint_update_operation(operation, "restore", error)) << error;
        for (size_t i = 0; i < area_paths.size(); ++i) {
            EXPECT_EQ(read(area_paths[i]), before[i]);
        }
    }
    manifest["stage"] = "saving";
    std::ofstream{operation / "manifest.json"} << manifest.dump();
    const auto first = project / rows[0].at("path").get<std::string>();
    std::ofstream{first} << "Subsequent edit";
    EXPECT_FALSE(run_blueprint_update_operation(operation, "restore", error));
    EXPECT_NE(error.find("subsequent edits"), std::string::npos) << error;
    EXPECT_EQ(read(first), "Subsequent edit");
}
} // namespace
} // namespace nw::toolset
