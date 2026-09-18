#include "client_input.hpp"
#include "client_ui_action.hpp"
#include "command_view.hpp"
#include "editor_input.hpp"
#include "object_workbench_view.hpp"
#include "runtime_input.hpp"
#include "workspace.hpp"
#include "workspace_view.hpp"

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

TEST_F(ClientInput, ConsumedNativeUiReleaseStillCompletesItsSdkPressOnce)
{
    auto* target = document->GetElementById("old_target");
    ASSERT_NE(target, nullptr);
    target->SetClass("home_area_card", true);
    target->SetAttribute("data-key", "0tail");
    press_target();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(target->IsPseudoClassSet("active"));
    ASSERT_FALSE(client_row_key(target));
    recorder.events.clear();
    const auto point = target_point();
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = point.x;
    event.button.y = point.y;
    ClientInputDispatchState dispatch{.native_handled = true};
    EXPECT_FALSE(client_input_forwarding_pending(dispatch));
    EXPECT_TRUE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event).performed);
    EXPECT_TRUE(dispatch.native_handled);
    EXPECT_EQ(dispatch.forwarding_phase, ClientRmlForwardPhase::after_native);
    EXPECT_EQ(std::count(recorder.events.begin(), recorder.events.end(), "mouseup"), 1);
    EXPECT_FALSE(target->IsPseudoClassSet("active"));
    EXPECT_FALSE(forward_client_input(dispatch, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event).performed);
    EXPECT_EQ(std::count(recorder.events.begin(), recorder.events.end(), "mouseup"), 1);
    ClientInputDispatchState consumed_down{.native_handled = true};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    EXPECT_FALSE(forward_client_input(consumed_down, ClientRmlRecipient::toolset,
        ClientRmlForwardPhase::after_native, context, window, event).performed);
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
    for (const char* key : {"", "17suffix", "0tail", "+0", " 0", "0 ", "2147483648", "-2147483649"}) {
        SCOPED_TRACE(key);
        auto* row = document->GetElementById("old_target");
        ASSERT_NE(row, nullptr);
        row->SetAttribute("data-key", key);
        EXPECT_FALSE(client_row_key(row));
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

TEST_F(ClientInput, GeneratedViewportRoutesPointerInputToTheCurrentWorldMap)
{
    for (const auto kind : {WorkspaceTabKind::area, WorkspaceTabKind::preview}) {
        WorkspaceTab tab;
        tab.kind = kind;
        tab.detail = "shared/a<&\".json";
        std::string markup;
        append_workspace_viewport_markup(markup, tab);
        EXPECT_EQ(markup, "<div id=\"workspace_viewer_viewport\" class=\"workspace_viewer_viewport\" data-resource=\"shared/a&lt;&amp;&quot;.json\"></div>");
        document->GetElementById("workspace_content")->SetInnerRML("<div class='workspace_preview_body' style='position:absolute;left:100px;top:100px;width:500px;height:300px;'>" + markup + "</div>");
        context->Update();
        auto* viewport = document->GetElementById("workspace_viewer_viewport");
        ASSERT_NE(viewport, nullptr);
        ASSERT_TRUE(viewport->IsVisible(true));
        ASSERT_GT(viewport->GetOffsetWidth(), 40);
        ASSERT_GT(viewport->GetOffsetHeight(), 40);
        const Rml::Vector2f point{viewport->GetAbsoluteLeft() + 20, viewport->GetAbsoluteTop() + 20};
        for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL}) {
            SDL_Event event{};
            event.type = type;
            if (type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                event.button.button = SDL_BUTTON_LEFT;
                event.button.x = point.x;
                event.button.y = point.y;
            } else if (type == SDL_EVENT_MOUSE_MOTION) {
                event.motion.x = point.x;
                event.motion.y = point.y;
            } else {
                event.wheel.mouse_x = point.x;
                event.wheel.mouse_y = point.y;
                event.wheel.y = 1;
            }
            for (const auto role : {ClientControlRole::editor, ClientControlRole::player, ClientControlRole::dm}) {
                const auto map = client_input_map(role, false);
                const std::array facts{capture_client_input_facts(event, window, context, nullptr, nullptr, nullptr,
                    {.map = map, .world_available = true})};
                EXPECT_EQ(facts[0].target, ClientInputTarget::world);
                const auto route = resolve_client_event_input_route(event, window, context, nullptr, nullptr, nullptr,
                    {.map = map, .world_available = true});
                EXPECT_EQ(route.native, map == ClientInputMap::editor ? ClientNativeRecipient::editor : ClientNativeRecipient::pc);
                if (map == ClientInputMap::pc) {
                    EXPECT_TRUE(route.sources.pointer);
                    const auto unavailable = resolve_client_event_input_route(event, window, context, nullptr, nullptr, nullptr,
                        {.map = map});
                    EXPECT_EQ(unavailable.native, ClientNativeRecipient::none);
                    EXPECT_EQ(unavailable.disposition, ClientInputDisposition::unavailable_world);
                }
            }
        }
        tab.detail.clear();
        std::string empty;
        append_workspace_viewport_markup(empty, tab);
        EXPECT_EQ(empty, std::string{"<div id=\"workspace_viewer_viewport\" class=\"workspace_viewer_viewport empty\" data-resource=\"\"><div class=\"workspace_area_placeholder\">"} + (kind == WorkspaceTabKind::area ? "Open an area from the project tree." : "Open a previewable blueprint from the project tree.") + "</div></div>");
        viewport->SetProperty("display", "none");
        context->Update();
        SDL_Event hidden{};
        hidden.type = SDL_EVENT_MOUSE_MOTION;
        hidden.motion.x = point.x;
        hidden.motion.y = point.y;
        const auto facts = capture_client_input_facts(hidden, window, context, nullptr, nullptr, nullptr,
            {.map = ClientInputMap::pc, .world_available = true});
        EXPECT_NE(facts.target, ClientInputTarget::world);
    }
    WorkspaceTab other;
    std::string markup = "prefix";
    append_workspace_viewport_markup(markup, other);
    EXPECT_EQ(markup, "prefix");
}

