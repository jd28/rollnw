#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct SDL_Process;

namespace nw::toolset {

struct ProjectImportCompletion {
    bool ok = false;
    std::filesystem::path project_dir;
    std::string message;
};

// One UI-owned job imports the resource batch in a separate process: the CLI
// importer replaces kernel services and must not run in the editor's runtime.
class ProjectImportJob {
public:
    bool start(const std::filesystem::path& executable,
        const std::filesystem::path& module,
        const std::filesystem::path& parent, std::string& error);
    [[nodiscard]] bool active() const noexcept { return bool(process_); }
    [[nodiscard]] std::optional<ProjectImportCompletion> poll();

private:
    struct ProcessDeleter {
        void operator()(SDL_Process* process) const noexcept;
    };
    // Releasing the tracker does not interrupt file writes. Normal UI shutdown
    // waits for completion; unexpected teardown leaves the child to finish.
    std::unique_ptr<SDL_Process, ProcessDeleter> process_;
    std::filesystem::path project_dir_;
};

} // namespace nw::toolset
