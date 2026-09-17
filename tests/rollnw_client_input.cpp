#include "client_input.hpp"
#include "client_ui_action.hpp"
#include "command_view.hpp"
#include "object_workbench_view.hpp"
#include "runtime_input.hpp"
#include "workspace.hpp"

#include <RmlUi/Core.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace nw::toolset;

namespace {

class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
    {
        return 1;
    }

    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override { }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override { }

    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override { }

    void EnableScissorRegion(bool) override { }
    void SetScissorRegion(Rml::Rectanglei) override { }
};

class InputEventRecorder final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override
    {
        events.push_back(event.GetType());
        if (on_release && event.GetType() == "mouseup") {
            auto callback = std::exchange(on_release, std::function<void()>{});
            callback();
        }
        if (consume_keys && event.GetType() == "keydown") { event.StopPropagation(); }
        if ((replace_on_release && event.GetType() == "mouseup")
            || (replace_on_blur && event.GetType() == "blur")) {
            replace_on_release = false;
            replace_on_blur = false;
            ++replacements;
            document->GetElementById("workspace_content")->SetInnerRML("<button id='new_target' style='position:absolute;left:160px;top:20px;width:80px;height:40px;'>New</button>");
        }
    }
    Rml::ElementDocument* document = nullptr;
    std::vector<std::string> events;
    int replacements = 0;
    bool consume_keys = false;
    bool replace_on_release = false;
    bool replace_on_blur = false;
    std::function<void()> on_release;
};

} // namespace

class ClientInput : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (const char* hint = SDL_GetHint(SDL_HINT_VIDEO_DRIVER)) { previous_video_hint = hint; }
        if (SDL_WasInit(SDL_INIT_VIDEO) && std::string_view{SDL_GetCurrentVideoDriver()} != "dummy") {
            GTEST_SKIP() << "An existing SDL video subsystem uses another driver";
        }
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { GTEST_SKIP() << "Dummy SDL video unavailable: " << SDL_GetError(); }
        video_started = true;
        ASSERT_STREQ(SDL_GetCurrentVideoDriver(), "dummy");
        window = SDL_CreateWindow("client input fixture", 1200, 700, SDL_WINDOW_HIDDEN);
        if (!window) { GTEST_SKIP() << "Dummy SDL window unavailable: " << SDL_GetError(); }
        ASSERT_FLOAT_EQ(SDL_GetWindowPixelDensity(window), 1);
        Rml::SetRenderInterface(&renderer);
        rml_initialized = Rml::Initialise();
        ASSERT_TRUE(rml_initialized);
        std::ifstream input{std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "tools/client/assets/fonts/inter/Inter-Medium.ttf", std::ios::binary};
        font = {std::istreambuf_iterator<char>{input}, {}};
        ASSERT_FALSE(font.empty());
        ASSERT_TRUE(Rml::LoadFontFace({font.data(), font.size()}, "RollnwSans", Rml::Style::FontStyle::Normal,
            static_cast<Rml::Style::FontWeight>(500)));
        context = Rml::CreateContext("ordered-client-input", {1200, 700});
        ASSERT_NE(context, nullptr);
        const auto panel = std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "tools/client/ui/panel.rml";
        document = context->LoadDocument(panel.string());
        ASSERT_NE(document, nullptr);
        document->GetElementById("workspace_content")->SetInnerRML("<input id='variable' class='object_variable_value' type='text' style='position:absolute;left:20px;top:20px;width:80px;height:40px;'/>"
                                                                   "<button id='old_target' style='position:absolute;left:160px;top:20px;width:80px;height:40px;'>Old</button>");
        recorder.document = document;
        context->AddEventListener("mouseup", &recorder, false);
        context->AddEventListener("click", &recorder, false);
        context->AddEventListener("keydown", &recorder, false);
        context->AddEventListener("blur", &recorder, true);
        document->Show();
        context->Update();
    }

    void TearDown() override
    {
        if (command_context) { Rml::RemoveContext("ordered-client-command"); }
        if (context) {
            context->RemoveEventListener("mouseup", &recorder, false);
            context->RemoveEventListener("click", &recorder, false);
            context->RemoveEventListener("keydown", &recorder, false);
            context->RemoveEventListener("blur", &recorder, true);
            Rml::RemoveContext("ordered-client-input");
        }
        if (rml_initialized) { Rml::Shutdown(); }
        Rml::SetRenderInterface(nullptr);
        if (window) { SDL_DestroyWindow(window); }
        if (video_started) { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
        if (previous_video_hint) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, previous_video_hint->c_str());
        } else {
            SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        }
    }

    Rml::Vector2f target_point() const
    {
        auto* target = document->GetElementById("old_target");
        return {target->GetAbsoluteLeft() + 10, target->GetAbsoluteTop() + 10};
    }

    void press_target()
    {
        const auto point = target_point();
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.x = point.x;
        motion.motion.y = point.y;
        ClientInputDispatchState motion_dispatch;
        ASSERT_TRUE(forward_client_input(motion_dispatch, ClientRmlRecipient::toolset,
            ClientRmlForwardPhase::after_native, context, window, motion)
                .performed);
        SDL_Event down{};
        down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        down.button.button = SDL_BUTTON_LEFT;
        down.button.x = point.x;
        down.button.y = point.y;
        ClientInputDispatchState down_dispatch;
        ASSERT_TRUE(forward_client_input(down_dispatch, ClientRmlRecipient::toolset,
            ClientRmlForwardPhase::after_native, context, window, down)
                .performed);
    }

    std::optional<std::string> previous_video_hint;
    bool video_started = false;
    bool rml_initialized = false;
    SDL_Window* window = nullptr;
    NullRenderInterface renderer;
    std::vector<Rml::byte> font;
    Rml::Context* context = nullptr;
    Rml::Context* command_context = nullptr;
    Rml::ElementDocument* document = nullptr;
    InputEventRecorder recorder;
};

