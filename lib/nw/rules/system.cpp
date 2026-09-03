#include "system.hpp"

#include "Class.hpp"
#include "feats.hpp"

#include <utility>

namespace nw {

// == Qualifier ===============================================================
// ============================================================================

bool match_qualifier_value(int32_t actual, QualifierMatch match, int32_t expected) noexcept
{
    switch (match) {
    case QualifierMatch::eq:
        return actual == expected;
    case QualifierMatch::ne:
        return actual != expected;
    case QualifierMatch::lt:
        return actual < expected;
    case QualifierMatch::lte:
        return actual <= expected;
    case QualifierMatch::gt:
        return actual > expected;
    case QualifierMatch::gte:
        return actual >= expected;
    case QualifierMatch::any_bits:
        return (actual & expected) != 0;
    case QualifierMatch::all_bits:
        return (actual & expected) == expected;
    case QualifierMatch::no_bits:
        return (actual & expected) == 0;
    }
    return false;
}

Qualifier make_qualifier(ReqType type, int32_t subtype, QualifierMatch match, int32_t value)
{
    return Qualifier{
        type,
        subtype,
        match,
        value,
    };
}

Qualifier qualifier_ability(nw::Ability id, int value, QualifierMatch match)
{
    return make_qualifier(nw::req_type_ability, *id, match, value);
}

Qualifier qualifier_ability(nw::Ability id, QualifierMatch match, int value)
{
    return qualifier_ability(id, value, match);
}

Qualifier qualifier_alignment(nw::AlignmentAxis axis, nw::AlignmentFlags flags)
{
    return make_qualifier(
        req_type_alignment,
        static_cast<int32_t>(axis),
        QualifierMatch::any_bits,
        static_cast<int32_t>(flags));
}

Qualifier qualifier_base_attack_bonus(int value, QualifierMatch match)
{
    return make_qualifier(req_type_bab, -1, match, value);
}

Qualifier qualifier_base_attack_bonus(QualifierMatch match, int value)
{
    return qualifier_base_attack_bonus(value, match);
}

Qualifier qualifier_class_level(nw::Class id, int value, QualifierMatch match)
{
    return make_qualifier(req_type_class_level, *id, match, value);
}

Qualifier qualifier_class_level(nw::Class id, QualifierMatch match, int value)
{
    return qualifier_class_level(id, value, match);
}

Qualifier qualifier_feat(nw::Feat id)
{
    return make_qualifier(req_type_feat, *id, QualifierMatch::eq, 1);
}

Qualifier qualifier_race(nw::Race id)
{
    return make_qualifier(req_type_race, -1, QualifierMatch::eq, *id);
}

Qualifier qualifier_skill(nw::Skill id, int value, QualifierMatch match)
{
    return make_qualifier(req_type_skill, *id, match, value);
}

Qualifier qualifier_skill(nw::Skill id, QualifierMatch match, int value)
{
    return qualifier_skill(id, value, match);
}

Qualifier qualifier_level(int value, QualifierMatch match)
{
    return make_qualifier(req_type_level, -1, match, value);
}

Qualifier qualifier_level(QualifierMatch match, int value)
{
    return qualifier_level(value, match);
}

// == Requirement =============================================================
// ============================================================================

Requirement::Requirement(bool conjunction_)
    : conjunction{conjunction_}
{
}

Requirement::Requirement(std::initializer_list<Qualifier> quals, bool conjunction_)
    : conjunction{conjunction_}
{
    for (const auto& q : quals) {
        qualifiers.push_back(q);
    }
}

void Requirement::add(Qualifier qualifier)
{
    qualifiers.push_back(std::move(qualifier));
}

size_t Requirement::size() const noexcept
{
    return qualifiers.size();
}

} // namespace nw
