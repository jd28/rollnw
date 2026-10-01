#include "client_edit_trace.hpp"

#include <nlohmann/json.hpp>

namespace nw::toolset {
namespace {
thread_local ClientEditFrame* current_frame = nullptr;
constexpr std::array phase_names{
    "input", "command", "queries", "mutations", "scene", "snapshots",
    "details", "variables", "classes", "feats", "spells", "inventory",
    "workspace", "layout", "rml_update", "ui_render", "item_preview", "item_models", "viewport", "present"};
static_assert(phase_names.size() == static_cast<size_t>(ClientEditPhase::count));
} // namespace

ScopedClientEditPhase::ScopedClientEditPhase(ClientEditPhase phase)
    : frame_{current_frame}
    , phase_{phase}
{
    if (frame_) { start_ = std::chrono::steady_clock::now(); }
}

ScopedClientEditPhase::~ScopedClientEditPhase()
{
    if (!frame_) { return; }
    const auto index = static_cast<size_t>(phase_);
    if (index >= frame_->nanoseconds.size()) { return; }
    frame_->nanoseconds[index] += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start_)
                                      .count();
    ++frame_->calls[index];
}

ClientEditTrace::ClientEditTrace(const char* path)
    : enabled_{path && *path}
{
    if (enabled_) { output_.open(path); }
}

ClientEditTrace::~ClientEditTrace()
{
    if (current_frame == &frame_) { current_frame = nullptr; }
}

bool ClientEditTrace::valid() const noexcept
{
    return !enabled_ || output_.good();
}

void ClientEditTrace::begin_frame(uint64_t epoch)
{
    if (!enabled_) { return; }
    frame_ = {};
    epoch_ = epoch;
    start_ = std::chrono::steady_clock::now();
    current_frame = &frame_;
}

bool ClientEditTrace::end_frame(uint64_t epoch, bool presented, int width, int height)
{
    if (!enabled_) { return true; }
    const auto end = std::chrono::steady_clock::now();
    current_frame = nullptr;
    if (epoch == epoch_) { return valid(); }
    nlohmann::json phases = nlohmann::json::object();
    nlohmann::json calls = nlohmann::json::object();
    for (size_t index = 0; index < phase_names.size(); ++index) {
        phases[phase_names[index]] = static_cast<double>(frame_.nanoseconds[index]) / 1e6;
        calls[phase_names[index]] = frame_.calls[index];
    }
    const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start_).count();
    const nlohmann::json row{
        {"schema", 1}, {"sequence", sequence_++}, {"epoch_before", epoch_}, {"epoch_after", epoch},
        {"width", width}, {"height", height}, {"present_returned", presented},
        {"dispatch_to_present_ms", presented ? nlohmann::json(elapsed_ms) : nlohmann::json(nullptr)},
        {"display_latency_ms", nullptr}, {"inclusive_ms", phases}, {"calls", calls}};
    output_ << row.dump() << '\n';
    output_.flush();
    return valid();
}

} // namespace nw::toolset
