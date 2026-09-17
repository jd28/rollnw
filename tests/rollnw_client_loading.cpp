#include "command_view.hpp"
#include "loading_view.hpp"
#include "shell_controller.hpp"

#include <nw/kernel/Kernel.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <thread>

using namespace nw::toolset;

class ClientLoading : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (const char* hint = SDL_GetHint(SDL_HINT_VIDEO_DRIVER)) { previous_video_hint = hint; }
        if (SDL_WasInit(SDL_INIT_VIDEO) && std::string_view{SDL_GetCurrentVideoDriver()} != "dummy") {
            GTEST_SKIP() << "An existing SDL video subsystem uses another driver";
        }
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { GTEST_SKIP() << "Dummy video unavailable: " << SDL_GetError(); }
        video_started = true;
        ASSERT_STREQ(SDL_GetCurrentVideoDriver(), "dummy");
        ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_EVENTS));
        events_started = true;
        state.open_module_dialog_event = SDL_RegisterEvents(1);
        ASSERT_NE(state.open_module_dialog_event, 0u);
    }

    void TearDown() override
    {
        (void)close_loading_dialog_delivery(state);
        if (events_started) { SDL_QuitSubSystem(SDL_INIT_EVENTS); }
        if (video_started) { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
        if (previous_video_hint) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, previous_video_hint->c_str());
        } else {
            SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        }
    }

    SDL_Event take_event()
    {
        SDL_Event event{};
        EXPECT_EQ(SDL_PeepEvents(&event, 1, SDL_GETEVENT,
                      state.open_module_dialog_event, state.open_module_dialog_event),
            1);
        return event;
    }

    LoadingViewState state;
    ShellController shell;
    bool events_started = false;
    bool video_started = false;
    std::optional<std::string> previous_video_hint;
};

TEST_F(ClientLoading, NativePayloadCopiesSelectionAndIsConsumedOnce)
{
    state.module_dialog_open = true;
    state.module_dialog_command = "import.module";
    std::string path = "module ; $ with spaces.mod";
    const char* files[] = {path.c_str(), nullptr};
    open_module_dialog_callback(new OpenModuleDialogRequest{state.open_module_dialog_event, state.native_dialog_delivery}, files, 0);
    path = "changed after callback";
    auto event = take_event();
    const auto result = take_loading_dialog_result(state, event);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->command, "import.module");
    EXPECT_EQ(result->selection.path, "module ; $ with spaces.mod");
    EXPECT_FALSE(state.module_dialog_open);
    EXPECT_TRUE(state.module_dialog_command.empty());
    EXPECT_EQ(event.user.data1, nullptr);
    EXPECT_FALSE(take_loading_dialog_result(state, event));
    EXPECT_EQ(apply_loading_dialog_selection(state, *result, nullptr, shell), LoadingDialogAction::import_changed);
    EXPECT_EQ(state.import_module_path, result->selection.path);
    EXPECT_NE(state.import_status.find("Review"), std::string::npos);
}

TEST_F(ClientLoading, LateCallbackCannotQueueForAnExpiredOwnerAfterSdlRestart)
{
    const auto expired_event_type = state.open_module_dialog_event;
    auto expired = std::make_unique<LoadingViewState>();
    expired->open_module_dialog_event = expired_event_type;
    auto request = std::make_unique<OpenModuleDialogRequest>(OpenModuleDialogRequest{expired->open_module_dialog_event, expired->native_dialog_delivery});
    expired.reset();
    (void)close_loading_dialog_delivery(state);
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    events_started = false;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    video_started = false;
    ASSERT_EQ(SDL_WasInit(SDL_INIT_EVENTS), 0u);
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    video_started = true;
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_EVENTS));
    events_started = true;
    LoadingViewState fresh;
    fresh.open_module_dialog_event = SDL_RegisterEvents(1);
    ASSERT_NE(fresh.open_module_dialog_event, 0u);
    ASSERT_NE(fresh.open_module_dialog_event, expired_event_type);
    const char* files[] = {"expired owner's selection.mod", nullptr};
    auto* callback_request = request.release();
    std::thread completion{[&] { open_module_dialog_callback(callback_request, files, 0); }};
    completion.join();
    SDL_Event event{};
    const auto received = SDL_PeepEvents(&event, 1, SDL_GETEVENT,
        expired_event_type, expired_event_type);
    EXPECT_EQ(received, 0);
    if (received == 1) { delete static_cast<OpenModuleDialogResult*>(event.user.data1); }
    EXPECT_FALSE(fresh.module_dialog_open);
    EXPECT_EQ(close_loading_dialog_delivery(fresh), 0);
}

