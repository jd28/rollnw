#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>

namespace nw::toolset {

enum class ClientEditPhase : uint8_t {
    input,
    command,
    queries,
    mutations,
    scene,
    snapshots,
    details,
    variables,
    classes,
    feats,
    spells,
    inventory,
    workspace,
    layout,
    rml_update,
    ui_render,
    item_preview,
    item_models,
    viewport,
    present,
    count,
};

struct ClientEditFrame {
    std::array<uint64_t, static_cast<size_t>(ClientEditPhase::count)> nanoseconds{};
    std::array<uint32_t, static_cast<size_t>(ClientEditPhase::count)> calls{};
};

// Optional diagnostics for the one desktop frame stream. The trace and all
// scopes live on the UI thread; feature scopes borrow only the current frame.
// Phases are inclusive and may nest. No clocks/allocations when tracing is off.
class ScopedClientEditPhase {
public:
    explicit ScopedClientEditPhase(ClientEditPhase phase);
    ~ScopedClientEditPhase();
    ScopedClientEditPhase(const ScopedClientEditPhase&) = delete;
    ScopedClientEditPhase& operator=(const ScopedClientEditPhase&) = delete;

private:
    ClientEditFrame* frame_ = nullptr;
    ClientEditPhase phase_;
    std::chrono::steady_clock::time_point start_;
};

// Empty path disables tracing. An invalid output fails before the first frame.
// Each changed frame emits schema-1 JSONL after its measured work. A skipped
// surface explicitly reports unavailable presentation, never a zero duration.
// Epochs are opaque monotonic mutation identities; no object pointers persist.
class ClientEditTrace {
public:
    explicit ClientEditTrace(const char* path);
    ~ClientEditTrace();
    ClientEditTrace(const ClientEditTrace&) = delete;
    ClientEditTrace& operator=(const ClientEditTrace&) = delete;
    [[nodiscard]] bool valid() const noexcept;
    void begin_frame(uint64_t epoch);
    [[nodiscard]] bool end_frame(uint64_t epoch, bool presented, int width, int height);

private:
    std::ofstream output_;
    bool enabled_ = false;
    ClientEditFrame frame_;
    uint64_t epoch_ = 0;
    uint64_t sequence_ = 0;
    std::chrono::steady_clock::time_point start_;
};

} // namespace nw::toolset
