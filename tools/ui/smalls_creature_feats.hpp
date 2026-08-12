#pragma once

#include <nw/objects/ObjectHandle.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nw::smalls {
struct Runtime;
}

namespace nw::toolset {

struct CreatureFeatTextSlice {
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct CreatureFeatRow {
    uint32_t feat_id = 0;
    CreatureFeatTextSlice name;
    bool assigned = false;
};

enum class CreatureFeatViewStatus : uint8_t {
    empty,
    ready,
    invalid_object,
    invalid_data,
};

struct CreatureFeatViewSnapshot {
    ObjectHandle object{};
    CreatureFeatViewStatus status = CreatureFeatViewStatus::empty;
    std::vector<CreatureFeatRow> rows;
    std::string text;
    std::string diagnostic;
    uint32_t assigned_count = 0;

    [[nodiscard]] std::string_view text_view(CreatureFeatTextSlice slice) const noexcept;
};

// Builds the visible rules projection from the live CreatureStats feat array.
// The query is ASCII case-insensitive; an empty query includes every valid rule.
void build_creature_feat_rows(smalls::Runtime& runtime,
    ObjectHandle active_object,
    std::string_view query,
    CreatureFeatViewSnapshot& output);

} // namespace nw::toolset