TEST_F(ClientLoading, DeliveryClosureDisposesOnlyItsQueuedPayloadsAndDropsPendingRequests)
{
    const auto event_type = state.open_module_dialog_event;
    std::weak_ptr<NativeDialogDeliveryState> lifetime = state.native_dialog_delivery;
    state.module_dialog_open = true;
    state.module_dialog_command = "toolset.open";
    const char* files[] = {"queued.mod", nullptr};
    const char* canceled[] = {nullptr};
    open_module_dialog_callback(new OpenModuleDialogRequest{event_type, state.native_dialog_delivery}, files, 0);
    open_module_dialog_callback(new OpenModuleDialogRequest{event_type, state.native_dialog_delivery}, canceled, 0);
    auto pending = std::make_unique<OpenModuleDialogRequest>(OpenModuleDialogRequest{event_type, state.native_dialog_delivery});
    const auto unrelated_type = SDL_RegisterEvents(1);
    ASSERT_NE(unrelated_type, 0u);
    auto unrelated = std::make_unique<OpenModuleDialogResult>();
    SDL_Event other{};
    other.type = unrelated_type;
    other.user.data1 = unrelated.get();
    ASSERT_TRUE(SDL_PushEvent(&other));

    EXPECT_EQ(close_loading_dialog_delivery(state), 2);
    EXPECT_EQ(state.open_module_dialog_event, 0u);
    EXPECT_FALSE(state.module_dialog_open);
    EXPECT_TRUE(state.module_dialog_command.empty());
    EXPECT_EQ(state.native_dialog_delivery, nullptr);
    EXPECT_FALSE(lifetime.expired());
    auto* request = pending.release();
    std::thread completion{[&] { open_module_dialog_callback(request, files, 0); }};
    completion.join();
    EXPECT_TRUE(lifetime.expired());
    SDL_Event event{};
    EXPECT_EQ(SDL_PeepEvents(&event, 1, SDL_GETEVENT, event_type, event_type), 0);
    ASSERT_EQ(SDL_PeepEvents(&event, 1, SDL_GETEVENT, unrelated_type, unrelated_type), 1);
    EXPECT_EQ(event.user.data1, unrelated.get());
    EXPECT_EQ(close_loading_dialog_delivery(state), 0);
    EXPECT_EQ(show_loading_module_dialog(nullptr, state), LoadingDialogStatus::unavailable);
    EXPECT_EQ(show_loading_project_dialog(nullptr, state), LoadingDialogStatus::unavailable);
    show_loading_blueprint_directory_dialog(nullptr, state, "unused");
    EXPECT_FALSE(state.module_dialog_open);
    EXPECT_TRUE(state.module_dialog_command.empty());
}

TEST_F(ClientLoading, LiveWorkerCopiesItsErrorAndMalformedRequestsNeverPublish)
{
    state.module_dialog_open = true;
    state.module_dialog_command = "toolset.open";
    auto* request = new OpenModuleDialogRequest{state.open_module_dialog_event, state.native_dialog_delivery};
    std::thread completion{[&] {
        SDL_SetError("worker dialog failure");
        open_module_dialog_callback(request, nullptr, 0);
    }};
    completion.join();
    auto event = take_event();
    const auto result = take_loading_dialog_result(state, event);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->selection.error, "worker dialog failure");
    EXPECT_TRUE(result->selection.path.empty());
    EXPECT_FALSE(result->selection.canceled);
    EXPECT_EQ(event.user.data1, nullptr);
    EXPECT_FALSE(take_loading_dialog_result(state, event));
    const char* files[] = {"unused.mod", nullptr};
    open_module_dialog_callback(nullptr, files, 0);
    open_module_dialog_callback(new OpenModuleDialogRequest{state.open_module_dialog_event, {}}, files, 0);
    for (const auto tag : {Uint32{0}, Uint32{SDL_EVENT_QUIT}, UINT32_MAX}) {
        open_module_dialog_callback(new OpenModuleDialogRequest{tag, state.native_dialog_delivery}, files, 0);
    }
    EXPECT_EQ(SDL_PeepEvents(&event, 1, SDL_GETEVENT, state.open_module_dialog_event, state.open_module_dialog_event), 0);
}

TEST_F(ClientLoading, CancellationNullPayloadAndUnrelatedEventsRetainTheirPolicies)
{
    state.module_dialog_open = true;
    state.module_dialog_command = "import.destination";
    const char* files[] = {nullptr};
    open_module_dialog_callback(new OpenModuleDialogRequest{state.open_module_dialog_event, state.native_dialog_delivery}, files, 0);
    auto event = take_event();
    auto result = take_loading_dialog_result(state, event);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->selection.canceled);
    state.import_parent_dir = "keep destination";
    EXPECT_EQ(apply_loading_dialog_selection(state, *result, nullptr, shell), LoadingDialogAction::import_changed);
    EXPECT_EQ(state.import_parent_dir, "keep destination");
    EXPECT_EQ(state.import_status, "Selection canceled. No project files were written.");

    state.module_dialog_open = true;
    state.module_dialog_command = "toolset.open_project";
    std::unique_ptr<OpenModuleDialogResult> unrelated{new OpenModuleDialogResult{}};
    event.type = SDL_EVENT_USER;
    if (event.type == state.open_module_dialog_event) { ++event.type; }
    event.user.data1 = unrelated.get();
    EXPECT_FALSE(take_loading_dialog_result(state, event));
    EXPECT_EQ(event.user.data1, unrelated.get());
    EXPECT_TRUE(state.module_dialog_open);
    EXPECT_EQ(state.module_dialog_command, "toolset.open_project");
    event.type = state.open_module_dialog_event;
    event.user.data1 = nullptr;
    EXPECT_FALSE(take_loading_dialog_result(state, event));
    EXPECT_FALSE(state.module_dialog_open);
    EXPECT_TRUE(state.module_dialog_command.empty());
}