TEST(ClientInputValidation, RejectsInvalidPointerCoordinatesBeforeHitOrForwarding)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    EXPECT_TRUE(valid_client_pointer_event(event));
    event.motion.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(valid_client_pointer_event(event));
    event.motion.x = 0;
    event.motion.yrel = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(valid_client_pointer_event(event));
    event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.x = std::numeric_limits<float>::max();
    EXPECT_FALSE(valid_client_pointer_event(event));
    event = {};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(valid_client_pointer_event(event));
    event = {};
    event.type = SDL_EVENT_FINGER_MOTION;
    event.tfinger.x = 1.1f;
    EXPECT_FALSE(valid_client_pointer_event(event));
    event.type = SDL_EVENT_KEY_DOWN;
    EXPECT_TRUE(valid_client_pointer_event(event));
}

TEST_F(ClientInput, ForwardingPreservesPropagationAndIsIndependentOfNativeHandling)
{
    ASSERT_TRUE(document->GetElementById("old_target")->Focus());
    recorder.consume_keys = true;
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_F1;
    ClientInputDispatchState dispatch;
    EXPECT_TRUE(client_input_forwarding_pending(dispatch));
    const auto result = forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event);
    EXPECT_TRUE(result.performed);
    EXPECT_FALSE(result.propagating);
    EXPECT_FALSE(dispatch.native_handled);
    EXPECT_FALSE(client_input_forwarding_pending(dispatch));
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::command,
        ClientRmlForwardPhase::after_native, context, window, event)
            .performed);
    EXPECT_EQ(recorder.events, (std::vector<std::string>{"keydown"}));
    ClientInputDispatchState native{.native_handled = true};
    EXPECT_FALSE(client_input_forwarding_pending(native));
    EXPECT_FALSE(forward_client_input(native, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event)
            .performed);
    EXPECT_EQ(recorder.events.size(), 1u);
}

