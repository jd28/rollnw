#pragma once

#include <vector>

namespace nw {

struct LevelHistoryEntry {
};

/// Encapsulates a players level up history
struct LevelHistory {
    std::vector<LevelHistoryEntry> entries;
};

} // namespace nw
