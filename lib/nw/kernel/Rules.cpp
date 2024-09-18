#include "Rules.hpp"

#include "../util/string.hpp"
#include "GameProfile.hpp"
#include "TwoDACache.hpp"

#include <cstddef>

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
    modifiers.clear();
    baseitems.clear();
    classes.clear();
    feats.clear();
    races.clear();
    spells.clear();
    skills.clear();
    master_feats.clear();
    appearances.clear();
    phenotypes.clear();
    traps.clear();
    placeables.clear();
}

void Rules::initialize(ServiceInitTime time)
{
    if (time != ServiceInitTime::kernel_start && time != ServiceInitTime::module_post_load) {
        return;
    }

    LOG_F(INFO, "kernel: rules system initializing...");
    if (auto profile = services().profile()) {
        profile->load_rules();
    }
}

const AttackFuncs& Rules::attack_functions() const noexcept
{
    return attack_functions_;
}

CombatModeFuncs Rules::combat_mode(CombatMode mode)
{
    auto it = combat_modes_.find(*mode);
    if (it == std::end(combat_modes_)) { return {}; }
    return it->second;
}

bool Rules::match(const Qualifier& qual, const ObjectBase* obj) const
{
    if (qual.type.idx() >= qualifiers_.size()) {
        return true;
    }
    auto fd = qualifiers_[qual.type.idx()];
    return fd(qual, obj);
}

bool Rules::meets_requirement(const Requirement& req, const ObjectBase* obj) const
{
    for (const auto& q : req.qualifiers) {
        if (req.conjunction) {
            if (!match(q, obj)) {
                return false;
            }
        } else if (match(q, obj)) {
            return true;
        }
    }
    return true;
}

void Rules::register_combat_mode(CombatModeFuncs callbacks, std::initializer_list<CombatMode> modes)
{
    for (auto mode : modes) {
        combat_modes_[*mode] = callbacks;
    }
}

void Rules::register_special_attack(SpecialAttack type, SpecialAttackFuncs funcs)
{
    special_attacks_[*type] = funcs;
}

void Rules::set_attack_functions(AttackFuncs cbs)
{
    attack_functions_ = cbs;
}

void Rules::set_qualifier(ReqType type, bool (*qualifier)(const Qualifier&, const ObjectBase*))
{
    if (type.idx() >= qualifiers_.size()) {
        qualifiers_.resize(type.idx() + 1);
    }
    if (qualifier) { qualifiers_[type.idx()] = qualifier; };
}

SpecialAttackFuncs Rules::special_attack(SpecialAttack type)
{
    auto it = special_attacks_.find(*type);
    if (it == std::end(special_attacks_)) { return {}; }
    return it->second;
}

} // namespace nw::kernel