TEST_F(ClientInput, EventAdapterUsesFreshUiAndTheCapturedWorldOwner)
{
    auto* target = document->GetElementById("old_target");
    ASSERT_NE(target, nullptr);
    const auto point = target_point();
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = point.x;
    event.motion.y = point.y;
    for (const auto role : {ClientControlRole::editor, ClientControlRole::player, ClientControlRole::dm}) {
        const auto map = client_input_map(role, role == ClientControlRole::editor);
        ClientInputOwnership ownership{.map = map, .world_available = true};
        const auto resolve = [&] {
            return resolve_client_event_input_route(event, window, context, nullptr, nullptr, nullptr, ownership);
        };
        EXPECT_EQ(resolve().native, ClientNativeRecipient::ui);
        ownership.pointer_owner = ClientPointerOwner::pc;
        EXPECT_EQ(resolve().native, ClientNativeRecipient::pc);
        EXPECT_TRUE(resolve().sources.pointer);
        ownership.world_available = false;
        EXPECT_EQ(resolve().native, ClientNativeRecipient::none);
        ownership.world_available = true;
        ownership.world_input_blocked = true;
        EXPECT_EQ(resolve().native, ClientNativeRecipient::none);
        EXPECT_FALSE(resolve().sources.pointer);
        ownership.world_input_blocked = false;
        ownership.command_modal = true;
        EXPECT_EQ(resolve().native, ClientNativeRecipient::ui);
        EXPECT_EQ(resolve().rml, ClientRmlRecipient::command);
        ownership.command_modal = false;
        ownership.map = ClientInputMap::editor;
        EXPECT_EQ(resolve().disposition, ClientInputDisposition::invalid_input);
        ownership.pointer_owner = ClientPointerOwner::editor;
        EXPECT_EQ(resolve().native, ClientNativeRecipient::editor);
        ownership.map = ClientInputMap::invalid;
        EXPECT_EQ(resolve().disposition, ClientInputDisposition::invalid_input);
    }
    // The same event observes a newly generated viewport, then a panel, rather
    // than retaining a hit/focus snapshot across document changes.
    WorkspaceTab tab;
    tab.kind = WorkspaceTabKind::area;
    tab.detail = "area";
    std::string markup;
    append_workspace_viewport_markup(markup, tab);
    auto* content = document->GetElementById("workspace_content");
    content->SetInnerRML("<div class='workspace_preview_body' style='position:absolute;left:0px;top:0px;width:600px;height:400px;'>" + markup + "</div>");
    context->Update();
    const ClientInputOwnership ownership{.map = ClientInputMap::pc, .world_available = true};
    EXPECT_EQ(resolve_client_event_input_route(event, window, context, nullptr, nullptr, nullptr, ownership).native, ClientNativeRecipient::pc);
    content->SetInnerRML("<button style='position:absolute;left:0px;top:0px;width:600px;height:400px;'>Panel</button>");
    context->Update();
    EXPECT_EQ(resolve_client_event_input_route(event, window, context, nullptr, nullptr, nullptr, ownership).native, ClientNativeRecipient::ui);
}

