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

TEST(MudlCli, ModelBenchmarkParsesMeasuredFrameOptions)
{
    mudl::ParsedArgs args;
    const auto exit_code = parse_args_for({"mudl", "model-benchmark", "models/actor.glb",
                                              "--frames", "600", "--warmup", "180", "--step-ms", "16",
                                              "--width", "1920", "--height", "1080", "--animation", "walk",
                                              "--json", "samples.json", "--screenshot", "frame.png"},
        args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_EQ(args.initial_model, "models/actor.glb");
    EXPECT_EQ(args.benchmark_frames, 600);
    EXPECT_EQ(args.benchmark_warmup_frames, 180);
    EXPECT_EQ(args.benchmark_step_ms, 16);
    EXPECT_EQ(args.benchmark_width, 1920);
    EXPECT_EQ(args.benchmark_height, 1080);
    EXPECT_EQ(args.animation_override, "walk");
    EXPECT_EQ(args.benchmark_output_path, "samples.json");
    EXPECT_EQ(args.benchmark_screenshot_path, "frame.png");
    EXPECT_TRUE(args.screenshot_path.empty());
}

TEST(MudlCli, ModelBenchmarkRejectsInvalidNumericInputs)
{
    for (const auto option : {"--frames", "--warmup", "--width", "--height", "--step-ms"}) {
        for (const auto value : {"", "-1", "16ms", "16.5", "99999999999999999999"}) {
            mudl::ParsedArgs args;
            testing::internal::CaptureStderr();
            const auto exit_code = parse_args_for({"mudl", "model-benchmark", "actor.glb", option, value}, args);
            const auto error = testing::internal::GetCapturedStderr();
            EXPECT_EQ(exit_code, 1) << option << ' ' << value;
            EXPECT_NE(error.find(option), std::string::npos);
        }
        mudl::ParsedArgs args;
        testing::internal::CaptureStderr();
        const auto exit_code = parse_args_for({"mudl", "model-benchmark", "actor.glb", option}, args);
        const auto error = testing::internal::GetCapturedStderr();
        EXPECT_EQ(exit_code, 1) << option;
        EXPECT_NE(error.find(option), std::string::npos);
    }
}

TEST(MudlCli, ModelBenchmarkChecksCountAndStepBoundaries)
{
    for (const auto option : {"--frames", "--width", "--height"}) {
        mudl::ParsedArgs args;
        testing::internal::CaptureStderr();
        EXPECT_EQ(parse_args_for({"mudl", "model-benchmark", "actor.glb", option, "0"}, args), 1);
        testing::internal::GetCapturedStderr();
    }
    for (const auto step : {"0", "1000"}) {
        mudl::ParsedArgs args;
        EXPECT_FALSE(parse_args_for({"mudl", "model-benchmark", "actor.glb",
                                        "--frames", "1", "--warmup", "0", "--step-ms", step},
            args)
                .has_value());
    }
    for (const auto& extra : {std::vector<std::string>{"--frames", "2147483647", "--warmup", "1"},
             std::vector<std::string>{"--step-ms", "1001"}}) {
        std::vector<std::string> command{"mudl", "model-benchmark", "actor.glb"};
        command.insert(command.end(), extra.begin(), extra.end());
        mudl::ParsedArgs args;
        testing::internal::CaptureStderr();
        EXPECT_EQ(parse_args_for(command, args), 1);
        testing::internal::GetCapturedStderr();
    }
}

TEST(MudlCli, ModelBenchmarkRequiresSourceAndRejectsUnknownOptions)
{
    for (const auto& extra : {std::vector<std::string>{}, std::vector<std::string>{""},
             std::vector<std::string>{"--frames", "60"},
             std::vector<std::string>{"actor.glb", "--unknown"},
             std::vector<std::string>{"actor.glb", "--json"},
             std::vector<std::string>{"actor.glb", "--screenshot"},
             std::vector<std::string>{"actor.glb", "--json", ""},
             std::vector<std::string>{"actor.glb", "--screenshot", ""},
             std::vector<std::string>{"actor.glb", "--json", "--validate"},
             std::vector<std::string>{"actor.glb", "--screenshot", "--validate"}}) {
        std::vector<std::string> command{"mudl", "model-benchmark"};
        command.insert(command.end(), extra.begin(), extra.end());
        mudl::ParsedArgs args;
        testing::internal::CaptureStderr();
        testing::internal::CaptureStdout();
        EXPECT_EQ(parse_args_for(command, args), 1);
        testing::internal::GetCapturedStdout();
        testing::internal::GetCapturedStderr();
    }
}

TEST(MudlCli, ModelBenchmarkHelpDoesNotLoadAModel)
{
    mudl::ParsedArgs args;
    testing::internal::CaptureStdout();
    EXPECT_EQ(parse_args_for({"mudl", "model-benchmark", "--help"}, args), 0);
    const auto output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("model-benchmark"), std::string::npos);
    EXPECT_TRUE(args.initial_model.empty());
}

