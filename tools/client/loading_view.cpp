#include "loading_view.hpp"
#include "shell_controller.hpp"

#include <RmlUi/Core.h>
#include <nw/kernel/Kernel.hpp>

#include <array>
#include <memory>
#include <utility>

namespace nw::toolset {
namespace {

Rml::Element* find_el(Rml::ElementDocument* doc, const char* id)
{
    return doc ? doc->GetElementById(id) : nullptr;
}

Rml::Element* find_ancestor_with_id(Rml::Element* element, std::string_view id)
{
    for (; element; element = element->GetParentNode()) {
        if (element->GetId() == id) { return element; }
    }
    return nullptr;
}

std::string escape_html(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::filesystem::path module_dialog_start_location()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const fs::path user = nw::kernel::config().user_path();
    if (!user.empty()) {
        const fs::path modules = user / "modules";
        if (fs::is_directory(modules, ec)) {
            return modules;
        }
        if (fs::is_directory(user, ec)) {
            return user;
        }
    }

    return {};
}

std::filesystem::path project_dialog_start_location()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec && fs::is_directory(cwd, ec)) {
        return cwd;
    }
    return module_dialog_start_location();
}

} // namespace

void SDLCALL open_module_dialog_callback(void* userdata, const char* const* filelist, int /*filter*/)
{
    std::unique_ptr<OpenModuleDialogRequest> request{static_cast<OpenModuleDialogRequest*>(userdata)};
    if (!request || !request->delivery || request->event_type < SDL_EVENT_USER
        || request->event_type > SDL_EVENT_LAST) { return; }
    std::lock_guard lock{request->delivery->mutex};
    if (!request->delivery->accepting) { return; }

    auto result = std::make_unique<OpenModuleDialogResult>();
    if (!filelist) {
        result->error = SDL_GetError();
    } else if (!filelist[0]) {
        result->canceled = true;
    } else {
        result->path = filelist[0];
    }

    SDL_Event event{};
    event.type = request->event_type;
    event.user.data1 = result.get();
    if (SDL_PushEvent(&event)) { (void)result.release(); }
}

LoadingViewState::~LoadingViewState()
{
    if (native_dialog_delivery) {
        std::lock_guard lock{native_dialog_delivery->mutex};
        native_dialog_delivery->accepting = false;
    }
}

size_t close_loading_dialog_delivery(LoadingViewState& state)
{
    if (state.native_dialog_delivery) {
        std::lock_guard lock{state.native_dialog_delivery->mutex};
        state.native_dialog_delivery->accepting = false;
    }
    state.native_dialog_delivery.reset();
    size_t disposed = 0;
    const auto event_type = std::exchange(state.open_module_dialog_event, 0u);
    state.module_dialog_open = false;
    state.module_dialog_command.clear();
    if (event_type < SDL_EVENT_USER || event_type > SDL_EVENT_LAST || !SDL_WasInit(SDL_INIT_EVENTS)) { return disposed; }
    SDL_Event event{};
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, event_type, event_type) > 0) {
        delete static_cast<OpenModuleDialogResult*>(event.user.data1);
        ++disposed;
    }
    return disposed;
}

void sync_loading_overlay(Rml::ElementDocument* document, const ProjectLoadRequest& load)
{
    auto* host = find_el(document,
        "project_load_overlay");
    if (!host) { return; }
    host->SetClass("active", load.active());
    if (!load.active()) {
        host->SetInnerRML("");
        return;
    }

    auto* message = find_el(document,
        "project_load_message");
    if (!message) {
        std::string markup
            = "<div class=\"command_form project_load_panel\">"
              "<div class=\"command_form_title\">Opening Project</div>"
              "<div id=\"project_load_message\" class=\"command_form_message\"></div>"
              "<div class=\"home_import_progress\"><div class=\"home_import_progress_fill\"></div></div>"
              "<div class=\"project_load_path\">";
        markup += escape_html(load.path);
        markup += "</div></div>";
        host->SetInnerRML(markup);
        message = find_el(document,
            "project_load_message");
    }
    if (message) {
        message->SetInnerRML(escape_html(load.stage));
    }
}

