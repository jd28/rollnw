#pragma once

#include "../util/InternedString.hpp"
#include "rule_type.hpp"

#include <absl/container/flat_hash_map.h>

#include <limits>
#include <vector>

namespace nw {

DECLARE_RULE_TYPE(Ability)

// Since there is no 2da for abilities, this is a placeholder.

struct AbilityInfo {
    uint32_t name = 0xFFFFFFFF;
    InternedString constant;

    bool valid() const noexcept { return name != 0xFFFFFFFF; }
};

/// Ability singleton component
struct AbilityArray {
    using map_type = absl::flat_hash_map<
        InternedString,
        Ability,
        InternedStringHash,
        InternedStringEq>;

    const AbilityInfo* get(Ability ability) const noexcept;
    bool is_valid(Ability ability) const noexcept;
    Ability from_constant(std::string_view constant) const;

    std::vector<AbilityInfo> entries;
    map_type constant_to_index;
};

} // namespace nw
