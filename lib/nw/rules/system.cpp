#include "system.hpp"

#include "../objects/Creature.hpp"

namespace nw {

// == Selector ================================================================

RuleValue Selector::select(const flecs::entity cre) const
{
    switch (type) {
    default:
        return {};
    case SelectorType::ability: {
        auto stats = cre.get<CreatureStats>();
        if (!stats) { return {}; }
        auto idx = static_cast<size_t>(subtype);
        return static_cast<int>(stats->abilities[idx]);
    }
    case SelectorType::feat: {
        auto stats = cre.get<CreatureStats>();
        if (!stats) { return {}; }
        return stats->has_feat(static_cast<uint16_t>(subtype));
    }
    case SelectorType::level: {
        auto levels = cre.get<LevelStats>();
        if (!levels) { return {}; }

        int level = 0;
        for (const auto& ce : levels->classes) {
            level += ce.level;
        }
        return level;
    }
    case SelectorType::race: {
        auto c = cre.get<Creature>();
        if (!c) { return {}; }
        return static_cast<int>(c->race);
    }
    case SelectorType::skill: {
        auto stats = cre.get<CreatureStats>();
        if (!stats) { return {}; }

        auto idx = static_cast<size_t>(subtype);
        return static_cast<int>(stats->skills[idx]);
    }
    }

    return {};
}

bool operator==(const Selector& lhs, const Selector& rhs)
{
    return std::tie(lhs.type, lhs.subtype) == std::tie(rhs.type, rhs.subtype);
}

bool operator<(const Selector& lhs, const Selector& rhs)
{
    return std::tie(lhs.type, lhs.subtype) < std::tie(rhs.type, rhs.subtype);
}

namespace select {

Selector ability(Constant id)
{
    return {SelectorType::ability, id->as<int32_t>()};
}

Selector feat(Constant id)
{
    return {SelectorType::feat, id->as<int32_t>()};
}

Selector level()
{
    return {SelectorType::level, {}};
}

Selector skill(Constant id)
{
    return {SelectorType::skill, id->as<int32_t>()};
}

Selector race()
{
    return {SelectorType::race, {}};
}

} // namespace select

// == Qualifier ===============================================================

bool Qualifier::match(const flecs::entity cre) const
{
    auto value = selector.select(cre);
    if (!value.empty()) {
        switch (selector.type) {
        default:
            return false;
        case SelectorType::level: {
            auto val = value.as<int32_t>();
            auto min = params[0].as<int32_t>();
            auto max = params[1].as<int32_t>();
            if (val < min || (max != 0 && val > max)) { return false; }
            return true;
        }
        case SelectorType::feat: {
            return value.is<int32_t>() && value.as<int32_t>();
        }
        case SelectorType::race: {
            auto val = value.as<int32_t>();
            return val == params[0].as<int32_t>();
        }
        case SelectorType::skill:
        case SelectorType::ability: {
            auto val = value.as<int32_t>();
            auto min = params[0].as<int32_t>();
            auto max = params[1].as<int32_t>();
            if (val < min) { return false; }
            if (max != 0 && val > max) { return false; }
            return true;
        } break;
        }
    }
    return false;
}

namespace qualifier {

Qualifier ability(Constant id, int min, int max)
{
    Qualifier q;
    q.selector = select::ability(id);
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

Qualifier feat(Constant id)
{
    Qualifier q;
    q.selector = select::feat(id);
    return q;
}

Qualifier race(Constant id)
{
    Qualifier q;
    q.selector = select::race();
    q.params.push_back(id->as<int32_t>());
    return q;
}

Qualifier skill(Constant id, int min, int max)
{
    Qualifier q;
    q.selector = select::skill(id);
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

Qualifier level(int min, int max)
{
    Qualifier q;
    q.selector = select::level();
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

} // namespace qualifier

// == Requirement =============================================================

Requirement::Requirement(bool conjunction)
    : conjunction_{conjunction}
{
}

Requirement::Requirement(std::initializer_list<Qualifier> quals, bool conjunction)
    : conjunction_{conjunction}
{
    for (const auto& q : quals) {
        qualifiers_.push_back(q);
    }
}

void Requirement::add(Qualifier qualifier)
{
    qualifiers_.push_back(std::move(qualifier));
}

bool Requirement::met(const flecs::entity cre) const
{
    for (const auto& q : qualifiers_) {
        if (conjunction_) {
            if (!q.match(cre)) { return false; }
        } else if (q.match(cre)) {
            return true;
        }
    }
    return true;
}

} // namespace nw