bool queue_loading_project(LoadingViewState& state, std::string path,
    nw::toolset::CommandSource source,
    bool close_import_panel_on_success)
{
    if (path.empty() || state.project_load.active()) { return false; }
    state.project_load = {
        .path = std::move(path),
        .source = source,
        .close_import_panel_on_success = close_import_panel_on_success,
    };
    return true;
}

LoadingDialogStatus show_loading_module_dialog(SDL_Window* window, LoadingViewState& state, bool import)
{
    if (state.open_module_dialog_event == 0) {
        return LoadingDialogStatus::unavailable;
    }
    if (state.module_dialog_open) {
        return LoadingDialogStatus::already_active;
    }
    if (import && state.project_import.active()) {
        return LoadingDialogStatus::import_active;
    }

    static constexpr SDL_DialogFileFilter filters[] = {
        {"Neverwinter modules", "mod;zip"},
        {"All files", "*"},
    };
    static constexpr SDL_DialogFileFilter import_filters[] = {
        {"Neverwinter Nights modules", "mod"},
    };

    state.module_dialog_default_location = module_dialog_start_location().string();
    const char* default_location = state.module_dialog_default_location.empty()
        ? nullptr
        : state.module_dialog_default_location.c_str();

    state.module_dialog_command = import ? "import.module" : "toolset.open";
    if (import) {
        state.import_status = "Choose the .mod file to import.";
    }
    state.module_dialog_open = true;
    SDL_ShowOpenFileDialog(open_module_dialog_callback,
        new OpenModuleDialogRequest{state.open_module_dialog_event, state.native_dialog_delivery},
        window,
        import ? import_filters : filters,
        import ? 1 : static_cast<int>(sizeof(filters) / sizeof(filters[0])),
        default_location,
        false);
    return LoadingDialogStatus::started;
}

LoadingDialogStatus show_loading_project_dialog(SDL_Window* window, LoadingViewState& state, bool import)
{
    if (state.open_module_dialog_event == 0) {
        return LoadingDialogStatus::unavailable;
    }
    if (state.module_dialog_open) {
        return LoadingDialogStatus::already_active;
    }
    if (import && state.project_import.active()) { return LoadingDialogStatus::import_active; }

    state.module_dialog_default_location = project_dialog_start_location().string();
    const char* default_location = state.module_dialog_default_location.empty()
        ? nullptr
        : state.module_dialog_default_location.c_str();

    state.module_dialog_command = import ? "import.destination" : "toolset.open_project";
    state.module_dialog_open = true;
    const auto props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
    if (default_location) {
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, default_location);
    }
    const std::string title = import
        ? "Choose a parent folder for the imported project"
        : "Open Project";
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_ACCEPT_STRING, import ? "Select Folder" : "Open Project");
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, open_module_dialog_callback,
        new OpenModuleDialogRequest{state.open_module_dialog_event, state.native_dialog_delivery},
        props);
    SDL_DestroyProperties(props);
    return LoadingDialogStatus::started;
}

void show_loading_blueprint_directory_dialog(SDL_Window* window, LoadingViewState& state,
    const std::filesystem::path& location)
{
    if (state.open_module_dialog_event == 0) { return; }
    state.module_dialog_default_location = location.string();
    state.module_dialog_command = "blueprint.directory";
    state.module_dialog_open = true;
    SDL_ShowOpenFolderDialog(open_module_dialog_callback,
        new OpenModuleDialogRequest{state.open_module_dialog_event, state.native_dialog_delivery}, window,
        state.module_dialog_default_location.c_str(), false);
}