TEST_F(ClientInput, ControllerEdgesUseSharedPcBindingsThroughFreshFocusAndRoute)
{
    auto* field = document->GetElementById("variable");
    ASSERT_NE(field, nullptr);
    ASSERT_TRUE(field->Focus());
    for (const auto role : {ClientControlRole::editor, ClientControlRole::player, ClientControlRole::dm}) {
        const auto map = client_input_map(role, role == ClientControlRole::editor);
        ClientInputOwnership owner{.map = map, .world_available = true};
        RuntimeInputState input;
        for (const auto button : {SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_BACK}) {
            input.pending = {};
            ASSERT_TRUE(set_preview_click_target(input.pending, {3, 4, 5}));
            SDL_Event event{};
            event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            event.gbutton.button = button;
            auto route = resolve_client_event_input_route(event, window, context, nullptr, nullptr, nullptr, owner);
            ASSERT_EQ(route.native, ClientNativeRecipient::pc);
            EXPECT_TRUE(route.sources.controller);
            EXPECT_FALSE(route.sources.keyboard);
            ASSERT_EQ(apply_runtime_pc_controller_button(input, event, route), PreviewStatus::ok);
            EXPECT_EQ(input.pending.flags, preview_input_click_target | preview_input_cancel);
            PcDeviceSample physical;
            ASSERT_EQ(acquire_pc_device_sample(input, 0.001, route.sources, physical), PreviewStatus::ok);
            PreviewInputSample sample;
            ASSERT_EQ(translate_pc_input_samples({&physical, 1}, {&sample, 1}), PreviewStatus::ok);
            EXPECT_EQ(sample.flags, preview_input_click_target | preview_input_cancel);
            EXPECT_EQ(sample.click_target, (glm::vec3{3, 4, 5}));
            owner.command_modal = true;
            route = resolve_client_event_input_route(event, window, context, nullptr, nullptr, nullptr, owner);
            ASSERT_EQ(route.native, ClientNativeRecipient::ui);
            input.pending.flags &= ~preview_input_cancel;
            ASSERT_EQ(apply_runtime_pc_controller_button(input, event, route), PreviewStatus::ok);
            EXPECT_EQ(input.pending.flags, preview_input_click_target);
            owner.command_modal = false;
            event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            route = resolve_client_event_input_route(event, window, context, nullptr, nullptr, nullptr, owner);
            ASSERT_EQ(apply_runtime_pc_controller_button(input, event, route), PreviewStatus::ok);
            EXPECT_EQ(input.pending.flags, preview_input_click_target);
            event.type = SDL_EVENT_MOUSE_MOTION;
            EXPECT_EQ(apply_runtime_pc_controller_button(input, event, route), PreviewStatus::invalid_input);
            EXPECT_EQ(input.pending.flags, preview_input_click_target);
        }
    }
}