TEST_F(ClientInput, EarlyReleasePrecedesDomReplacementAndCannotForwardAgain)
{
    const auto point = target_point();
    const auto converted = to_context_point(window, point.x, point.y);
    EXPECT_EQ(converted, point);
    press_target();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    recorder.events.clear();
    recorder.replace_on_release = true;
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = point.x;
    event.button.y = point.y;
    ClientInputDispatchState dispatch;
    EXPECT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::before_native, context, window, event)
            .performed);
    EXPECT_EQ(dispatch.forwarding_phase, ClientRmlForwardPhase::before_native);
    EXPECT_EQ(recorder.events, (std::vector<std::string>{"mouseup", "blur"}));
    EXPECT_EQ(recorder.replacements, 1);
    recorder.events.emplace_back("native");
    context->Update();
    ASSERT_NE(document->GetElementById("new_target"), nullptr);
    EXPECT_EQ(document->GetElementById("old_target"), nullptr);
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event)
            .performed);
    EXPECT_EQ(recorder.events, (std::vector<std::string>{"mouseup", "blur", "native"}));
}

TEST_F(ClientInput, NativeRowKeySurvivesReleaseReplacingItsDom)
{
    WorkspaceState workspace;
    workspace.open_tab("resource");
    ASSERT_TRUE(workspace.set_active_tab("resource"));
    const ClientUiActionContext facts{.map = ClientInputMap::editor};
    const auto owner = capture_client_ui_action_owner(workspace, facts);
    auto* row = document->GetElementById("old_target");
    ASSERT_NE(row, nullptr);
    row->SetAttribute("data-key", "17");
    const auto point = target_point();
    press_target();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    recorder.events.clear();
    recorder.replace_on_release = true;
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = point.x;
    event.button.y = point.y;
    ClientInputDispatchState dispatch;
    const auto key = release_client_row_key(dispatch, row, context, window, event);
    ASSERT_TRUE(key);
    EXPECT_EQ(*key, 17);
    EXPECT_EQ(recorder.replacements, 1);
    EXPECT_EQ(document->GetElementById("old_target"), nullptr);
    EXPECT_NE(document->GetElementById("new_target"), nullptr);
    EXPECT_TRUE(same_client_ui_action_owner(owner, capture_client_ui_action_owner(workspace, facts)));
    EXPECT_EQ(dispatch.forwarding_phase, ClientRmlForwardPhase::before_native);
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event)
            .performed);
    EXPECT_FALSE(release_client_row_key(dispatch, row, context, window, event));
}

TEST_F(ClientInput, ReleaseCallbackChangingWorkspaceRejectsTheCapturedOwner)
{
    WorkspaceState workspace;
    workspace.open_tab("first", "First", WorkspaceTabKind::resource);
    workspace.find_tab("first")->detail = "objects/Agent.utc";
    workspace.open_tab("second", "Second", WorkspaceTabKind::resource);
    workspace.find_tab("second")->detail = "objects/Ranger.utc";
    ASSERT_TRUE(workspace.set_active_tab("first"));
    const ClientUiActionContext facts{.map = ClientInputMap::editor};
    const auto before = capture_client_ui_action_owner(workspace, facts);
    auto* row = document->GetElementById("old_target");
    ASSERT_NE(row, nullptr);
    row->SetAttribute("data-key", "7");
    const auto point = target_point();
    press_target();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    recorder.events.clear();
    recorder.on_release = [&] { EXPECT_TRUE(workspace.set_active_tab("second")); };
    recorder.replace_on_release = true;
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = point.x;
    event.button.y = point.y;
    ClientInputDispatchState dispatch;
    const auto key = release_client_row_key(dispatch, row, context, window, event);
    ASSERT_TRUE(key);
    EXPECT_EQ(*key, 7);
    EXPECT_FALSE(same_client_ui_action_owner(before, capture_client_ui_action_owner(workspace, facts)));
    EXPECT_EQ(workspace.active_tab_id(), "second");
    EXPECT_EQ(before.text, "firstobjects/Agent.utc");
    EXPECT_EQ(recorder.replacements, 1);
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event)
            .performed);
    EXPECT_EQ(std::count(recorder.events.begin(), recorder.events.end(), "mouseup"), 1);
}