TEST_F(ClientLoading, NativeDirectoryResultStillUsesCommandFormGeneration)
{
    CommandViewState command;
    command.command_form = CommandPrompt{};
    command.command_form->id = "blueprint.create";
    command.command_form->fields.resize(2);
    command.command_form->fields[1].value = "current";
    command.command_form_generation = 12;
    command.command_form_browse_generation = 11;
    state.module_dialog_open = true;
    state.module_dialog_command = "blueprint.directory";
    const char* files[] = {"selected directory", nullptr};
    open_module_dialog_callback(new OpenModuleDialogRequest{state.open_module_dialog_event, state.native_dialog_delivery}, files, 0);
    auto event = take_event();
    const auto result = take_loading_dialog_result(state, event);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->command, "blueprint.directory");
    EXPECT_FALSE(apply_command_form_directory_result(command, result->selection.path, result->selection.error, result->selection.canceled));
    EXPECT_EQ(command.command_form->fields[1].value, "current");
}

TEST_F(ClientLoading, CompletionQueuesOneUnpresentedLoadAndKeepsBusyWork)
{
    ProjectImportCompletion result{.ok = true, .project_dir = "completed project", .message = "Imported"};
    state.import_module_generation = 9;
    state.import_status = result.message;
    EXPECT_FALSE(queue_loading_project(state, "", CommandSource::widget));
    EXPECT_TRUE(finish_loading_import(state, result, {.module_generation = 9}, nullptr));
    EXPECT_EQ(state.project_load.path, "completed project");
    EXPECT_TRUE(state.project_load.close_import_panel_on_success);
    EXPECT_FALSE(state.project_load.presented);
    EXPECT_EQ(state.project_load.source, CommandSource::widget);
    EXPECT_FALSE(queue_loading_project(state, "second", CommandSource::palette));
    EXPECT_EQ(state.project_load.path, "completed project");

    // With the dummy driver no desktop dialog can be displayed. These are the
    // real completion policies; graphics/native dialog presentation is separate.
    state.project_load = {};
    EXPECT_FALSE(finish_loading_import(state, {.ok = false}, {.module_generation = 9}, nullptr));
    EXPECT_FALSE(state.project_load.active());
    EXPECT_FALSE(poll_loading_import(state, nullptr, shell));
    const std::array busy_work{
        LoadingImportWorkState{.dirty_tabs = true, .module_generation = 9},
        LoadingImportWorkState{.module_generation = 10},
        LoadingImportWorkState{.module_generation = 9, .preview_active = true},
    };
    for (const auto work : busy_work) {
        state.import_status = result.message;
        EXPECT_FALSE(finish_loading_import(state, result, work, nullptr));
        EXPECT_FALSE(state.project_load.active());
        EXPECT_NE(state.import_status.find("current work was kept open"), std::string::npos);
    }
    state.module_dialog_open = true;
    EXPECT_FALSE(finish_loading_import(state, result, {.module_generation = 9}, nullptr));
    EXPECT_FALSE(state.project_load.active());
    state.module_dialog_open = false;
    ASSERT_TRUE(queue_loading_project(state, "previous", CommandSource::palette));
    EXPECT_FALSE(finish_loading_import(state, result, {.module_generation = 9}, nullptr));
    EXPECT_EQ(state.project_load.path, "previous");
    EXPECT_NE(state.import_status.find("Another project is already opening"), std::string::npos);
}

TEST(ClientLoadingPresentation, StageFallbackAndHomeControlsUseOwnedState)
{
    using Stage = nw::kernel::ModuleLoadProgressStage;
    EXPECT_EQ(project_load_stage_message(Stage::load_dependencies), "Loading HAK and TLK dependencies...");
    EXPECT_EQ(project_load_stage_message(static_cast<Stage>(255)), "Opening project...");
    LoadingViewState state;
    state.import_panel_open = true;
    state.import_module_path = "source <&>.mod";
    state.import_parent_dir = "parent";
    state.import_status = "status <&>";
    std::string markup;
    append_loading_home_markup(markup, state);
    EXPECT_NE(markup.find("home_import_start"), std::string::npos);
    EXPECT_NE(markup.find("source &lt;&amp;&gt;.mod"), std::string::npos);
    EXPECT_NE(markup.find("status &lt;&amp;&gt;"), std::string::npos);
    state.module_dialog_open = true;
    markup.clear();
    append_loading_home_markup(markup, state);
    EXPECT_NE(markup.find("id=\"home_import_start\" disabled"), std::string::npos);
}