TEST(ClientEditorInput, ApplicationShortcutBatchesKeepTheCurrentModifierAndRepeatMatrix)
{
    using A = EditorShortcutAction;
    const std::array inputs{
        EditorShortcutInput{SDLK_P, SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI, true},
        EditorShortcutInput{SDLK_P, SDL_KMOD_CTRL, false},
        EditorShortcutInput{SDLK_J, SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI, true},
        EditorShortcutInput{SDLK_J, SDL_KMOD_GUI, false},
        EditorShortcutInput{SDLK_GRAVE, SDL_KMOD_SHIFT, true},
        EditorShortcutInput{SDLK_GRAVE, SDL_KMOD_ALT, false},
        EditorShortcutInput{SDLK_GRAVE, SDL_KMOD_CTRL, false},
        EditorShortcutInput{SDLK_GRAVE, SDL_KMOD_GUI, false},
        EditorShortcutInput{SDLK_W, SDL_KMOD_CTRL | SDL_KMOD_CAPS | SDL_KMOD_NUM, false},
        EditorShortcutInput{SDLK_W, SDL_KMOD_CTRL, true},
        EditorShortcutInput{SDLK_W, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, false},
        EditorShortcutInput{SDLK_S, SDL_KMOD_CTRL, false},
        EditorShortcutInput{SDLK_S, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, false},
        EditorShortcutInput{SDLK_S, SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT, false},
        EditorShortcutInput{SDLK_S, SDL_KMOD_CTRL | SDL_KMOD_GUI, false},
        EditorShortcutInput{SDLK_S, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, true},
        EditorShortcutInput{SDLK_Z, SDL_KMOD_CTRL, false},
        EditorShortcutInput{SDLK_Y, SDL_KMOD_CTRL, false},
        EditorShortcutInput{SDLK_Z, SDL_KMOD_CTRL | SDL_KMOD_SHIFT, false},
        EditorShortcutInput{SDLK_A, SDL_KMOD_CTRL, false},
        EditorShortcutInput{SDLK_UNKNOWN, SDL_KMOD_CTRL, false},
    };
    const std::array expected{A::palette_toggle, A::none, A::output_toggle, A::none,
        A::terminal_toggle, A::none, A::none, A::none, A::close_tab, A::none, A::none,
        A::save_tab, A::save_all, A::none, A::none, A::none, A::undo, A::redo,
        A::none, A::none, A::none};
    std::array<A, inputs.size()> actions;
    actions.fill(A::redo);
    ASSERT_TRUE(resolve_editor_shortcut_actions(inputs, actions));
    EXPECT_EQ(actions, expected);
    EXPECT_FALSE(resolve_editor_shortcut_actions(std::span{inputs}.first(1), actions));
    for (const auto action : actions) {
        EXPECT_EQ(action, A::none);
    }
    EXPECT_TRUE(resolve_editor_shortcut_actions({}, {}));
    RecordProperty("shortcut_input_bytes", sizeof(EditorShortcutInput));
    RecordProperty("shortcut_action_bytes", sizeof(EditorShortcutAction));
}