TEST(MudlCli, AreaBenchmarkDefaultsToForwardPlusGpuCull)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "area-benchmark", "ms_4city"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_TRUE(args.benchmark_forward_plus_policy.gpu_culling);
    EXPECT_TRUE(args.benchmark_local_shadows_enabled);
    EXPECT_FALSE(args.benchmark_area_time_seconds.has_value());
    EXPECT_EQ(args.benchmark_step_ms, 0);
}

TEST(MudlCli, AreaBenchmarkAcceptsFixedStepBoundaries)
{
    for (const auto value : {"0", "16", "1000"}) {
        mudl::ParsedArgs args;
        const auto exit_code = parse_args_for({"mudl", "area-benchmark", "ms_4city", "--step-ms", value}, args);
        EXPECT_FALSE(exit_code.has_value()) << value;
        EXPECT_EQ(args.benchmark_step_ms, std::stoi(value));
    }
}

TEST(MudlCli, AreaBenchmarkRejectsInvalidFixedSteps)
{
    for (const auto value : {"", "-1", "1001", "16ms", "16.5", "nan", "99999999999999999999"}) {
        mudl::ParsedArgs args;
        testing::internal::CaptureStderr();
        const auto exit_code = parse_args_for({"mudl", "area-benchmark", "ms_4city", "--step-ms", value}, args);
        const auto error = testing::internal::GetCapturedStderr();
        EXPECT_EQ(exit_code, 1) << value;
        EXPECT_NE(error.find("--step-ms"), std::string::npos);
    }
}

TEST(MudlCli, AreaBenchmarkRequiresFixedStepValue)
{
    mudl::ParsedArgs args;
    testing::internal::CaptureStderr();
    const auto exit_code = parse_args_for({"mudl", "area-benchmark", "ms_4city", "--step-ms"}, args);
    const auto error = testing::internal::GetCapturedStderr();
    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(error.find("--step-ms"), std::string::npos);
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

TEST(MudlCli, AreaEditBenchmarkParsesMeasurementOptions)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({
                                              "mudl",
                                              "area-edit-benchmark",
                                              "fda",
                                              "--module",
                                              "/tmp/module",
                                              "--samples",
                                              "7",
                                              "--warmup",
                                              "2",
                                              "--width",
                                              "1024",
                                              "--height",
                                              "768",
                                              "--json",
                                              "/tmp/area-edit.json",
                                          },
        args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_EQ(args.command, "area-edit-benchmark");
    EXPECT_EQ(args.initial_model, "fda");
    EXPECT_EQ(args.module_path, "/tmp/module");
    EXPECT_EQ(args.benchmark_frames, 7);
    EXPECT_EQ(args.benchmark_warmup_frames, 2);
    EXPECT_EQ(args.benchmark_width, 1024);
    EXPECT_EQ(args.benchmark_height, 768);
    EXPECT_EQ(args.benchmark_output_path, "/tmp/area-edit.json");
}

TEST(MudlCli, ParsesPbrIblDisableFlag)
{
    mudl::ParsedArgs args;

    const auto exit_code = parse_args_for({"mudl", "view", "tests/test_data/renderer/Fox/glTF/Fox.gltf", "--no-pbr-ibl"}, args);

    EXPECT_FALSE(exit_code.has_value());
    EXPECT_FALSE(args.pbr_ibl_enabled);
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
