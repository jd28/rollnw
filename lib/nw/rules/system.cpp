#include "system.hpp"

#include "../objects/Creature.hpp"

namespace nw {

// == Constant ================================================================

size_t Constant::as_index() const
{
    return static_cast<size_t>(std::get<int>(value));
}

int Constant::as_int() const
{
    return std::get<int>(value);
}

float Constant::as_float() const
{
    return std::get<float>(value);
}

const std::string& Constant::as_string() const
{
    return std::get<std::string>(value);
}

bool Constant::is_int() const noexcept
{
    return std::holds_alternative<int>(value);
}

bool Constant::is_float() const noexcept
{
    return std::holds_alternative<float>(value);
}

bool Constant::is_string() const noexcept
{
    return std::holds_alternative<std::string>(value);
}

bool operator==(const Constant& lhs, const Constant& rhs)
{
    return std::tie(lhs.name, lhs.value) == std::tie(rhs.name, rhs.value);
}

// == Selector ================================================================

std::optional<int> Selector::select(const Creature& cre) const
{
    switch (type) {
    default:
        return {};
    case SelectorType::race:
        return static_cast<int>(cre.race);
    case SelectorType::ability:
        return static_cast<int>(cre.stats.abilities[subtype->as_index()]);
    case SelectorType::skill:
        return static_cast<int>(cre.stats.skills[subtype->as_index()]);
    }

    return {};
}

namespace select {

Selector ability(Constant id)
{
    return {SelectorType::ability, std::optional<Constant>{id}};
}

Selector skill(Constant id)
{
    return {SelectorType::skill, id};
}

Selector race()
{
    return {SelectorType::race, std::optional<Constant>{}};
}

} // namespace select

} // namespace nw