TEST(ClientEditorInput, WheelBatchesPreserveFocusModifiersAndRejectNonEditorRecipients)
{
    std::array<EditorWheelInput, 15> inputs;
    inputs.fill({.recipient = ClientNativeRecipient::editor, .viewport = EditorViewportKind::area, .object_type = nw::ObjectType::placeable, .amount = 1});
    inputs[1].modifiers = SDL_KMOD_CTRL;
    inputs[2].modifiers = SDL_KMOD_SHIFT;
    inputs[3].modifiers = SDL_KMOD_ALT;
    inputs[4].text_focused = true;
    inputs[5].object_type = nw::ObjectType::sound;
    inputs[6].object_type = nw::ObjectType::sound;
    inputs[6].modifiers = SDL_KMOD_CTRL;
    inputs[7].viewport = EditorViewportKind::preview;
    inputs[8].viewport = EditorViewportKind::none;
    inputs[9].amount = 0;
    inputs[10].amount = std::numeric_limits<float>::infinity();
    inputs[11].recipient = ClientNativeRecipient::pc;
    inputs[12].recipient = static_cast<ClientNativeRecipient>(255);
    inputs[13].viewport = static_cast<EditorViewportKind>(255);
    inputs[14].object_type = static_cast<nw::ObjectType>(255);
    const std::array expected{
        EditorWheelActionKind::object_scale, EditorWheelActionKind::object_rotate,
        EditorWheelActionKind::camera_zoom, EditorWheelActionKind::camera_zoom,
        EditorWheelActionKind::camera_zoom, EditorWheelActionKind::sound_radius,
        EditorWheelActionKind::camera_zoom, EditorWheelActionKind::camera_zoom,
        EditorWheelActionKind::none, EditorWheelActionKind::none, EditorWheelActionKind::none,
        EditorWheelActionKind::none, EditorWheelActionKind::none, EditorWheelActionKind::none,
        EditorWheelActionKind::camera_zoom};
    std::array<EditorWheelAction, inputs.size()> outputs{};
    ASSERT_TRUE(resolve_editor_wheel_actions(inputs, outputs));
    for (size_t i = 0; i < outputs.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(outputs[i].kind, expected[i]);
        EXPECT_EQ(outputs[i].amount, expected[i] == EditorWheelActionKind::none ? 0 : 1);
    }
    EXPECT_FALSE(resolve_editor_wheel_actions(std::span{inputs}.first(1), outputs));
    for (const auto& output : outputs) {
        EXPECT_EQ(output.kind, EditorWheelActionKind::none);
        EXPECT_EQ(output.amount, 0);
    }
    RecordProperty("wheel_input_bytes", sizeof(EditorWheelInput));
    RecordProperty("wheel_action_bytes", sizeof(EditorWheelAction));
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
    // The frame loop skips the command context after the palette closes.
    ASSERT_FALSE(box->IsVisible(true));
    route = resolve(capture_client_input_facts(event, window, context, command_context, palette, nullptr, owner));
    EXPECT_EQ(route.rml, ClientRmlRecipient::toolset);
    EXPECT_EQ(resolve_client_forward_route(event, window, palette, owner.map).rml, ClientRmlRecipient::toolset);
    route = resolve(capture_client_held_input_facts(context, command_context, palette, nullptr, owner));
    EXPECT_TRUE(route.sources.keyboard);
    EXPECT_TRUE(route.sources.controller);
    EXPECT_TRUE(route.sources.pointer);

    WorkspaceTab tab;
    tab.kind = WorkspaceTabKind::area;
    tab.detail = "area";
    std::string markup;
    append_workspace_viewport_markup(markup, tab);
    document->GetElementById("workspace_content")->SetInnerRML("<div class='workspace_preview_body' style='position:absolute;left:100px;top:100px;width:500px;height:300px;'>" + markup + "</div>");
    context->Update();
    auto* viewport = document->GetElementById("workspace_viewer_viewport");
    ASSERT_NE(viewport, nullptr);
    const Rml::Vector2f point{viewport->GetAbsoluteLeft() + 20, viewport->GetAbsoluteTop() + 20};
    for (const auto map : {ClientInputMap::editor, ClientInputMap::pc}) {
        const ClientInputOwnership ownership{.map = map, .world_available = true};
        for (const auto type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL, SDL_EVENT_KEY_DOWN}) {
            SDL_Event world_event{};
            world_event.type = type;
            if (type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                world_event.button.button = SDL_BUTTON_LEFT;
                world_event.button.x = point.x;
                world_event.button.y = point.y;
            } else if (type == SDL_EVENT_MOUSE_MOTION) {
                world_event.motion.x = point.x;
                world_event.motion.y = point.y;
            } else if (type == SDL_EVENT_MOUSE_WHEEL) {
                world_event.wheel.mouse_x = point.x;
                world_event.wheel.mouse_y = point.y;
                world_event.wheel.y = 1;
            } else {
                world_event.key.key = SDLK_W;
            }
            const auto facts = capture_client_input_facts(world_event, window, context, command_context, palette, nullptr, ownership);
            EXPECT_FALSE(facts.world_input_blocked);
            const auto world_route = resolve(facts);
            EXPECT_EQ(world_route.native, map == ClientInputMap::editor ? ClientNativeRecipient::editor : ClientNativeRecipient::pc);
            if (map == ClientInputMap::pc) {
                EXPECT_TRUE(world_route.sources.keyboard);
                EXPECT_TRUE(world_route.sources.pointer);
            } else if (type == SDL_EVENT_KEY_DOWN) {
                const std::array inputs{EditorKeyInput{
                    .facts = facts,
                    .key = SDLK_W,
                    .viewport = EditorViewportKind::area,
                    .viewport_focused = true}};
                std::array<EditorKeyAction, 1> actions{};
                ASSERT_TRUE(resolve_editor_key_actions(inputs, actions));
                EXPECT_EQ(actions[0].kind, EditorKeyActionKind::camera);
            }
        }
    }
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