std::optional<LoadingDialogSelection> take_loading_dialog_result(LoadingViewState& state, SDL_Event& event)
{
    if (!state.open_module_dialog_event || event.type != state.open_module_dialog_event) { return std::nullopt; }
    state.module_dialog_open = false;
    auto command = state.module_dialog_command.empty() ? std::string{"toolset.open"} : state.module_dialog_command;
    state.module_dialog_command.clear();
    std::unique_ptr<OpenModuleDialogResult> result{static_cast<OpenModuleDialogResult*>(event.user.data1)};
    event.user.data1 = nullptr;
    if (!result) { return std::nullopt; }
    return LoadingDialogSelection{std::move(command), std::move(*result)};
}

LoadingDialogAction apply_loading_dialog_selection(LoadingViewState& state,
    const LoadingDialogSelection& result, SDL_Window* window, ShellController& shell)
{
    const auto& command = result.command;
    const auto& [path, error, canceled] = result.selection;
    const bool importing = command == "import.module" || command == "import.destination";
    if (!error.empty()) {
        shell.append_output("error", std::string{"File dialog failed: "} + error);
        if (importing) {
            state.import_status = "Import dialog failed: " + error;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Import failed", error.c_str(), window);
            return LoadingDialogAction::import_changed;
        }
        return LoadingDialogAction::none;
    }
    if (canceled || path.empty()) {
        if (importing) {
            state.import_status = "Selection canceled. No project files were written.";
            return LoadingDialogAction::import_changed;
        }
        return LoadingDialogAction::none;
    }
    if (command == "import.module") {
        state.import_module_path = path;
        state.import_status = "Review the source and destination, then click Import.";
        return LoadingDialogAction::import_changed;
    }
    if (command == "import.destination") {
        state.import_parent_dir = path;
        state.import_status = "Review the source and destination, then click Import.";
        return LoadingDialogAction::import_changed;
    }
    return command == "toolset.open_project" ? LoadingDialogAction::open_project : LoadingDialogAction::dispatch;
}

LoadingHomeAction handle_loading_home_target(LoadingViewState& state,
    Rml::Element* target, SDL_Window* window, ShellController& shell,
    const std::filesystem::path& executable, uint64_t module_generation)
{
    if (find_ancestor_with_id(target, "home_import_module")) {
        state.import_panel_open = true;
    } else if (find_ancestor_with_id(target, "home_import_source")) {
        return LoadingHomeAction::browse_module;
    } else if (find_ancestor_with_id(target, "home_import_destination")) {
        return LoadingHomeAction::browse_destination;
    } else if (find_ancestor_with_id(target, "home_import_close")) {
        if (!state.project_import.active() && !state.module_dialog_open) { state.import_panel_open = false; }
    } else if (find_ancestor_with_id(target, "home_import_start")) {
        if (state.project_import.active() || state.module_dialog_open) { return LoadingHomeAction::none; }
        std::string error;
        if (state.project_import.start(executable, state.import_module_path, state.import_parent_dir, error)) {
            state.import_module_generation = module_generation;
            state.import_status = "Importing... You can continue working; please wait for import to finish before quitting.";
            shell.append_output("info", state.import_status);
        } else {
            state.import_status = error;
            shell.append_output("error", error);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Import failed", error.c_str(), window);
        }
    } else if (find_ancestor_with_id(target, "home_open_project")) {
        return LoadingHomeAction::open_project;
    } else {
        return LoadingHomeAction::none;
    }
    return LoadingHomeAction::changed;
}

std::optional<ProjectImportCompletion> poll_loading_import(LoadingViewState& state,
    SDL_Window* window, ShellController& shell)
{
    auto result = state.project_import.poll();
    if (!result) { return std::nullopt; }
    state.import_status = result->message;
    shell.append_output(result->ok ? "info" : "error", result->message);
    if (!result->ok) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Import failed", result->message.c_str(), window); }
    return result;
}

