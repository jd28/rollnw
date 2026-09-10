#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct SDL_Process;
namespace nw::toolset {

// UI-owned process tracker. Each phase processes the operation's complete batch
// in isolation; starting a new phase trades kernel startup cost for no idle child
// process, interactive pipe protocol, or shared mutable runtime.
class BlueprintUpdateJob {
public:
    bool start(const std::filesystem::path& executable, const std::filesystem::path& operation,
        std::string_view phase, std::string& error);
    [[nodiscard]] bool active() const noexcept { return bool(process_); }
    [[nodiscard]] std::optional<int> poll();

private:
    struct ProcessDeleter {
        void operator()(SDL_Process*) const noexcept;
    };
    std::unique_ptr<SDL_Process, ProcessDeleter> process_;
};
} // namespace nw::toolset