TEST_F(ClientInput, EditorKeysUseFreshVisibleFocusAndRejectEveryPcRole)
{
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_W;
    const auto classify = [&](ClientInputMap map) {
        const std::array inputs{EditorKeyInput{
            .facts = capture_client_input_facts(event, window, context, nullptr, nullptr, nullptr,
                {.map = map, .world_available = true}),
            .key = event.key.key,
            .modifiers = event.key.mod,
            .viewport = EditorViewportKind::area,
            .repeat = event.key.repeat,
            .viewport_focused = true,
            .tile_action_allowed = true,
        }};
        std::array<EditorKeyAction, 1> actions{};
        EXPECT_TRUE(resolve_editor_key_actions(inputs, actions));
        return actions.front();
    };
    auto* variable = document->GetElementById("variable");
    ASSERT_NE(variable, nullptr);
    ASSERT_TRUE(variable->Focus());
    EXPECT_EQ(classify(ClientInputMap::editor).kind, EditorKeyActionKind::none);
    variable->SetProperty("display", "none");
    context->Update();
    ASSERT_FALSE(variable->IsVisible(true));
    auto action = classify(ClientInputMap::editor);
    EXPECT_EQ(action.kind, EditorKeyActionKind::camera);
    EXPECT_EQ(action.camera, ClientViewportCameraCommand::move_forward);
    event.key.repeat = true;
    event.key.mod = SDL_KMOD_SHIFT;
    EXPECT_FLOAT_EQ(classify(ClientInputMap::editor).scale, 3);
    event.key.mod = SDL_KMOD_CTRL;
    EXPECT_EQ(classify(ClientInputMap::editor).kind, EditorKeyActionKind::none);
    event.key.repeat = false;
    event.key.mod = SDL_KMOD_NONE;
    for (const auto role : {ClientControlRole::editor, ClientControlRole::player, ClientControlRole::dm}) {
        EXPECT_EQ(classify(client_input_map(role, true)).kind, EditorKeyActionKind::none);
    }
    EXPECT_EQ(classify(ClientInputMap::invalid).kind, EditorKeyActionKind::none);
    event.key.key = SDLK_R;
    EXPECT_EQ(classify(ClientInputMap::editor).kind, EditorKeyActionKind::rotate_tiles);
    EXPECT_EQ(classify(ClientInputMap::pc).kind, EditorKeyActionKind::none);
}

