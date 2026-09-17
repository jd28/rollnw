#include "command_view.hpp"
#include "loading_view.hpp"
#include "shell_controller.hpp"

#include <nw/kernel/Kernel.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory>

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
        SDL_Event event{};
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT,
                   state.open_module_dialog_event, state.open_module_dialog_event)
            > 0) {
            delete static_cast<OpenModuleDialogResult*>(event.user.data1);
        }
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
    open_module_dialog_callback(new OpenModuleDialogRequest{state.open_module_dialog_event}, files, 0);
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

TEST_F(ClientLoading, CancellationNullPayloadAndUnrelatedEventsRetainTheirPolicies)
{
    state.module_dialog_open = true;
    state.module_dialog_command = "import.destination";
    const char* files[] = {nullptr};
    open_module_dialog_callback(new OpenModuleDialogRequest{state.open_module_dialog_event}, files, 0);
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
    open_module_dialog_callback(new OpenModuleDialogRequest{state.open_module_dialog_event}, files, 0);
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