TEST_F(ClientInput, RejectedIntegerControlReleasesItsSdkPressWithoutANativeAction)
{
    WorkspaceState workspace;
    workspace.open_tab("resource");
    ASSERT_TRUE(workspace.set_active_tab("resource"));
    ObjectWorkbenchViewState view;
    auto* target = document->GetElementById("old_target");
    ASSERT_NE(target, nullptr);
    target->SetClass("object_details_integer_step", true);
    target->SetAttribute("data-row", "2147483648");
    target->SetAttribute("data-current", "0");
    target->SetAttribute("data-delta", "1");
    context->Update();
    const auto point = target_point();
    press_target();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(target->IsPseudoClassSet("active"));
    recorder.events.clear();
    const auto click = capture_object_workbench_command_click(target, view, workspace, 0);
    ASSERT_TRUE(click);
    EXPECT_EQ(click->kind, ObjectWorkbenchCommandKind::none);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = point.x;
    event.button.y = point.y;
    ClientInputDispatchState dispatch;
    if (click->release_phase == ClientRmlForwardPhase::before_native) {
        (void)forward_client_input(dispatch, ClientRmlRecipient::toolset,
            click->release_phase, context, window, event);
    }
    dispatch.native_handled = true;
    EXPECT_FALSE(client_input_forwarding_pending(dispatch));
    EXPECT_EQ(dispatch.forwarded_recipient, ClientRmlRecipient::toolset);
    EXPECT_EQ(std::count(recorder.events.begin(), recorder.events.end(), "mouseup"), 1);
    EXPECT_FALSE(target->IsPseudoClassSet("active"));
    EXPECT_EQ(workspace.undo_count(), 0);
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event)
            .performed);
}

TEST_F(ClientInput, RejectedCreatureAndInventoryControlsReleaseTheirSdkPress)
{
    WorkspaceState workspace;
    workspace.open_tab("resource");
    ASSERT_TRUE(workspace.set_active_tab("resource"));
    CreatureWorkbenchViewState creature;
    InventoryWorkbenchViewState inventory;
    for (const bool creature_control : {true, false}) {
        SCOPED_TRACE(creature_control);
        auto* target = document->GetElementById("old_target");
        ASSERT_NE(target, nullptr);
        target->SetClass("creature_spell_increment", creature_control);
        target->SetClass("creature_inventory_item", !creature_control);
        target->SetAttribute("data-spell", "-1");
        target->SetAttribute("data-key", "2147483648");
        context->Update();
        const auto point = target_point();
        press_target();
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        ASSERT_TRUE(target->IsPseudoClassSet("active"));
        recorder.events.clear();
        ClientRmlForwardPhase phase;
        if (creature_control) {
            const auto click = capture_creature_workbench_command_click(target, creature, {}, workspace, 0);
            ASSERT_TRUE(click);
            EXPECT_EQ(click->kind, CreatureWorkbenchCommandKind::none);
            phase = click->release_phase;
        } else {
            const auto click = capture_inventory_workbench_click(target, inventory, {}, workspace, 0);
            ASSERT_TRUE(click);
            EXPECT_EQ(click->kind, InventoryWorkbenchClickKind::none);
            phase = click->release_phase;
        }
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = point.x;
        event.button.y = point.y;
        ClientInputDispatchState dispatch;
        if (phase == ClientRmlForwardPhase::before_native) {
            (void)forward_client_input(dispatch, ClientRmlRecipient::toolset, phase, context, window, event);
        }
        dispatch.native_handled = true;
        EXPECT_FALSE(client_input_forwarding_pending(dispatch));
        EXPECT_EQ(std::count(recorder.events.begin(), recorder.events.end(), "mouseup"), 1);
        EXPECT_FALSE(target->IsPseudoClassSet("active"));
        EXPECT_EQ(workspace.undo_count(), 0);
        EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
            ClientRmlForwardPhase::after_native, context, window, event)
                .performed);
        cancel_client_pointer_interactions(context, nullptr, event);
    }
}

TEST_F(ClientInput, MalformedNativeRowKeysReleaseOnceWithoutAnAction)
{
    for (const char* key : {"", "17suffix", "2147483648", "-2147483649"}) {
        SCOPED_TRACE(key);
        auto* row = document->GetElementById("old_target");
        ASSERT_NE(row, nullptr);
        row->SetAttribute("data-key", key);
        const auto point = target_point();
        press_target();
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        recorder.events.clear();
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = point.x;
        event.button.y = point.y;
        ClientInputDispatchState dispatch;
        EXPECT_FALSE(release_client_row_key(dispatch, row, context, window, event));
        EXPECT_EQ(dispatch.forwarded_recipient, ClientRmlRecipient::toolset);
        EXPECT_EQ(std::count(recorder.events.begin(), recorder.events.end(), "mouseup"), 1);
        EXPECT_FALSE(release_client_row_key(dispatch, row, context, window, event));
        EXPECT_EQ(std::count(recorder.events.begin(), recorder.events.end(), "mouseup"), 1);
    }
}

