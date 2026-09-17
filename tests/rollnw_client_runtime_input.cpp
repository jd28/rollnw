#include "runtime_input.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>

using namespace nw::toolset;

TEST(ClientRuntimeInput, AcquisitionRetainsPendingEdgesUntilTheConsumerEmitsTicks)
{
    RuntimeInputState input;
    input.mouse_look_pixels = {10, -20};
    input.wheel_zoom = 2;
    ASSERT_TRUE(set_preview_click_target(input.pending, {3, 4, 5}));
    input.pending.flags |= preview_input_cancel;
    PcDeviceSample physical;
    ASSERT_EQ(acquire_pc_device_sample(input, 0.001, {}, physical), PreviewStatus::ok);
    EXPECT_FALSE(physical.controller_connected);
    PreviewInputSample sample;
    ASSERT_EQ(translate_pc_input_samples({&physical, 1}, {&sample, 1}), PreviewStatus::ok);
    PreviewFixedStepState fixed;
    std::array<PreviewInputSample, 6> ticks{};
    EXPECT_EQ(build_preview_tick_samples(fixed, 0.001, sample, ticks).tick_count, 0u);
    EXPECT_EQ(input.pending.flags, preview_input_click_target | preview_input_cancel);
    EXPECT_EQ(input.mouse_look_pixels, (glm::vec2{10, -20}));
    EXPECT_EQ(input.wheel_zoom, 2);
    ASSERT_EQ(acquire_pc_device_sample(input, 0.05, {}, physical), PreviewStatus::ok);
    EXPECT_DOUBLE_EQ(physical.mouse_sample_seconds, 0.051);
    ASSERT_EQ(translate_pc_input_samples({&physical, 1}, {&sample, 1}), PreviewStatus::ok);
    ASSERT_EQ(build_preview_tick_samples(fixed, 0.05, sample, ticks).tick_count, 3u);
    consume_runtime_input_edges(input);
    EXPECT_EQ(input.pending.flags, preview_input_none);
    EXPECT_EQ(input.mouse_look_pixels, glm::vec2{});
    EXPECT_DOUBLE_EQ(input.mouse_sample_seconds, 0);
    EXPECT_EQ(input.wheel_zoom, 0);
}

TEST(ClientRuntimeInput, ClaimedPointerAndControllerEdgesAreDiscardedWithoutReplay)
{
    RuntimeInputState input;
    ASSERT_TRUE(set_preview_click_door(input.pending, 2, {0, 0, 0}, {1, 1, 1}));
    input.pending.flags |= preview_input_cancel;
    input.mouse_look_pixels = {10, 20};
    input.mouse_sample_seconds = 0.02;
    input.wheel_zoom = 3;
    PcDeviceSample physical;
    ASSERT_EQ(acquire_pc_device_sample(input, 0.01,
                  {.keyboard = true, .controller = false, .pointer = false}, physical),
        PreviewStatus::ok);
    EXPECT_EQ(input.pending.flags, preview_input_none);
    EXPECT_EQ(input.mouse_look_pixels, glm::vec2{});
    EXPECT_DOUBLE_EQ(input.mouse_sample_seconds, 0);
    EXPECT_EQ(input.wheel_zoom, 0);
    ASSERT_EQ(acquire_pc_device_sample(input, 0.01, {}, physical), PreviewStatus::ok);
    EXPECT_EQ(physical.pending.flags, preview_input_none);
    EXPECT_EQ(physical.mouse_look_pixels, glm::vec2{});
    EXPECT_EQ(physical.wheel_zoom, 0);
}

TEST(ClientRuntimeInput, NegativeElapsedContributesZeroAndInvalidElapsedRejects)
{
    RuntimeInputState input;
    input.mouse_sample_seconds = 0.02;
    input.wheel_zoom = 2;
    PcDeviceSample physical;
    ASSERT_EQ(acquire_pc_device_sample(input, -0.01, {}, physical), PreviewStatus::ok);
    EXPECT_DOUBLE_EQ(input.mouse_sample_seconds, 0.02);
    EXPECT_DOUBLE_EQ(physical.mouse_sample_seconds, 0.02);
    physical.pending.flags = preview_input_cancel;
    EXPECT_EQ(acquire_pc_device_sample(input, std::numeric_limits<double>::quiet_NaN(), {}, physical), PreviewStatus::invalid_input);
    EXPECT_DOUBLE_EQ(input.mouse_sample_seconds, 0.02);
    EXPECT_EQ(input.wheel_zoom, 2);
    EXPECT_EQ(physical.pending.flags, preview_input_none);
    close_runtime_gamepad(input);
    close_runtime_gamepad(input);
    EXPECT_FALSE(input.gamepad);
    reset_runtime_pending_input(input);
    EXPECT_DOUBLE_EQ(input.mouse_sample_seconds, 0);
    EXPECT_EQ(input.wheel_zoom, 0);
}
