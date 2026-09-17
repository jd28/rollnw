#include "../tools/client/client_frame.hpp"

#include <array>
#include <gtest/gtest.h>
#include <limits>

namespace nw::toolset {

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