bool finish_loading_import(LoadingViewState& state, const ProjectImportCompletion& result,
    LoadingImportWorkState work, SDL_Window* window)
{
    if (!result.ok) { return false; }
    if (work.dirty_tabs || work.module_generation != state.import_module_generation
        || state.module_dialog_open || work.preview_active) {
        state.import_status += ". Open it from Open Project or Recent Projects when you are ready; your current work was kept open.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Import complete", state.import_status.c_str(), window);
        return false;
    }
    if (!queue_loading_project(state, result.project_dir.string(), CommandSource::widget, true)) {
        state.import_status += ". Another project is already opening.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not open imported project", state.import_status.c_str(), window);
        return false;
    }
    return true;
}

std::string_view project_load_stage_message(
    nw::kernel::ModuleLoadProgressStage stage) noexcept
{
    using Stage = nw::kernel::ModuleLoadProgressStage;
    switch (stage) {
    case Stage::reset_services:
        return "Preparing module services...";
    case Stage::load_module_resource:
        return "Loading module resources...";
    case Stage::load_dependencies:
        return "Loading HAK and TLK dependencies...";
    case Stage::initialize_services:
        return "Initializing module data...";
    case Stage::instantiate_module:
        return "Instantiating module objects...";
    case Stage::complete:
        return "Finalizing project...";
    }
    return "Opening project...";
}

void append_loading_home_markup(std::string& content_markup, const LoadingViewState& state)
{
    content_markup += "<div class=\"home_project_actions\">"
                      "<button id=\"home_import_module\"";
    if (state.project_import.active() || state.module_dialog_open) {
        content_markup += " disabled";
    }
    content_markup += ">Import Module...</button>"
                      "<button id=\"home_open_project\">Open Project...</button></div>";
    if (state.import_panel_open) {
        const bool busy = state.project_import.active() || state.module_dialog_open;
        const auto destination = state.import_parent_dir.empty() || state.import_module_path.empty()
            ? std::string{}
            : (state.import_parent_dir / std::filesystem::path{state.import_module_path}.stem()).string();
        content_markup += "<div class=\"home_import_panel\"><div class=\"home_section_title\">Import Module</div>";
        const std::array<std::array<std::string_view, 3>, 2> fields{{
            {"Source", state.import_module_path, "home_import_source"},
            {"Destination", destination, "home_import_destination"},
        }};
        for (const auto& field : fields) {
            content_markup += "<div class=\"home_import_row\"><div class=\"home_import_label\">";
            content_markup += field[0];
            content_markup += "</div><div class=\"home_import_path\" title=\"";
            content_markup += escape_html(field[1]);
            content_markup += "\">";
            content_markup += field[1].empty() ? "Not selected" : escape_html(field[1]);
            content_markup += "</div><button id=\"";
            content_markup += field[2];
            content_markup += busy ? "\" disabled>Browse...</button></div>" : "\">Browse...</button></div>";
        }
        content_markup += "<div class=\"home_import_hint\">Choose a destination parent folder. "
                          "A new folder named after the module will be created inside it; existing folders are not overwritten.</div>"
                          "<div class=\"home_project_actions\"><button id=\"home_import_start\"";
        if (busy || destination.empty()) { content_markup += " disabled"; }
        content_markup += ">Import</button><button id=\"home_import_close\"";
        if (busy) { content_markup += " disabled"; }
        content_markup += ">Close</button></div>";
        if (state.project_import.active()) {
            content_markup += "<div class=\"home_import_progress\" title=\"Import in progress\">"
                              "<div class=\"home_import_progress_fill\"></div></div>";
        }
        content_markup += "</div>";
    }
    content_markup += "<div class=\"home_import_status\">";
    content_markup += state.import_status.empty()
        ? "Import a .mod file into a new editable project, or open an existing project folder."
        : escape_html(state.import_status);
    content_markup += "</div>";
}

} // namespace nw::toolset
