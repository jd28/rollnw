#pragma once

#include <cstdint>
#include <span>

namespace nw::toolset {

// In-process schema 1. Caller owns matching contiguous rows for one advancement.
// Production has one process frame clock; batches describe independent clocks.
struct ClientFrameClock {
    uint64_t ticks = 0;
    uint64_t counter = 0;
};
struct ClientFrameSample {
    uint64_t ticks = 0;
    uint64_t counter = 0;
    uint64_t frequency = 0;
};
struct ClientFrameDelta {
    float raw_seconds = 0;
    int32_t camera_milliseconds = 0;
};
// First counter (previous=0) and regressing timestamps yield zero differences.
// Camera milliseconds clamp to 100; raw seconds retain the full counter delta.
// Zero frequency or mismatched spans clears outputs, leaves all clocks unchanged
// and rejects the batch. Timestamps accept the full uint64 range.
[[nodiscard]] bool advance_client_frames(std::span<ClientFrameClock>,
    std::span<const ClientFrameSample>, std::span<ClientFrameDelta>) noexcept;

} // namespace nw::toolset
