#include "blueprint_update_job.hpp"

#include <SDL3/SDL.h>

namespace nw::toolset {
void BlueprintUpdateJob::ProcessDeleter::operator()(SDL_Process* process) const noexcept { SDL_DestroyProcess(process); }

bool BlueprintUpdateJob::start(const std::filesystem::path& executable,
    const std::filesystem::path& operation, std::string_view phase, std::string& error)
{
    error.clear();
    if (active() || (phase != "prepare" && phase != "commit" && phase != "restore")) {
        error = "Invalid or already running blueprint operation";
        return false;
    }
    try {
        const auto client = std::filesystem::canonical(executable);
        const auto directory = std::filesystem::canonical(operation);
        const auto log_path = directory / (std::string{phase} + ".log");
        std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> log{SDL_IOFromFile(log_path.string().c_str(), "ab"), SDL_CloseIO};
        if (!log) {
            error = SDL_GetError();
            return false;
        }
        const auto executable_text = client.string();
        const auto operation_text = directory.string();
        const std::string phase_text{phase};
        const char* args[]{executable_text.c_str(), "blueprint-update", operation_text.c_str(), phase_text.c_str(), nullptr};
        const auto props = SDL_CreateProperties();
        const bool configured = props
            && SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args)
            && SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, client.parent_path().string().c_str())
            && SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT)
            && SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, log.get())
            && SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
        if (configured) { process_.reset(SDL_CreateProcessWithProperties(props)); }
        SDL_DestroyProperties(props);
        if (!process_) { error = "Cannot start blueprint worker: " + std::string{SDL_GetError()}; }
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    return active();
}

std::optional<int> BlueprintUpdateJob::poll()
{
    int exit_code = 0;
    if (!process_ || !SDL_WaitProcess(process_.get(), false, &exit_code)) { return std::nullopt; }
    process_.reset();
    return exit_code;
}
} // namespace nw::toolset
