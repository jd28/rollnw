#pragma once

#include "command_bus.hpp"
#include "project_import.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

class ClientRenderer;

namespace Rml {
class Context;
class Element;
class ElementDocument;
}

namespace nw::kernel {
enum struct ModuleLoadProgressStage : uint8_t;
}

namespace nw::toolset {

class ShellController;
class ToolsetBackend;

struct ProjectLoadRequest {
    std::string path;
    std::string stage = "Indexing project files...";
    CommandSource source = CommandSource::widget;
    bool presented = false;
    bool close_import_panel_on_success = false;

    [[nodiscard]] bool active() const noexcept { return !path.empty(); }
};

// One desktop's native dialog/import/load state. Job and strings are owned;
// window/context/document borrows never survive a synchronous presentation call.
struct LoadingViewState {
    bool module_dialog_open = false;
    Uint32 open_module_dialog_event = 0;
    std::string module_dialog_command;
    std::string module_dialog_default_location;
    std::string import_module_path;
    std::filesystem::path import_parent_dir;
    bool import_panel_open = false;
    std::string import_status;
    ProjectImportJob project_import;
    ProjectLoadRequest project_load;
    uint64_t import_module_generation = 0;
};

// Callback owns the request, copies SDL's transient selection, then transfers
// one heap result into the SDL event queue. Failed enqueue destroys the result.
struct OpenModuleDialogRequest {
    Uint32 event_type = 0;
};

struct OpenModuleDialogResult {
    std::string path;
    std::string error;
    bool canceled = false;
};

void SDLCALL open_module_dialog_callback(void* userdata, const char* const* filelist, int filter);

struct LoadingDialogSelection {
    std::string command;
    OpenModuleDialogResult selection;
};

// Consumes only this owner's registered event type. Clears data1 after deletion;
// a null payload still closes the dialog, matching the original event path.
[[nodiscard]] std::optional<LoadingDialogSelection> take_loading_dialog_result(
    LoadingViewState& state, SDL_Event& event);

enum class LoadingDialogStatus : uint8_t {
    started,
    unavailable,
    already_active,
    import_active,
};

[[nodiscard]] LoadingDialogStatus show_loading_module_dialog(
    SDL_Window* window, LoadingViewState& state, bool import = false);
[[nodiscard]] LoadingDialogStatus show_loading_project_dialog(
    SDL_Window* window, LoadingViewState& state, bool import = false);
void show_loading_blueprint_directory_dialog(SDL_Window* window, LoadingViewState& state,
    const std::filesystem::path& location);

enum class LoadingDialogAction : uint8_t {
    none,
    import_changed,
    open_project,
    dispatch,
};

// Blueprint browse-generation validation remains with command_view. All other
// paths return owned values for current backend command dispatch by the root.
[[nodiscard]] LoadingDialogAction apply_loading_dialog_selection(LoadingViewState& state,
    const LoadingDialogSelection& result, SDL_Window* window, ShellController& shell);

// Appends the loading-owned controls/status into the existing home surface.
void append_loading_home_markup(std::string& content_markup, const LoadingViewState& state);

enum class LoadingHomeAction : uint8_t {
    none,
    changed,
    browse_module,
    browse_destination,
    open_project,
};

[[nodiscard]] LoadingHomeAction handle_loading_home_target(LoadingViewState& state,
    Rml::Element* target, SDL_Window* window, ShellController& shell,
    const std::filesystem::path& executable, uint64_t module_generation);

[[nodiscard]] std::optional<ProjectImportCompletion> poll_loading_import(
    LoadingViewState& state, SDL_Window* window, ShellController& shell);

// Current-work facts are copied after completion. No workspace/session owner or
// pointer is retained by the load coordinator.
struct LoadingImportWorkState {
    bool dirty_tabs = false;
    uint64_t module_generation = 0;
    bool preview_active = false;
};

// Called after recording the completed project in preferences. Failed results
// do nothing; success either keeps current work or queues this owner's load.
[[nodiscard]] bool finish_loading_import(LoadingViewState& state,
    const ProjectImportCompletion& result, LoadingImportWorkState work, SDL_Window* window);

[[nodiscard]] bool queue_loading_project(LoadingViewState& state, std::string path,
    CommandSource source, bool close_import_panel_on_success = false);
void sync_loading_overlay(Rml::ElementDocument* document, const ProjectLoadRequest& load);
[[nodiscard]] std::string_view project_load_stage_message(nw::kernel::ModuleLoadProgressStage stage) noexcept;

// Blocking main-thread singleton. The stack progress callback pumps SDL events
// but never polls/dispatches them. The root resolves prompts before clearing the
// load and applying cross-feature workspace effects.
[[nodiscard]] std::optional<CommandResult> poll_loading_project(LoadingViewState& state,
    SDL_Window* window, Rml::Context* context, Rml::Context* command_context,
    Rml::ElementDocument* overlay, ClientRenderer& renderer, ToolsetBackend& backend);

} // namespace nw::toolset
