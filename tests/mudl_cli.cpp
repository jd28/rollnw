#include "mudl_cli.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace {

std::optional<int> parse_args_for(std::vector<std::string> args, mudl::ParsedArgs& out)
{
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    return mudl::parse_args(static_cast<int>(argv.size()), argv.data(), out);
}

} // namespace

TEST(MudlCli, TurntableDefaultsToModelNamedOutputDirectory)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "turntable", "c_aribeth"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_EQ(args.command, "turntable");
    EXPECT_EQ(args.initial_model, "c_aribeth");
    EXPECT_TRUE(args.turntable_mode);
    EXPECT_EQ(args.turntable_frames, 36);
    EXPECT_EQ(args.output_dir, "c_aribeth-turntable");
}

TEST(MudlCli, TurntableDefaultOutputUsesSourcePathStem)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "turntable", "/tmp/models/plc_palm02.mdl"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_EQ(args.initial_model, "/tmp/models/plc_palm02.mdl");
    EXPECT_EQ(args.output_dir, "plc_palm02-turntable");
}

TEST(MudlCli, TurntablePreservesExplicitOutputDirectory)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "turntable", "c_aribeth", "8", "--output", "/tmp/aribeth"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_EQ(args.turntable_frames, 8);
    EXPECT_EQ(args.output_dir, "/tmp/aribeth");
}

TEST(MudlCli, TurntableHelpDoesNotBecomeModelName)
{
    mudl::ParsedArgs args;

    testing::internal::CaptureStdout();
    const auto exit_code = parse_args_for({"mudl", "turntable", "--help"}, args);
    const auto stdout_text = testing::internal::GetCapturedStdout();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 0);
    EXPECT_TRUE(args.initial_model.empty());
    EXPECT_NE(stdout_text.find("Usage:"), std::string::npos);
}

TEST(MudlCli, TurntableRejectsOptionWhereModelIsRequired)
{
    mudl::ParsedArgs args;

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    const auto exit_code = parse_args_for({"mudl", "turntable", "--output", "/tmp/aribeth"}, args);
    testing::internal::GetCapturedStdout();
    const auto stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_TRUE(args.initial_model.empty());
    EXPECT_NE(stderr_text.find("requires a model"), std::string::npos);
}

TEST(MudlCli, OutputOptionRequiresDirectory)
{
    mudl::ParsedArgs args;

    testing::internal::CaptureStderr();
    const auto exit_code = parse_args_for({"mudl", "turntable", "c_aribeth", "--output"}, args);
    const auto stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("--output requires a directory"), std::string::npos);
}

TEST(MudlCli, AreaBenchmarkParsesForwardPlusGpuCullFlag)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "area-benchmark", "ms_4city", "--forward-plus-gpu-cull"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_TRUE(args.benchmark_forward_plus_policy.gpu_culling);
}

TEST(MudlCli, AreaBenchmarkDefaultsToForwardPlusGpuCull)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "area-benchmark", "ms_4city"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_TRUE(args.benchmark_forward_plus_policy.gpu_culling);
    EXPECT_TRUE(args.benchmark_local_shadows_enabled);
    EXPECT_FALSE(args.benchmark_area_time_seconds.has_value());
}

TEST(MudlCli, AreaBenchmarkParsesLocalShadowDisableFlag)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "area-benchmark", "ms_4city", "--no-local-shadows"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_FALSE(args.benchmark_local_shadows_enabled);
    EXPECT_TRUE(args.benchmark_shadows_enabled);
}

TEST(MudlCli, AreaSweepParsesLocalShadowDisableFlag)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "area-sweep", "ms_4city", "--no-local-shadows"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_FALSE(args.benchmark_local_shadows_enabled);
}

TEST(MudlCli, AreaBenchmarkParsesAreaTime)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "area-benchmark", "ms_4city", "--area-time", "22.5"}, args);

    EXPECT_FALSE(exit_code.has_value());
    ASSERT_TRUE(args.benchmark_area_time_seconds.has_value());
    EXPECT_FLOAT_EQ(*args.benchmark_area_time_seconds, 22.5f);
}

TEST(MudlCli, ParsesPbrIblDisableFlag)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "view", "tests/test_data/renderer/Fox/glTF/Fox.gltf", "--no-pbr-ibl"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_FALSE(args.pbr_ibl_enabled);
}

