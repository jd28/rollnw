#include "rule_helper_funcs.hpp"

#include "../../kernel/Kernel.hpp"

namespace nwn1 {

namespace mod {

#define DEFINE_MOD(name, type)                                              \
    nw::Modifier name(nw::ModifierVariant value, nw::Requirement req,       \
        nw::Versus versus, std::string_view tag, nw::ModifierSource source) \
    {                                                                       \
        return nw::Modifier{                                                \
            type,                                                           \
            value,                                                          \
            req,                                                            \
            versus,                                                         \
            nw::kernel::strings().intern(tag),                              \
            source,                                                         \
        };                                                                  \
    }

DEFINE_MOD(ac_dodge, nw::ModifierType::ac_dodge)
DEFINE_MOD(ac_natural, nw::ModifierType::ac_natural)
DEFINE_MOD(hitpoints, nw::ModifierType::hitpoints)

} // namespace mod

namespace qual {

nw::Qualifier ability(nw::Index id, int min, int max)
{
    nw::Qualifier q;
    q.selector = sel::ability(id);
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

nw::Qualifier alignment(nw::AlignmentAxis axis, nw::AlignmentFlags flags)
{
    nw::Qualifier q;
    q.selector = sel::alignment(axis);
    q.params.push_back(static_cast<int32_t>(flags));
    return q;
}

nw::Qualifier class_level(nw::Index id, int min, int max)
{
    nw::Qualifier q;
    q.selector = sel::class_level(id);
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

nw::Qualifier feat(nw::Index id)
{
    nw::Qualifier q;
    q.selector = sel::feat(id);
    return q;
}

nw::Qualifier race(nw::Index id)
{
    nw::Qualifier q;
    q.selector = sel::race();
    q.params.push_back(id);
    return q;
}

nw::Qualifier skill(nw::Index id, int min, int max)
{
    nw::Qualifier q;
    q.selector = sel::skill(id);
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

nw::Qualifier level(int min, int max)
{
    nw::Qualifier q;
    q.selector = sel::level();
    q.params.push_back(min);
    q.params.push_back(max);
    return q;
}

} // namespace qual

namespace sel {

nw::Selector ability(nw::Index id)
{
    return {nw::SelectorType::ability, id};
}

nw::Selector alignment(nw::AlignmentAxis id)
{
    return {nw::SelectorType::alignment, static_cast<int32_t>(id)};
}

nw::Selector class_level(nw::Index id)
{
    return {nw::SelectorType::class_level, id};
}

nw::Selector feat(nw::Index id)
{
    return {nw::SelectorType::feat, id};
}

nw::Selector level()
{
    return {nw::SelectorType::level, {}};
}

nw::Selector local_var_int(std::string_view var)
{
    return {nw::SelectorType::local_var_int, std::string(var)};
}

nw::Selector local_var_str(std::string_view var)
{
    return {nw::SelectorType::local_var_str, std::string(var)};
}

nw::Selector skill(nw::Index id)
{
    return {nw::SelectorType::skill, id};
}

nw::Selector race()
{
    return {nw::SelectorType::race, {}};
}

} // namespace sel
} // namespace nwn1