TEST_F(ClientInput, BlurCallbackReplacesDomBeforeTheNextTargetIsAcquired)
{
    const auto point = target_point();
    recorder.replace_on_blur = true;
    ASSERT_TRUE(document->GetElementById("variable")->Focus());
    recorder.events.clear();
    blur_focused_object_variable_input(context, point);
    context->Update();
    SDL_MouseButtonEvent button{};
    button.x = point.x;
    button.y = point.y;
    auto* current = element_at_mouse(context, window, button);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->GetId(), "new_target");
    // Removing the focused input during blur makes Rml emit its removal blur too.
    EXPECT_EQ(recorder.events, (std::vector<std::string>{"blur", "blur"}));
    EXPECT_EQ(recorder.replacements, 1);
    EXPECT_EQ(document->GetElementById("variable"), nullptr);
}

TEST_F(ClientInput, RejectedReleaseCancelsTheOldPressWithoutActivatingItLater)
{
    const auto point = target_point();
    press_target();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    recorder.events.clear();
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = std::numeric_limits<float>::quiet_NaN();
    ClientInputDispatchState dispatch;
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event)
            .performed);
    cancel_client_pointer_interactions(context, nullptr, event);
    EXPECT_TRUE(recorder.events.empty());
    context->ProcessMouseMove(static_cast<int>(point.x), static_cast<int>(point.y), 0);
    event.button.x = point.x;
    event.button.y = point.y;
    EXPECT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event)
            .performed);
    EXPECT_EQ(recorder.events, (std::vector<std::string>{"mouseup"}));
}

TEST_F(ClientInput, VisiblePaletteRoutesThroughProductionCaptureResolveAndSdkOnce)
{
    command_context = Rml::CreateContext("ordered-client-command", {1200, 700});
    ASSERT_NE(command_context, nullptr);
    // The production in-memory document resolves panel.rcss through the app's
    // resource interface. This fixture uses the SDK filesystem implementation.
    const auto previous_path = std::filesystem::current_path();
    std::filesystem::current_path(std::filesystem::path{ROLLNW_TEST_SOURCE_DIR} / "tools/client/ui");
    auto* palette = load_command_palette_document(*command_context);
    std::filesystem::current_path(previous_path);
    ASSERT_NE(palette, nullptr);
    palette->Show();
    CommandViewState view;
    bool viewport_focused = false;
    set_command_palette_visibility(view, context, command_context, document, palette, viewport_focused, true);
    command_context->Update();
    auto* box = palette->GetElementById("command_palette");
    ASSERT_TRUE(box->IsVisible(true));
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = box->GetAbsoluteLeft() + 10;
    event.motion.y = box->GetAbsoluteTop() + 10;
    const ClientInputOwnership owner{.map = ClientInputMap::pc, .world_available = true};
    const auto resolve = [&](const ClientInputFacts& facts) {
        std::array<ClientInputRoute, 1> routes{};
        EXPECT_TRUE(resolve_client_input_routes({&facts, 1}, routes));
        return routes[0];
    };
    auto route = resolve(capture_client_input_facts(event, window, context, command_context, palette, nullptr, owner));
    EXPECT_EQ(route.native, ClientNativeRecipient::ui);
    EXPECT_EQ(route.rml, ClientRmlRecipient::command);
    EXPECT_FALSE(route.sources.keyboard);
    EXPECT_FALSE(route.sources.controller);
    EXPECT_FALSE(route.sources.pointer);
    const auto forward_route = resolve_client_forward_route(event, window, palette, owner.map);
    EXPECT_EQ(forward_route.rml, ClientRmlRecipient::command);
    ClientInputDispatchState dispatch;
    EXPECT_TRUE(forward_client_input(dispatch, forward_route.rml, forward_route.phase, command_context, window, event).performed);
    EXPECT_FALSE(forward_client_input(dispatch, forward_route.rml, forward_route.phase, command_context, window, event).performed);
    set_command_palette_visibility(view, context, command_context, document, palette, viewport_focused, false);
    command_context->Update();
    route = resolve(capture_client_input_facts(event, window, context, command_context, palette, nullptr, owner));
    EXPECT_EQ(route.rml, ClientRmlRecipient::toolset);
    EXPECT_EQ(resolve_client_forward_route(event, window, palette, owner.map).rml, ClientRmlRecipient::toolset);
    route = resolve(capture_client_held_input_facts(context, command_context, palette, nullptr, owner));
    EXPECT_TRUE(route.sources.keyboard);
    EXPECT_TRUE(route.sources.controller);
    EXPECT_TRUE(route.sources.pointer);
}

