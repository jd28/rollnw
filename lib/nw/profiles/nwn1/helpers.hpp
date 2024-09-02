#pragma once

#include "../../rules/Class.hpp"
#include "../../rules/Spell.hpp"
#include "../../rules/attributes.hpp"
#include "../../rules/combat.hpp"
#include "../../rules/feats.hpp"
#include "../../rules/items.hpp"
#include "../../rules/system.hpp"

namespace nwn1 {

namespace qual {

nw::Qualifier ability(nw::Ability id, int min, int max = 0);
nw::Qualifier alignment(nw::AlignmentAxis axis, nw::AlignmentFlags flags);
nw::Qualifier base_attack_bonus(int min, int max = 0);
nw::Qualifier class_level(nw::Class id, int min, int max = 0);
nw::Qualifier level(int min, int max = 0);
nw::Qualifier feat(nw::Feat id);
nw::Qualifier race(nw::Race id);
nw::Qualifier skill(nw::Skill id, int min, int max = 0);

} // namespace qual

// Convenience functions
namespace sel {

nw::Selector ability(nw::Ability id);
nw::Selector alignment(nw::AlignmentAxis id);
nw::Selector base_attack_bonus();
// nw::Selector armor_class(nw::Index id, bool base = false);
nw::Selector class_level(nw::Class id);
nw::Selector feat(nw::Feat id);
nw::Selector level();
nw::Selector local_var_int(std::string_view var);
nw::Selector local_var_str(std::string_view var);
nw::Selector skill(nw::Skill id);
nw::Selector race();

} // namespace sel

} // namespace nwn1
