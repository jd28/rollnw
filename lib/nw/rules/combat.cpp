#include "combat.hpp"

namespace nw {

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
