#pragma once

#include "../../rules/system.hpp"

namespace nwn1 {

namespace mod {

#define DECLARE_MOD(name)                                                                 \
    nw::Modifier name(nw::ModifierVariant value, nw::Requirement req = nw::Requirement{}, \
        nw::Versus versus = {}, std::string_view tag = {},                                \
        nw::ModifierSource source = nw::ModifierSource::unknown)

DECLARE_MOD(ac_dodge);
DECLARE_MOD(ac_natural);
DECLARE_MOD(hitpoints);

#undef DECLARE_MOD

} // namespace mod

namespace qual {

nw::Qualifier ability(nw::Index id, int min, int max = 0);
nw::Qualifier alignment(nw::AlignmentAxis axis, nw::AlignmentFlags flags);
nw::Qualifier class_level(nw::Index id, int min, int max = 0);
nw::Qualifier level(int min, int max = 0);
nw::Qualifier feat(nw::Index id);
nw::Qualifier race(nw::Index id);
nw::Qualifier skill(nw::Index id, int min, int max = 0);

} // namespace qual

// Convenience functions
namespace sel {

nw::Selector ability(nw::Index id);
nw::Selector alignment(nw::AlignmentAxis id);
// nw::Selector armor_class(nw::Index id, bool base = false);
nw::Selector class_level(nw::Index id);
nw::Selector feat(nw::Index id);
nw::Selector level();
nw::Selector local_var_int(std::string_view var);
nw::Selector local_var_str(std::string_view var);
nw::Selector skill(nw::Index id);
nw::Selector race();

} // namespace sel

} // namespace nwn1
