#include "Ability.hpp"

namespace nw {

const AbilityInfo* AbilityArray::get(Ability ability) const noexcept
{
    if (ability < entries.size() && entries[ability].valid()) {
        return &entries[ability];
    }
    return nullptr;
}

bool AbilityArray::is_valid(Ability ability) const noexcept
{
    return ability < entries.size() && entries[ability].valid();
}

Ability AbilityArray::from_constant(std::string_view constant) const
{
    absl::string_view v{constant.data(), constant.size()};
    auto it = constant_to_index.find(v);
    if (it == constant_to_index.end()) {
        return Ability::invalid();
    } else {
        return it->second;
    }
}
} // namespace nw
