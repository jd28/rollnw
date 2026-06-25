#include "benchmark_report.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace {

TEST(MudlBenchmarkReport, EmptyPathMetadataIsNull)
{
    EXPECT_TRUE(mudl::benchmark_path_metadata_json("").is_null());
}

TEST(MudlBenchmarkReport, PathMetadataPreservesInputPathAndAddsAbsolutePath)
{
    const std::filesystem::path root = "tmp/mudl_benchmark_report";
    const std::filesystem::path module_path = root / "module";
    std::filesystem::create_directories(module_path);
    std::ofstream{module_path / "module.json"} << "{}\n";

    const std::string input_path = module_path.generic_string();
    const auto metadata = mudl::benchmark_path_metadata_json(input_path);

    ASSERT_TRUE(metadata.is_object());
    ASSERT_EQ(metadata.at("path"), input_path);
    ASSERT_TRUE(metadata.contains("absolute_path"));
    const std::filesystem::path absolute_path = metadata.at("absolute_path").get<std::string>();
    EXPECT_TRUE(absolute_path.is_absolute());
    EXPECT_EQ(std::filesystem::weakly_canonical(module_path), absolute_path);
}

} // namespace