TEST(ClientEditorInput, BatchKeysPreserveCameraBindingsEditPriorityAndRejectInvalidRows)
{
    RecordProperty("editor_key_input_bytes", sizeof(EditorKeyInput));
    RecordProperty("editor_key_action_bytes", sizeof(EditorKeyAction));
    EditorKeyInput source{
        .facts = {.category = ClientInputCategory::key, .edge = ClientInputEdge::down, .map = ClientInputMap::editor},
        .key = SDLK_W,
        .viewport = EditorViewportKind::area,
        .viewport_focused = true,
    };
    std::array<EditorKeyInput, 12> inputs;
    inputs.fill(source);
    inputs[1].viewport = EditorViewportKind::preview;
    inputs[2].key = SDLK_G;
    inputs[2].viewport = EditorViewportKind::preview;
    inputs[3].key = SDLK_G;
    inputs[4].key = SDLK_R;
    inputs[4].viewport = EditorViewportKind::none;
    inputs[4].tile_action_allowed = true;
    inputs[5].key = SDLK_DELETE;
    inputs[6].key = SDLK_R;
    inputs[7].key = SDLK_R;
    inputs[7].repeat = true;
    inputs[8].viewport = EditorViewportKind::none;
    inputs[9].key = SDLK_UNKNOWN;
    inputs[9].viewport = EditorViewportKind::none;
    inputs[10].viewport = static_cast<EditorViewportKind>(255);
    inputs[11].facts.world_input_blocked = true;
    std::array<EditorKeyAction, 12> actions{};
    ASSERT_TRUE(resolve_editor_key_actions(inputs, actions));
    EXPECT_EQ(actions[0].camera, ClientViewportCameraCommand::move_forward);
    EXPECT_EQ(actions[0].kind, EditorKeyActionKind::camera);
    EXPECT_EQ(actions[1].camera, ClientViewportCameraCommand::pitch_up);
    EXPECT_EQ(actions[1].kind, EditorKeyActionKind::camera);
    EXPECT_EQ(actions[2].kind, EditorKeyActionKind::none);
    EXPECT_EQ(actions[3].camera, ClientViewportCameraCommand::gameplay);
    EXPECT_EQ(actions[4].kind, EditorKeyActionKind::rotate_tiles);
    EXPECT_EQ(actions[5].kind, EditorKeyActionKind::remove_object);
    EXPECT_EQ(actions[6].kind, EditorKeyActionKind::randomize_object_orientation);
    EXPECT_EQ(actions[7].kind, EditorKeyActionKind::none);
    EXPECT_TRUE(actions[8].clear_viewport_focus);
    EXPECT_EQ(actions[8].kind, EditorKeyActionKind::none);
    EXPECT_FALSE(actions[9].clear_viewport_focus);
    EXPECT_EQ(actions[9].kind, EditorKeyActionKind::none);
    EXPECT_EQ(actions[10].kind, EditorKeyActionKind::none);
    EXPECT_EQ(actions[11].kind, EditorKeyActionKind::none);
    source.facts.pointer_owner = ClientPointerOwner::toolset;
    ASSERT_TRUE(resolve_editor_key_actions({&source, 1}, {actions.data(), 1}));
    EXPECT_EQ(actions[0].kind, EditorKeyActionKind::camera);
    EXPECT_TRUE(editor_key_has_binding(SDLK_G));
    EXPECT_TRUE(editor_key_has_binding(SDLK_DELETE));
    EXPECT_FALSE(editor_key_has_binding(SDLK_UNKNOWN));
    source.key = SDLK_W;
    source.viewport_focused = false;
    source.tiles_active = true;
    ASSERT_TRUE(resolve_editor_key_actions({&source, 1}, {actions.data(), 1}));
    EXPECT_EQ(actions[0].kind, EditorKeyActionKind::camera);
    source.viewport = EditorViewportKind::preview;
    ASSERT_TRUE(resolve_editor_key_actions({&source, 1}, {actions.data(), 1}));
    EXPECT_EQ(actions[0].kind, EditorKeyActionKind::none);
    source.facts.category = ClientInputCategory::held;
    ASSERT_TRUE(resolve_editor_key_actions({&source, 1}, {actions.data(), 1}));
    EXPECT_EQ(actions[0].kind, EditorKeyActionKind::none);
    actions.fill({.kind = EditorKeyActionKind::camera, .scale = 3, .clear_viewport_focus = true});
    EXPECT_FALSE(resolve_editor_key_actions({}, actions));
    for (const auto& action : actions) {
        EXPECT_EQ(action.kind, EditorKeyActionKind::none);
        EXPECT_FLOAT_EQ(action.scale, 1);
        EXPECT_FALSE(action.clear_viewport_focus);
    }
}