TEST(MudlCli, RejectsRetiredGltfAnimationStatePathFlags)
{
    mudl::ParsedArgs args;

    testing::internal::CaptureStderr();
    auto exit_code = parse_args_for({"mudl", "view", "tests/test_data/renderer/Fox/glTF/Fox.gltf", "--legacy-gltf-animation-state"}, args);
    auto stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("RenderModel animation state is always stored per instance"), std::string::npos);

    args = {};
    testing::internal::CaptureStderr();
    exit_code = parse_args_for({"mudl", "view", "tests/test_data/renderer/Fox/glTF/Fox.gltf", "--unified-gltf-animation-state"}, args);
    stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("RenderModel animation state is always stored per instance"), std::string::npos);
}

TEST(MudlCli, RejectsRetiredRenderModelDrawPathFlags)
{
    mudl::ParsedArgs args;

    testing::internal::CaptureStderr();
    auto exit_code = parse_args_for({"mudl", "view", "tests/test_data/renderer/Fox/glTF/Fox.gltf", "--legacy-render-model-draws"}, args);
    auto stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("RenderModel surfaces always use the common prepared draw path"), std::string::npos);

    args = {};
    testing::internal::CaptureStderr();
    exit_code = parse_args_for({"mudl", "view", "tests/test_data/renderer/Fox/glTF/Fox.gltf", "--prepared-render-model-draws"}, args);
    stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("RenderModel surfaces always use the common prepared draw path"), std::string::npos);
}

TEST(MudlCli, RejectsRetiredNwnPreparedDrawPathFlags)
{
    mudl::ParsedArgs args;

    testing::internal::CaptureStderr();
    auto exit_code = parse_args_for({"mudl", "view", "c_aribeth", "--prepared-nwn-draws"}, args);
    auto stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("non-area NWN sidecars always use the common prepared draw path"), std::string::npos);

    args = {};
    testing::internal::CaptureStderr();
    exit_code = parse_args_for({"mudl", "view", "c_aribeth", "--legacy-nwn-draws"}, args);
    stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("non-area NWN sidecars always use the common prepared draw path"), std::string::npos);
}

TEST(MudlCli, ParsesLegacyNwnModelLoadPathFlag)
{
    mudl::ParsedArgs args;

    auto exit_code = parse_args_for({"mudl", "view", "plc_palm02", "--legacy-nwn-model-path"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_TRUE(args.legacy_nwn_model_path);
}

TEST(MudlCli, RejectsRetiredNwnRenderModelLoadPathFlag)
{
    mudl::ParsedArgs args;

    testing::internal::CaptureStderr();
    auto exit_code = parse_args_for({"mudl", "view", "plc_palm02", "--render-model-nwn-model-path"}, args);
    auto stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("NWN previews use ModelAsset -> RenderModel by default"), std::string::npos);
}

TEST(MudlCli, RejectsRetiredGltfModelLoadPathFlags)
{
    mudl::ParsedArgs args;

    testing::internal::CaptureStderr();
    auto exit_code = parse_args_for(
        {"mudl", "view", "tests/test_data/renderer/Fox/glTF/Fox.gltf", "--legacy-gltf-model-path"}, args);
    auto stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("glTF previews always use ModelAsset -> RenderModel"), std::string::npos);

    args = {};
    testing::internal::CaptureStderr();
    exit_code = parse_args_for(
        {"mudl", "view", "tests/test_data/renderer/Fox/glTF/Fox.gltf", "--model-asset-gltf-model-path"}, args);
    stderr_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(exit_code.has_value());
    EXPECT_EQ(*exit_code, 1);
    EXPECT_NE(stderr_text.find("glTF previews always use ModelAsset -> RenderModel"), std::string::npos);
}

TEST(MudlCli, AreaSweepParsesForwardPlusGpuCullDisableFlag)
{
    mudl::ParsedArgs args;
    const std::vector<std::string> cli_args{
        "mudl",
        "area-sweep",
        "ms_4city",
        "--forward-plus-gpu-cull",
        "--no-forward-plus-gpu-cull",
    };

    const auto exit_code = parse_args_for(cli_args, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_FALSE(args.benchmark_forward_plus_policy.gpu_culling);
}
