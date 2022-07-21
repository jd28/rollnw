#pragma once

#include "system.hpp"
#include "type_traits.hpp"

namespace nw {

enum struct ModifierType : int32_t {
    invalid = -1,
};

constexpr ModifierType make_modifier_type(int32_t id) { return static_cast<ModifierType>(id); }

template <>
struct is_rule_type_base<ModifierType> : std::true_type {
};

enum struct ModifierSource {
    unknown,
    ability,
    class_,
    environment,
    feat,
    race,
    situation,
    skill,
};

using ModifierResult = Variant<int, float>;
using ModifierFunction = std::function<ModifierResult(flecs::entity)>;
using ModifierVariant = Variant<int, float, ModifierFunction>;

struct Modifier {
    ModifierType type = ModifierType::invalid;
    ModifierVariant value;
    InternedString tagged;
    ModifierSource source = ModifierSource::unknown;
    Requirement requirement = Requirement{};
    Versus versus = {};
    int subtype = -1;
};

inline bool operator<(const Modifier& lhs, const Modifier& rhs)
{
    return std::tie(lhs.type, lhs.type, lhs.source) < std::tie(rhs.type, rhs.type, rhs.source);
}
} // namespace nw
