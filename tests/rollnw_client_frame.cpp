#include "../tools/client/client_edit_trace.hpp"
#include "../tools/client/client_frame.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <nlohmann/json.hpp>

namespace nw::toolset {

TEST(ClientEditTrace, RecordsChangedFramesWithInclusivePhasesAndExplicitUnavailablePresentation)
{
    const std::filesystem::path path{"tmp/client-edit-trace.jsonl"};
    std::filesystem::create_directories(path.parent_path());
    {
        ClientEditTrace trace{path.string().c_str()};
        ASSERT_TRUE(trace.valid());
        trace.begin_frame(10);
        {
            const ScopedClientEditPhase input{ClientEditPhase::input};
            const ScopedClientEditPhase command{ClientEditPhase::command};
        }
        ASSERT_TRUE(trace.end_frame(11, true, 1280, 720));
        trace.begin_frame(11);
        ASSERT_TRUE(trace.end_frame(11, true, 1280, 720)); // Idle is omitted.
        trace.begin_frame(11);
        {
            const ScopedClientEditPhase command{ClientEditPhase::command};
        }
        ASSERT_TRUE(trace.end_frame(12, false, 0, 0));
    }
    // Disabled scopes also remain safe after the recorder is destroyed.
    {
        const ScopedClientEditPhase command{ClientEditPhase::command};
    }
    std::ifstream input{path};
    std::string line;
    ASSERT_TRUE(static_cast<bool>(std::getline(input, line)));
    const auto first = nlohmann::json::parse(line);
    EXPECT_EQ(first["schema"], 1);
    EXPECT_EQ(first["sequence"], 0);
    EXPECT_EQ(first["epoch_before"], 10);
    EXPECT_EQ(first["epoch_after"], 11);
    EXPECT_TRUE(first["dispatch_to_present_ms"].is_number());
    EXPECT_TRUE(first["display_latency_ms"].is_null());
    EXPECT_EQ(first["calls"]["input"], 1);
    EXPECT_EQ(first["calls"]["command"], 1);
    EXPECT_GE(first["inclusive_ms"]["input"].get<double>(), first["inclusive_ms"]["command"].get<double>());
    ASSERT_TRUE(static_cast<bool>(std::getline(input, line)));
    const auto skipped = nlohmann::json::parse(line);
    EXPECT_EQ(skipped["sequence"], 1);
    EXPECT_FALSE(skipped["present_returned"].get<bool>());
    EXPECT_TRUE(skipped["dispatch_to_present_ms"].is_null());
    EXPECT_EQ(skipped["calls"]["input"], 0);
    EXPECT_EQ(skipped["calls"]["command"], 1);
    EXPECT_FALSE(static_cast<bool>(std::getline(input, line)));
}

TEST(ClientEditTrace, DisabledAndInvalidOutputsHaveExplicitResults)
{
    ClientEditTrace disabled{nullptr};
    ASSERT_TRUE(disabled.valid());
    disabled.begin_frame(1);
    {
        const ScopedClientEditPhase command{ClientEditPhase::command};
    }
    EXPECT_TRUE(disabled.end_frame(2, true, 1280, 720));
    ClientEditTrace invalid{""};
    EXPECT_TRUE(invalid.valid());
    // A directory cannot be opened as a trace file.
    std::filesystem::create_directories("tmp");
    ClientEditTrace directory{"tmp"};
    EXPECT_FALSE(directory.valid());
}

TEST(ClientApplicationFrames, StartupAndRegressionsKeepRawTimeSeparateFromCameraCap)
{
    std::array<ClientFrameClock, 1> clocks{{{500, 0}}};
    std::array<ClientFrameSample, 1> samples{{{516, 5000, 1000}}};
    std::array<ClientFrameDelta, 1> deltas{};
    ASSERT_TRUE(advance_client_frames(clocks, samples, deltas));
    EXPECT_EQ(deltas[0].raw_seconds, 0);
    EXPECT_EQ(deltas[0].camera_milliseconds, 16);
    samples[0] = {2000, 7000, 1000};
    ASSERT_TRUE(advance_client_frames(clocks, samples, deltas));
    EXPECT_EQ(deltas[0].raw_seconds, 2);
    EXPECT_EQ(deltas[0].camera_milliseconds, 100);
    samples[0] = {100, 4000, 1000};
    ASSERT_TRUE(advance_client_frames(clocks, samples, deltas));
    EXPECT_EQ(deltas[0].raw_seconds, 0);
    EXPECT_EQ(deltas[0].camera_milliseconds, 0);
    EXPECT_EQ(clocks[0].ticks, 100);
    EXPECT_EQ(clocks[0].counter, 4000);
    samples[0] = {std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max(), 1};
    ASSERT_TRUE(advance_client_frames(clocks, samples, deltas));
    EXPECT_GT(deltas[0].raw_seconds, 0);
    EXPECT_EQ(deltas[0].camera_milliseconds, 100);
    RecordProperty("frame_clock_bytes", sizeof(ClientFrameClock));
    RecordProperty("frame_sample_bytes", sizeof(ClientFrameSample));
    RecordProperty("frame_delta_bytes", sizeof(ClientFrameDelta));
}

TEST(ClientApplicationFrames, IndependentBatchClocksRejectInvalidSamplesWithoutPartialAdvancement)
{
    std::array<ClientFrameClock, 2> clocks{{{10, 1}, {20, 2}}};
    std::array<ClientFrameSample, 2> samples{{{14, 101, 100}, {25, 152, 1000}}};
    std::array<ClientFrameDelta, 2> deltas{};
    ASSERT_TRUE(advance_client_frames(clocks, samples, deltas));
    EXPECT_EQ(deltas[0].raw_seconds, 1);
    EXPECT_FLOAT_EQ(deltas[1].raw_seconds, 0.15f);
    EXPECT_EQ(deltas[0].camera_milliseconds, 4);
    EXPECT_EQ(deltas[1].camera_milliseconds, 5);
    const auto previous = clocks;
    samples[1].frequency = 0;
    EXPECT_FALSE(advance_client_frames(clocks, samples, deltas));
    for (size_t i = 0; i < clocks.size(); ++i) {
        EXPECT_EQ(clocks[i].ticks, previous[i].ticks);
        EXPECT_EQ(clocks[i].counter, previous[i].counter);
        EXPECT_EQ(deltas[i].raw_seconds, 0);
        EXPECT_EQ(deltas[i].camera_milliseconds, 0);
    }
    deltas.fill({1, 100});
    EXPECT_FALSE(advance_client_frames(clocks, {}, deltas));
    for (const auto& delta : deltas) {
        EXPECT_EQ(delta.raw_seconds, 0);
        EXPECT_EQ(delta.camera_milliseconds, 0);
    }
    EXPECT_TRUE(advance_client_frames({}, {}, {}));
}

} // namespace nw::toolset
