#include "client_input_routes.hpp"
#include "pc_input.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>

using namespace nw::toolset;

TEST(ClientPcInput, PhysicalKeysUseOneMovementTurnAndLookMap)
{
    std::array<PcDeviceSample, static_cast<size_t>(PcKey::count)> inputs{};
    std::array<PreviewInputSample, inputs.size()> outputs{};
    for (size_t index = 0; index < inputs.size(); ++index) {
        inputs[index].keys[index] = true;
    }
    ASSERT_EQ(translate_pc_input_samples(inputs, outputs), PreviewStatus::ok);
    const std::array expected_move{glm::vec2{0, 1}, glm::vec2{0, -1}, glm::vec2{-1, 0}, glm::vec2{1, 0},
        glm::vec2{}, glm::vec2{}, glm::vec2{}, glm::vec2{}, glm::vec2{}, glm::vec2{}};
    const std::array expected_turn{0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const std::array expected_look{glm::vec2{}, glm::vec2{}, glm::vec2{}, glm::vec2{}, glm::vec2{}, glm::vec2{},
        glm::vec2{-1, 0}, glm::vec2{1, 0}, glm::vec2{0, -1}, glm::vec2{0, 1}};
    for (size_t index = 0; index < inputs.size(); ++index) {
        EXPECT_EQ(outputs[index].move_axis, expected_move[index]);
        EXPECT_EQ(outputs[index].turn_axis, expected_turn[index]);
        EXPECT_EQ(outputs[index].look_axis, expected_look[index]);
        EXPECT_EQ(outputs[index].zoom_axis, 0.0f);
        EXPECT_EQ(outputs[index].zoom_delta, 0.0f);
        EXPECT_EQ(outputs[index].flags, preview_input_none);
    }
}

TEST(ClientPcInput, CombinedInputsRetainBaselineClampsSignsAndMouseScaling)
{
    PcDeviceSample input;
    for (const auto key : {PcKey::e, PcKey::w, PcKey::d, PcKey::right, PcKey::down}) {
        input.keys[static_cast<size_t>(key)] = true;
    }
    input.controller_connected = true;
    input.left_stick = {0.6f, 0};
    input.right_stick = {1, 0};
    input.left_trigger = 0.25f;
    input.right_trigger = 1.0f;
    input.right_shoulder = true;
    input.mouse_look_pixels = {10, -20};
    input.mouse_sample_seconds = 0.02;
    input.wheel_zoom = 2.5f;
    ASSERT_TRUE(set_preview_click_target(input.pending, {3, 4, 5}));
    input.pending.flags |= preview_input_cancel;
    PreviewInputSample output;
    ASSERT_EQ(translate_pc_input_samples({&input, 1}, {&output, 1}), PreviewStatus::ok);
    EXPECT_EQ(output.move_axis, (glm::vec2{1, 1}));
    EXPECT_EQ(output.turn_axis, 1.0f);
    EXPECT_NEAR(output.look_axis.x, 2.7f, 1e-6f);
    EXPECT_NEAR(output.look_axis.y, -0.4f, 1e-6f);
    EXPECT_EQ(output.zoom_axis, 1.75f);
    EXPECT_EQ(output.zoom_delta, 2.5f);
    EXPECT_EQ(output.click_target, (glm::vec3{3, 4, 5}));
    EXPECT_EQ(output.flags, preview_input_click_target | preview_input_cancel);
    input.left_shoulder = true;
    input.right_shoulder = true;
    input.left_trigger = 0;
    input.right_trigger = 0.3f;
    ASSERT_EQ(translate_pc_input_samples({&input, 1}, {&output, 1}), PreviewStatus::ok);
    EXPECT_EQ(output.zoom_axis, 0.3f);
}

TEST(ClientPcInput, DeviceAbsenceDeadzoneAndNegativeStickYHaveExplicitResults)
{
    std::array<PcDeviceSample, 4> inputs{};
    inputs[0].left_stick = {1, -1}; // An absent device contributes nothing.
    inputs[1].controller_connected = true;
    inputs[1].left_stick = {0.2f, 0};
    inputs[1].right_stick = {0.1f, 0.1f};
    inputs[2].controller_connected = true;
    inputs[2].left_stick = {-1, -1};
    inputs[2].left_shoulder = true;
    inputs[3].controller_connected = true;
    inputs[3].left_stick = {0, 1};
    std::array<PreviewInputSample, inputs.size()> outputs{};
    ASSERT_EQ(translate_pc_input_samples(inputs, outputs), PreviewStatus::ok);
    EXPECT_EQ(outputs[0].move_axis, glm::vec2{});
    EXPECT_EQ(outputs[1].move_axis, glm::vec2{});
    EXPECT_EQ(outputs[1].look_axis, glm::vec2{});
    EXPECT_NEAR(outputs[2].move_axis.x, -0.70710678f, 1e-6f);
    EXPECT_NEAR(outputs[2].move_axis.y, 0.70710678f, 1e-6f);
    EXPECT_EQ(outputs[2].zoom_axis, -1.0f);
    EXPECT_EQ(outputs[3].move_axis, (glm::vec2{0, -1}));
}

TEST(ClientPcInput, ClaimedSourcesSuppressOnlyTheirOwnHeldAxesAndEdges)
{
    PcDeviceSample base;
    base.keys[static_cast<size_t>(PcKey::w)] = true;
    base.keys[static_cast<size_t>(PcKey::d)] = true;
    base.controller_connected = true;
    base.left_stick = {1, 0};
    base.right_trigger = 1;
    base.mouse_look_pixels = {10, 0};
    base.mouse_sample_seconds = 0.02;
    base.wheel_zoom = 2;
    ASSERT_TRUE(set_preview_click_door(base.pending, 4, {0, 0, 0}, {1, 1, 1}));
    base.pending.flags |= preview_input_cancel;
    std::array inputs{base, base, base};
    inputs[0].eligibility.keyboard = false;
    inputs[1].eligibility.controller = false;
    inputs[2].eligibility.pointer = false;
    std::array<PreviewInputSample, inputs.size()> outputs{};
    ASSERT_EQ(translate_pc_input_samples(inputs, outputs), PreviewStatus::ok);
    EXPECT_EQ(outputs[0].move_axis, (glm::vec2{1, 0}));
    EXPECT_EQ(outputs[0].turn_axis, 0.0f);
    EXPECT_EQ(outputs[1].move_axis, (glm::vec2{0, 1}));
    EXPECT_EQ(outputs[1].zoom_axis, 0.0f);
    EXPECT_EQ(outputs[1].flags, preview_input_click_door);
    EXPECT_EQ(outputs[2].move_axis, (glm::vec2{1, 1}));
    EXPECT_EQ(outputs[2].look_axis, glm::vec2{});
    EXPECT_EQ(outputs[2].zoom_delta, 0.0f);
    EXPECT_EQ(outputs[2].flags, preview_input_cancel);
}

TEST(ClientPcInput, InvalidRowsAndMismatchedSpansClearTheWholeOutputBatch)
{
    PcDeviceSample valid;
    valid.keys[static_cast<size_t>(PcKey::w)] = true;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::array<PcDeviceSample, 6> invalid{};
    invalid[0].mouse_look_pixels.x = nan;
    invalid[1].mouse_sample_seconds = -1;
    invalid[2].controller_connected = true;
    invalid[2].left_stick.x = 1.01f;
    invalid[3].pending.flags = preview_input_click_door;
    invalid[4].pending.flags = preview_input_click_target;
    invalid[4].pending.click_target.z = nan;
    invalid[5].mouse_sample_seconds = std::numeric_limits<double>::min();
    for (const auto& bad : invalid) {
        std::array inputs{valid, bad};
        std::array<PreviewInputSample, 2> outputs{};
        outputs[1].flags = preview_input_cancel;
        EXPECT_EQ(translate_pc_input_samples(inputs, outputs), PreviewStatus::invalid_input);
        for (const auto& output : outputs) {
            EXPECT_EQ(output.move_axis, glm::vec2{});
            EXPECT_EQ(output.flags, preview_input_none);
        }
    }
    std::array<PreviewInputSample, 2> outputs{};
    outputs[0].flags = preview_input_cancel;
    EXPECT_EQ(translate_pc_input_samples({&valid, 1}, outputs), PreviewStatus::invalid_input);
    EXPECT_EQ(outputs[0].flags, preview_input_none);
    EXPECT_EQ(translate_pc_input_samples({}, {}), PreviewStatus::ok);
}

TEST(ClientPcInput, LatestPointerAndWheelEdgesSurviveZeroTicksAndEmitOnce)
{
    PcDeviceSample input;
    input.keys[static_cast<size_t>(PcKey::w)] = true;
    ASSERT_TRUE(set_preview_click_door(input.pending, 2, {0, 0, 0}, {1, 1, 1}));
    ASSERT_TRUE(set_preview_click_target(input.pending, {3, 4, 5}));
    input.pending.flags |= preview_input_cancel;
    input.wheel_zoom = 2;
    PreviewInputSample sample;
    ASSERT_EQ(translate_pc_input_samples({&input, 1}, {&sample, 1}), PreviewStatus::ok);
    PreviewFixedStepState fixed;
    std::array<PreviewInputSample, 6> ticks{};
    EXPECT_EQ(build_preview_tick_samples(fixed, 0.001, sample, ticks).tick_count, 0u);
    ASSERT_EQ(translate_pc_input_samples({&input, 1}, {&sample, 1}), PreviewStatus::ok);
    ASSERT_EQ(build_preview_tick_samples(fixed, 0.05, sample, ticks).tick_count, 3u);
    EXPECT_EQ(ticks[0].flags, preview_input_click_target | preview_input_cancel);
    EXPECT_EQ(ticks[0].click_target, (glm::vec3{3, 4, 5}));
    EXPECT_EQ(ticks[0].zoom_delta, 2);
    for (size_t index = 1; index < 3; ++index) {
        EXPECT_EQ(ticks[index].flags, preview_input_none);
        EXPECT_EQ(ticks[index].zoom_delta, 0);
        EXPECT_EQ(ticks[index].move_axis, (glm::vec2{0, 1}));
    }
}

TEST(ClientPcInput, PointerActionsUseDoorIntentOrNavigationAndReplaceTheLastClick)
{
    const std::array inputs{
        PcPointerAction{.door_bounds_min = {0, 0, 0}, .door_bounds_max = {1, 1, 1}, .navigation_position = {3, 4, 5}, .door_index = 2, .door_interactable = true, .navigation_projected = true},
        PcPointerAction{.navigation_position = {3, 4, 5}, .door_index = 2, .navigation_projected = true},
        PcPointerAction{},
    };
    std::array<PreviewInputSample, inputs.size()> pending{};
    for (auto& sample : pending) {
        ASSERT_TRUE(set_preview_click_target(sample, {9, 9, 9}));
        sample.flags |= preview_input_cancel;
    }
    ASSERT_EQ(apply_pc_pointer_actions(inputs, pending), PreviewStatus::ok);
    EXPECT_EQ(pending[0].flags, preview_input_click_door | preview_input_cancel);
    EXPECT_EQ(pending[0].door_index, 2u);
    EXPECT_EQ(pending[1].flags, preview_input_click_target | preview_input_cancel);
    EXPECT_EQ(pending[1].click_target, (glm::vec3{3, 4, 5}));
    EXPECT_EQ(pending[1].door_index, UINT32_MAX);
    EXPECT_EQ(pending[2].flags, preview_input_cancel);
    auto invalid = inputs;
    invalid[0].door_bounds_min.x = 2;
    EXPECT_EQ(apply_pc_pointer_actions(invalid, pending), PreviewStatus::invalid_input);
    for (const auto& sample : pending) {
        EXPECT_EQ(sample.flags, preview_input_cancel);
    }
    EXPECT_EQ(apply_pc_pointer_actions(inputs, {}), PreviewStatus::invalid_input);
}

TEST(ClientPcInput, ControllerButtonBatchesRetainPendingFieldsAndRejectMalformedEdges)
{
    const std::array edges{
        PcControllerButtonEdge{PcControllerButton::east, true, true},
        PcControllerButtonEdge{PcControllerButton::back, true, true},
        PcControllerButtonEdge{PcControllerButton::east, false, true},
        PcControllerButtonEdge{PcControllerButton::east, true, false},
        PcControllerButtonEdge{PcControllerButton::other, true, true}};
    std::array<PreviewInputSample, edges.size()> pending{};
    for (auto& sample : pending) {
        sample.move_axis = {0.5f, -0.5f};
        ASSERT_TRUE(set_preview_click_target(sample, {3, 4, 5}));
    }
    ASSERT_EQ(apply_pc_controller_button_edges(edges, pending), PreviewStatus::ok);
    for (size_t i = 0; i < pending.size(); ++i) {
        EXPECT_EQ(pending[i].flags, preview_input_click_target | (i < 2 ? static_cast<uint32_t>(preview_input_cancel) : 0u));
        EXPECT_EQ(pending[i].move_axis, (glm::vec2{0.5f, -0.5f}));
        EXPECT_EQ(pending[i].click_target, (glm::vec3{3, 4, 5}));
    }
    auto invalid = edges;
    invalid[2].button = static_cast<PcControllerButton>(255);
    EXPECT_EQ(apply_pc_controller_button_edges(invalid, pending), PreviewStatus::invalid_input);
    for (const auto& sample : pending) {
        EXPECT_EQ(sample.flags, preview_input_click_target);
    }
    ASSERT_EQ(apply_pc_controller_button_edges(edges, pending), PreviewStatus::ok);
    EXPECT_EQ(apply_pc_controller_button_edges(std::span{edges}.first(1), pending), PreviewStatus::invalid_input);
    for (const auto& sample : pending) {
        EXPECT_EQ(sample.flags, preview_input_click_target);
    }
    EXPECT_EQ(apply_pc_controller_button_edges({}, {}), PreviewStatus::ok);
    RecordProperty("controller_edge_bytes", sizeof(PcControllerButtonEdge));
}

TEST(ClientInputRoutes, EqualBatchesResolveEachOwnerAndRejectStaleWorldWithoutEditorFallback)
{
    ClientInputFacts base{.category = ClientInputCategory::pointer, .edge = ClientInputEdge::down, .map = ClientInputMap::editor, .target = ClientInputTarget::world, .world_available = true};
    std::array<ClientInputFacts, 8> inputs;
    inputs.fill(base);
    inputs[1].map = ClientInputMap::pc;
    inputs[2].map = ClientInputMap::pc;
    inputs[2].world_available = false;
    inputs[3].target = ClientInputTarget::command;
    inputs[4].pointer_owner = ClientPointerOwner::toolset;
    inputs[5].category = ClientInputCategory::key;
    inputs[5].lifecycle_key = true;
    inputs[6].command_modal = true;
    inputs[7].target = ClientInputTarget::toolset;
    inputs[7].edge = ClientInputEdge::up;
    inputs[7].release_before_native = true;
    std::array<ClientInputRoute, inputs.size()> outputs{};
    ASSERT_TRUE(resolve_client_input_routes(inputs, outputs));
    EXPECT_EQ(outputs[0].native, ClientNativeRecipient::editor);
    EXPECT_EQ(outputs[1].native, ClientNativeRecipient::pc);
    EXPECT_EQ(outputs[2].native, ClientNativeRecipient::none);
    EXPECT_EQ(outputs[2].disposition, ClientInputDisposition::unavailable_world);
    EXPECT_EQ(outputs[3].native, ClientNativeRecipient::ui);
    EXPECT_EQ(outputs[3].rml, ClientRmlRecipient::command);
    EXPECT_EQ(outputs[4].native, ClientNativeRecipient::ui);
    EXPECT_EQ(outputs[5].native, ClientNativeRecipient::lifecycle);
    EXPECT_EQ(outputs[6].native, ClientNativeRecipient::ui);
    EXPECT_EQ(outputs[6].rml, ClientRmlRecipient::command);
    EXPECT_EQ(outputs[7].phase, ClientRmlForwardPhase::before_native);
    for (size_t i = 0; i < outputs.size() - 1; ++i) {
        EXPECT_EQ(outputs[i].phase, ClientRmlForwardPhase::after_native);
    }
    RecordProperty("input_row_bytes", sizeof(ClientInputFacts));
    RecordProperty("route_row_bytes", sizeof(ClientInputRoute));
}

TEST(ClientInputRoutes, CapturedWorldPointersKeepTheirOwnerAndBlockedWorldCannotAct)
{
    for (const auto map : {ClientInputMap::editor, ClientInputMap::pc}) {
        const auto native = map == ClientInputMap::editor ? ClientNativeRecipient::editor : ClientNativeRecipient::pc;
        const auto owner = map == ClientInputMap::editor ? ClientPointerOwner::editor : ClientPointerOwner::pc;
        for (const auto target : {ClientInputTarget::toolset, ClientInputTarget::command}) {
            std::array<ClientInputFacts, 4> inputs;
            inputs.fill({.category = ClientInputCategory::pointer, .edge = ClientInputEdge::motion, .map = map, .target = target, .pointer_owner = owner, .world_available = true});
            inputs[1].edge = ClientInputEdge::up;
            inputs[2].command_modal = true;
            inputs[3].category = ClientInputCategory::key;
            inputs[3].edge = ClientInputEdge::down;
            std::array<ClientInputRoute, inputs.size()> outputs{};
            ASSERT_TRUE(resolve_client_input_routes(inputs, outputs));
            EXPECT_EQ(outputs[0].native, native);
            EXPECT_EQ(outputs[1].native, native);
            EXPECT_EQ(outputs[2].native, ClientNativeRecipient::ui);
            EXPECT_EQ(outputs[2].rml, ClientRmlRecipient::command);
            EXPECT_EQ(outputs[3].native, ClientNativeRecipient::ui);
            if (map == ClientInputMap::pc) {
                EXPECT_TRUE(outputs[0].sources.pointer);
                EXPECT_FALSE(outputs[2].sources.pointer);
                inputs[0].world_available = false;
                ASSERT_TRUE(resolve_client_input_routes(inputs, outputs));
                EXPECT_EQ(outputs[0].native, ClientNativeRecipient::none);
                EXPECT_EQ(outputs[0].disposition, ClientInputDisposition::unavailable_world);
            }
        }
        std::array<ClientInputFacts, 3> blocked;
        blocked.fill({.category = ClientInputCategory::pointer, .edge = ClientInputEdge::wheel, .map = map, .target = ClientInputTarget::world, .world_available = true, .world_input_blocked = true});
        blocked[1].category = ClientInputCategory::key;
        blocked[1].edge = ClientInputEdge::down;
        blocked[2].target = ClientInputTarget::toolset;
        std::array<ClientInputRoute, blocked.size()> outputs{};
        ASSERT_TRUE(resolve_client_input_routes(blocked, outputs));
        EXPECT_EQ(outputs[0].native, ClientNativeRecipient::none);
        EXPECT_EQ(outputs[1].native, ClientNativeRecipient::none);
        EXPECT_EQ(outputs[0].disposition, ClientInputDisposition::unavailable_world);
        EXPECT_EQ(outputs[1].disposition, ClientInputDisposition::unavailable_world);
        EXPECT_EQ(outputs[2].native, ClientNativeRecipient::ui);
        for (const auto& output : outputs) {
            EXPECT_FALSE(output.sources.pointer);
            EXPECT_FALSE(output.sources.keyboard);
            EXPECT_FALSE(output.sources.controller);
        }
    }
}

TEST(ClientInputRoutes, MapChangesRejectThePreviousWorldPointerOwner)
{
    const std::array inputs{
        ClientInputFacts{.category = ClientInputCategory::pointer, .edge = ClientInputEdge::motion, .map = ClientInputMap::editor, .pointer_owner = ClientPointerOwner::pc, .world_available = true},
        ClientInputFacts{.category = ClientInputCategory::pointer, .edge = ClientInputEdge::motion, .map = ClientInputMap::pc, .pointer_owner = ClientPointerOwner::editor, .world_available = true},
    };
    std::array<ClientInputRoute, inputs.size()> outputs{};
    ASSERT_TRUE(resolve_client_input_routes(inputs, outputs));
    for (const auto& output : outputs) {
        EXPECT_EQ(output.disposition, ClientInputDisposition::invalid_input);
        EXPECT_EQ(output.native, ClientNativeRecipient::none);
        EXPECT_EQ(output.rml, ClientRmlRecipient::none);
        EXPECT_FALSE(output.sources.pointer);
    }
}

TEST(ClientInputRoutes, TextFocusClaimsKeyboardAndKeepsEligibleControllerEdgesInThePcMap)
{
    for (const auto focus : {ClientInputFocus::toolset_text, ClientInputFocus::command_text}) {
        std::array<ClientInputFacts, 3> inputs;
        inputs.fill({.category = ClientInputCategory::gamepad, .edge = ClientInputEdge::down, .map = ClientInputMap::pc, .focus = focus, .world_available = true});
        inputs[1].category = ClientInputCategory::key;
        inputs[2].command_modal = true;
        std::array<ClientInputRoute, inputs.size()> outputs{};
        ASSERT_TRUE(resolve_client_input_routes(inputs, outputs));
        EXPECT_EQ(outputs[0].native, ClientNativeRecipient::pc);
        EXPECT_TRUE(outputs[0].sources.controller);
        EXPECT_FALSE(outputs[0].sources.keyboard);
        EXPECT_EQ(outputs[1].native, ClientNativeRecipient::ui);
        EXPECT_EQ(outputs[2].native, ClientNativeRecipient::ui);
        EXPECT_FALSE(outputs[2].sources.controller);
    }
}

TEST(ClientInputRoutes, InvalidTagsEdgesAndMismatchedSpansClearPreviousActions)
{
    ClientInputFacts base{.category = ClientInputCategory::key, .edge = ClientInputEdge::down, .map = ClientInputMap::pc, .world_available = true};
    std::array<ClientInputFacts, 9> inputs;
    inputs.fill(base);
    inputs[0].map = ClientInputMap::invalid;
    inputs[1].category = static_cast<ClientInputCategory>(255);
    inputs[2].target = static_cast<ClientInputTarget>(255);
    inputs[3].focus = static_cast<ClientInputFocus>(255);
    inputs[4].pointer_owner = static_cast<ClientPointerOwner>(255);
    inputs[5].edge = ClientInputEdge::wheel;
    inputs[6].release_before_native = true;
    inputs[7].pointer_valid = false;
    inputs[8].pointer_owner = ClientPointerOwner::editor;
    std::array<ClientInputRoute, inputs.size()> outputs;
    for (auto& output : outputs) {
        output.native = ClientNativeRecipient::editor;
        output.rml = ClientRmlRecipient::toolset;
        output.sources = {};
    }
    ASSERT_TRUE(resolve_client_input_routes(inputs, outputs));
    const auto rejected = [](const ClientInputRoute& output) {
        EXPECT_EQ(output.disposition, ClientInputDisposition::invalid_input);
        EXPECT_EQ(output.native, ClientNativeRecipient::none);
        EXPECT_EQ(output.rml, ClientRmlRecipient::none);
        EXPECT_EQ(output.phase, ClientRmlForwardPhase::none);
        EXPECT_FALSE(output.sources.keyboard);
        EXPECT_FALSE(output.sources.controller);
        EXPECT_FALSE(output.sources.pointer);
    };
    for (const auto& output : outputs) {
        rejected(output);
    }
    inputs.fill(base);
    ASSERT_TRUE(resolve_client_input_routes(inputs, outputs));
    ASSERT_EQ(outputs.front().native, ClientNativeRecipient::pc);
    EXPECT_FALSE(resolve_client_input_routes(std::span{inputs}.first(1), outputs));
    for (const auto& output : outputs) {
        rejected(output);
    }
    EXPECT_TRUE(resolve_client_input_routes({}, {}));
}

TEST(ClientInputRoutes, RolesUseOnePcMapAndUiClaimsOnlyItsSources)
{
    EXPECT_EQ(client_input_map(ClientControlRole::editor, false), ClientInputMap::editor);
    EXPECT_EQ(client_input_map(ClientControlRole::editor, true), ClientInputMap::pc);
    EXPECT_EQ(client_input_map(static_cast<ClientControlRole>(255), true), ClientInputMap::invalid);
    for (const auto role : {ClientControlRole::player, ClientControlRole::dm}) {
        ClientInputFacts input{.category = ClientInputCategory::held, .map = client_input_map(role, false), .target = ClientInputTarget::world, .world_available = true};
        std::array<ClientInputRoute, 1> output{};
        const auto resolve = [&] { EXPECT_TRUE(resolve_client_input_routes({&input, 1}, output)); };
        resolve();
        EXPECT_EQ(output[0].native, ClientNativeRecipient::pc);
        EXPECT_TRUE(output[0].sources.keyboard);
        EXPECT_TRUE(output[0].sources.controller);
        EXPECT_TRUE(output[0].sources.pointer);
        input.focus = ClientInputFocus::toolset_text;
        resolve();
        EXPECT_FALSE(output[0].sources.keyboard);
        EXPECT_TRUE(output[0].sources.controller);
        EXPECT_TRUE(output[0].sources.pointer);
        input.pointer_owner = ClientPointerOwner::toolset;
        resolve();
        EXPECT_FALSE(output[0].sources.pointer);
        EXPECT_TRUE(output[0].sources.controller);
        input.command_modal = true;
        resolve();
        EXPECT_FALSE(output[0].sources.keyboard);
        EXPECT_FALSE(output[0].sources.controller);
        EXPECT_FALSE(output[0].sources.pointer);
    }
}
