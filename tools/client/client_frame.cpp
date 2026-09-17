#include "client_frame.hpp"

#include <algorithm>

namespace nw::toolset {

bool advance_client_frames(std::span<ClientFrameClock> clocks,
    std::span<const ClientFrameSample> samples, std::span<ClientFrameDelta> deltas) noexcept
{
    if (clocks.size() != samples.size() || samples.size() != deltas.size()
        || std::ranges::any_of(samples, [](const auto& sample) { return sample.frequency == 0; })) {
        std::ranges::fill(deltas, ClientFrameDelta{});
        return false;
    }
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& sample = samples[i];
        const auto& previous = clocks[i];
        const auto ticks = sample.ticks >= previous.ticks ? sample.ticks - previous.ticks : 0;
        const float seconds = previous.counter != 0 && sample.counter > previous.counter
            ? static_cast<float>(static_cast<double>(sample.counter - previous.counter) / static_cast<double>(sample.frequency))
            : 0.0f;
        deltas[i] = {seconds, static_cast<int32_t>(std::min<uint64_t>(ticks, 100))};
        clocks[i] = {sample.ticks, sample.counter};
    }
    return true;
}

} // namespace nw::toolset
