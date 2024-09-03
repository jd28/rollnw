#include "combat.hpp"

#include "nlohmann/json.hpp"

namespace nw {

DEFINE_RULE_TYPE(AttackType);
DEFINE_RULE_TYPE(Damage);
DEFINE_RULE_TYPE(DamageModType);
DEFINE_RULE_TYPE(Disease);
DEFINE_RULE_TYPE(MissChanceType);
DEFINE_RULE_TYPE(CombatMode);
DEFINE_RULE_TYPE(Poison);
DEFINE_RULE_TYPE(Situation);
DEFINE_RULE_TYPE(SpecialAttack);

void AttackData::add(nw::Damage type_, int amount, bool unblockable)
{
    bool found = false;
    for (auto& it : damage_) {
        if (it.type == type_) {
            found = true;
            if (unblockable) {
                it.unblocked += amount;
            } else {
                it.amount += amount;
            }
        }
    }
    if (!found) {
        DamageResult dr;
        dr.type = type_;
        if (unblockable) {
            dr.unblocked = amount;
        } else {
            dr.amount = amount;
        }
        damage_.push_back(dr);
    }
}

AttackData::DamageArray& AttackData::damages()
{
    return damage_;
}

const AttackData::DamageArray& AttackData::damages() const
{
    return damage_;
}

} // namespace nw
