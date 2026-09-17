#pragma once

namespace nw::toolset {

// Flat source-ownership protocol shared by input routing and PC sampling.
// Claimed sources contribute no held intent; pointer acquisition also discards
// their pending edges/deltas. No device, DOM or session ownership is retained.
struct PcSourceEligibility {
    bool keyboard = true;
    bool controller = true;
    bool pointer = true;
};

} // namespace nw::toolset
