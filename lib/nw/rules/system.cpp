#include "system.hpp"

#include "../kernel/Kernel.hpp"

namespace nw {

// == Selector ================================================================

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

Selector alignment(AlignmentAxis id)
{
    return {SelectorType::alignment, static_cast<int32_t>(id)};
}

Selector class_level(Constant id)
{
    return {SelectorType::class_level, id->as<int32_t>()};
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
    auto value = kernel::rules().select(selector, cre);
    if (!value.empty()) {
        switch (selector.type) {
        default:
            return false;
        case SelectorType::alignment: {
            auto target_axis = static_cast<AlignmentAxis>(selector.subtype.as<int32_t>());
            auto flags = static_cast<AlignmentFlags>(params[0].as<int32_t>());
            auto ge = 50;
            auto lc = 50;

            auto ge_sel = kernel::rules().select(select::alignment(AlignmentAxis::good_evil), cre);
            if (ge_sel.is<int32_t>()) { ge = ge_sel.as<int32_t>(); }

            auto lc_sel = kernel::rules().select(select::alignment(AlignmentAxis::law_chaos), cre);
            if (lc_sel.is<int32_t>()) { lc = lc_sel.as<int32_t>(); }

            if (!!(flags & AlignmentFlags::good) && !!(target_axis | AlignmentAxis::good_evil)) {
                if (ge > 50) { return true; }
            }

            if (!!(flags & AlignmentFlags::evil) && !!(target_axis | AlignmentAxis::good_evil)) {
                if (ge < 50) { return true; }
            }

            if (!!(flags & AlignmentFlags::lawful) && !!(target_axis | AlignmentAxis::law_chaos)) {
                if (lc > 50) { return true; }
            }

            if (!!(flags & AlignmentFlags::chaotic) && !!(target_axis | AlignmentAxis::law_chaos)) {
                if (lc < 50) { return true; }
            }

            if (!!(flags & AlignmentFlags::neutral)) {
                if (target_axis == AlignmentAxis::both) {
                    return ge == 50 && lc == 50;
                }
                if (target_axis == AlignmentAxis::good_evil) {
                    return ge == 50;
                }
                if (target_axis == AlignmentAxis::law_chaos) {
                    return lc == 50;
                }
            }
        } break;
        case SelectorType::class_level:
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

Qualifier alignment(AlignmentAxis axis, AlignmentFlags flags)
{
    Qualifier q;
    q.selector = select::alignment(axis);
    q.params.push_back(static_cast<int32_t>(flags));
    return q;
}

Qualifier class_level(Constant id, int min, int max)
{
    Qualifier q;
    q.selector = select::class_level(id);
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
