#include "project.hpp"
#include "project_import.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Item.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace nw::toolset {
namespace {

class ClientProjectImport : public ::testing::Test {
protected:
    void SetUp() override
    {
        root = std::filesystem::path{"tmp/client_gui_import"}
            / ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "projects");
        module = root / "module ; $ with spaces.mod";
        std::filesystem::copy_file("test_data/user/modules/DockerDemo.mod", module);
    }

    std::optional<ProjectImportCompletion> finish(ProjectImportJob& job)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto result = job.poll()) { return result; }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        ADD_FAILURE() << "Import did not finish within 60 seconds";
        return std::nullopt;
    }

    const std::filesystem::path executable{ROLLNW_TEST_CLIENT_EXECUTABLE};
    std::filesystem::path root;
    std::filesystem::path module;
};

TEST_F(ClientProjectImport, ImportsNativeProjectWithoutReplacingLiveRuntime)
{
    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);
    const auto handle = item->handle();
    item->comment = "Keep this unsaved edit";
    auto* runtime = &nw::kernel::runtime();

    ProjectImportJob job;
    EXPECT_FALSE(job.poll());
    std::string error;
    ASSERT_TRUE(job.start(executable, module, root / "projects", error)) << error;
    EXPECT_TRUE(job.active());
    EXPECT_FALSE(job.start(executable, module, root / "projects", error));
    EXPECT_NE(error.find("already running"), std::string::npos);
    const auto result = finish(job);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->ok) << result->message;
    EXPECT_FALSE(job.active());
    EXPECT_FALSE(job.poll());
    EXPECT_TRUE(is_project_directory(result->project_dir));
    EXPECT_TRUE(std::filesystem::is_regular_file(result->project_dir / "shared/areas/start.caf.json"));
    EXPECT_TRUE(std::filesystem::is_regular_file(result->project_dir / "import.log"));
    EXPECT_EQ(&nw::kernel::runtime(), runtime);
    ASSERT_TRUE(nw::kernel::objects().valid(handle));
    EXPECT_EQ(item->comment, "Keep this unsaved edit");
    nw::kernel::objects().destroy(handle);
}

TEST_F(ClientProjectImport, RejectsExistingDestinationsAndInvalidInputsWithoutWrites)
{
    ProjectImportJob job;
    std::string error;
    EXPECT_FALSE(job.start(executable, root / "missing.mod", root / "projects", error));
    EXPECT_FALSE(job.start(root / "missing-client", module, root / "projects", error));
    EXPECT_FALSE(job.start(executable, module, root / "missing-parent", error));
    EXPECT_TRUE(std::filesystem::is_empty(root / "projects"));
    const auto destination = root / "projects" / module.stem();
    std::filesystem::create_directory(destination);
    EXPECT_FALSE(job.start(executable, module, root / "projects", error));
    EXPECT_TRUE(std::filesystem::is_empty(destination));
    {
        std::ofstream marker{destination / "keep.txt"};
        marker << "Keep existing content";
    }
    EXPECT_FALSE(job.start(executable, module, root / "projects", error));
    std::ifstream marker{destination / "keep.txt"};
    std::string text;
    std::getline(marker, text);
    EXPECT_EQ(text, "Keep existing content");
    EXPECT_FALSE(std::filesystem::exists(destination / "import.log"));

    std::filesystem::create_directory(root / "links");
    std::error_code ec;
    std::filesystem::create_directory_symlink(std::filesystem::absolute(destination), root / "links" / module.stem(), ec);
    if (!ec) {
        EXPECT_FALSE(job.start(executable, module, root / "links", error));
        EXPECT_FALSE(std::filesystem::exists(destination / "import.log"));
    }
    EXPECT_FALSE(job.active());
}

TEST_F(ClientProjectImport, FailedImportReportsDiagnosticsAndPreservesRuntime)
{
    const auto invalid_module = root / "invalid.mod";
    {
        std::ofstream invalid{invalid_module};
        invalid << "Not an NWN module";
    }
    auto* item = nw::kernel::objects().make<nw::Item>();
    ASSERT_NE(item, nullptr);
    const auto handle = item->handle();
    auto* runtime = &nw::kernel::runtime();
    ProjectImportJob job;
    std::string error;
    ASSERT_TRUE(job.start(executable, invalid_module, root / "projects", error)) << error;
    const auto result = finish(job);
    ASSERT_TRUE(result);
    EXPECT_FALSE(result->ok);
    EXPECT_NE(result->message.find("Import failed"), std::string::npos);
    EXPECT_NE(result->message.find("import.log"), std::string::npos);
    EXPECT_FALSE(job.active());
    EXPECT_FALSE(is_project_directory(result->project_dir));
    EXPECT_GT(std::filesystem::file_size(result->project_dir / "import.log"), 0u);
    EXPECT_EQ(&nw::kernel::runtime(), runtime);
    EXPECT_TRUE(nw::kernel::objects().valid(handle));
    nw::kernel::objects().destroy(handle);
}

} // namespace
} // namespace nw::toolset