TEST_F(ClientInput, HeldEligibilityUsesFreshVisibleFocusAndCaptureForBothPcRoles)
{
    for (const auto role : {ClientControlRole::player, ClientControlRole::dm}) {
        ClientInputOwnership owner{.map = client_input_map(role, false), .world_available = true};
        const auto resolve = [&] {
            const std::array facts{capture_client_held_input_facts(context, nullptr, nullptr, nullptr, owner)};
            std::array<ClientInputRoute, 1> routes{};
            EXPECT_TRUE(resolve_client_input_routes(facts, routes));
            return routes[0];
        };
        auto* input = document->GetElementById("variable");
        ASSERT_NE(input, nullptr);
        input->SetProperty("display", "block");
        context->Update();
        input->Focus();
        auto route = resolve();
        EXPECT_FALSE(route.sources.keyboard);
        EXPECT_TRUE(route.sources.controller);
        EXPECT_TRUE(route.sources.pointer);
        input->SetProperty("display", "none");
        context->Update();
        route = resolve();
        EXPECT_TRUE(route.sources.keyboard);
        owner.pointer_owner = ClientPointerOwner::toolset;
        route = resolve();
        EXPECT_FALSE(route.sources.pointer);
        EXPECT_TRUE(route.sources.keyboard);
        EXPECT_TRUE(route.sources.controller);
        RuntimeInputState runtime;
        runtime.pending.flags = preview_input_click_target | preview_input_cancel;
        runtime.pending.click_target = {1, 2, 3};
        runtime.mouse_look_pixels = {40, 20};
        runtime.wheel_zoom = 1;
        PcDeviceSample device;
        ASSERT_EQ(acquire_pc_device_sample(runtime, 0.01, route.sources, device), PreviewStatus::ok);
        EXPECT_EQ(runtime.pending.flags, preview_input_cancel);
        EXPECT_EQ(device.pending.flags, preview_input_cancel);
        EXPECT_EQ(device.mouse_look_pixels, glm::vec2{});
        EXPECT_EQ(device.wheel_zoom, 0);
        owner.pointer_owner = ClientPointerOwner::pc;
        route = resolve();
        EXPECT_TRUE(route.sources.pointer);
        ASSERT_EQ(acquire_pc_device_sample(runtime, 0.01, route.sources, device), PreviewStatus::ok);
        EXPECT_EQ(device.pending.flags, preview_input_cancel);
        EXPECT_EQ(device.mouse_look_pixels, glm::vec2{});
        EXPECT_EQ(device.wheel_zoom, 0);
        owner.world_input_blocked = true;
        route = resolve();
        EXPECT_FALSE(route.sources.keyboard);
        EXPECT_FALSE(route.sources.controller);
        EXPECT_FALSE(route.sources.pointer);
        ASSERT_EQ(acquire_pc_device_sample(runtime, 0.01, route.sources, device), PreviewStatus::ok);
        EXPECT_EQ(runtime.pending.flags, preview_input_none);
        owner.world_input_blocked = false;
        owner.world_available = false;
        route = resolve();
        EXPECT_EQ(route.native, ClientNativeRecipient::none);
        EXPECT_EQ(route.disposition, ClientInputDisposition::unavailable_world);
        EXPECT_FALSE(route.sources.keyboard);
        EXPECT_FALSE(route.sources.controller);
        EXPECT_FALSE(route.sources.pointer);
    }
}
