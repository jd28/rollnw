#include "project_import.hpp"

#include <nw/kernel/Kernel.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>

namespace nw::toolset {

void ProjectImportJob::ProcessDeleter::operator()(SDL_Process* process) const noexcept
{
    SDL_DestroyProcess(process);
}

bool ProjectImportJob::start(const std::filesystem::path& executable,
    const std::filesystem::path& module,
    const std::filesystem::path& parent, std::string& error)
{
    namespace fs = std::filesystem;
    error.clear();
    if (active()) {
        error = "A module import is already running";
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(module, ec)) {
        error = "Module file does not exist: " + module.string();
        return false;
    }
    if (!fs::is_regular_file(executable, ec)) {
        error = "Client executable does not exist: " + executable.string();
        return false;
    }
    if (!fs::is_directory(parent, ec)) {
        error = "Choose an existing parent folder for the new project";
        return false;
    }
    const auto source = fs::absolute(module, ec);
    if (ec) {
        error = "Cannot resolve the module path: " + ec.message();
        return false;
    }
    const auto client = fs::absolute(executable, ec);
    if (ec) {
        error = "Cannot resolve the client executable: " + ec.message();
        return false;
    }
    const auto name = module.stem();
    if (name.empty() || name == "." || name == "..") {
        error = "The module filename does not provide a valid project folder name";
        return false;
    }
    const auto destination = fs::absolute(parent / name, ec);
    if (ec) {
        error = "Cannot resolve the project destination: " + ec.message();
        return false;
    }
    const auto install = fs::absolute(nw::kernel::config().install_path(), ec).string();
    if (ec) {
        error = "Cannot resolve the NWN installation: " + ec.message();
        return false;
    }
    const auto user = fs::absolute(nw::kernel::config().user_path(), ec).string();
    if (ec) {
        error = "Cannot resolve the NWN user directory: " + ec.message();
        return false;
    }
    // Reserve a new directory. Existing files, directories and symlinks all
    // reject; the interactive import never updates an existing project.
    if (!fs::create_directory(destination, ec)) {
        error = "Cannot create a new project at " + destination.string()
            + ". Choose another parent folder; existing destinations are not overwritten.";
        if (ec) { error += " " + ec.message(); }
        return false;
    }
    const auto log_path = destination / "import.log";
    std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> log{
        SDL_IOFromFile(log_path.string().c_str(), "wb"), SDL_CloseIO};
    if (!log) {
        error = "Cannot create import log: " + std::string{SDL_GetError()}
            + ". The new folder remains at " + destination.string();
        return false;
    }
    const std::string executable_text = client.string();
    const std::string module_text = source.string();
    const std::string destination_text = destination.string();
    const char* args[] = {executable_text.c_str(), "import", "--json",
        module_text.c_str(), destination_text.c_str(), nullptr};
    std::unique_ptr<SDL_Environment, decltype(&SDL_DestroyEnvironment)> environment{
        SDL_CreateEnvironment(true), SDL_DestroyEnvironment};
    const auto props = SDL_CreateProperties();
    const bool configured = props && environment
        && SDL_SetEnvironmentVariable(environment.get(), "NWN_ROOT", install.c_str(), true)
        && SDL_SetEnvironmentVariable(environment.get(), "NWN_HOME", user.c_str(), true)
        && SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args)
        // Use the installed packages, not another stdlib in the editor's cwd.
        && SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, client.parent_path().string().c_str())
        && SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment.get())
        && SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT)
        && SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, log.get())
        && SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    if (configured) {
        process_.reset(SDL_CreateProcessWithProperties(props));
    }
    if (!process_) {
        error = "Could not start import: " + std::string{SDL_GetError()}
            + ". The new folder remains at " + destination.string();
    }
    SDL_DestroyProperties(props);
    if (!process_) { return false; }
    project_dir_ = destination;
    return true;
}

std::optional<ProjectImportCompletion> ProjectImportJob::poll()
{
    int exit_code = 0;
    if (!process_ || !SDL_WaitProcess(process_.get(), false, &exit_code)) {
        return std::nullopt;
    }
    process_.reset();
    ProjectImportCompletion result;
    result.ok = exit_code == 0;
    result.project_dir = std::move(project_dir_);
    const auto log_path = result.project_dir / "import.log";
    if (result.ok) {
        result.message = "Imported project: " + result.project_dir.string();
    } else {
        result.message = "Import failed (exit " + std::to_string(exit_code)
            + "). Any partial output remains in " + result.project_dir.string()
            + ".\nSee " + log_path.string();
        // Diagnostics are cold, bounded UI text. The complete log stays on disk.
        std::ifstream log{log_path, std::ios::binary | std::ios::ate};
        const auto size = log.tellg();
        if (log && size > 0) {
            const auto count = std::min<std::streamoff>(size, 4096);
            std::string tail(static_cast<size_t>(count), '\0');
            log.seekg(-count, std::ios::end);
            log.read(tail.data(), count);
            tail.resize(static_cast<size_t>(log.gcount()));
            result.message += "\n\n" + tail;
        }
    }
    return result;
}

} // namespace nw::toolset
