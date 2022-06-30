#pragma once

#include "../util/InternedString.hpp"
#include "type_traits.hpp"

#include <absl/container/flat_hash_map.h>

#include <vector>

namespace nw {

enum struct Ability : int32_t {
    invalid = -1,
};
constexpr Ability make_ability(int32_t id) { return static_cast<Ability>(id); }

template <>
struct is_rule_type_base<Ability> : std::true_type {
};

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
