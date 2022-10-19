#include "MasterFeat.hpp"

namespace nw {

const ModifierVariant& MasterFeatRegistry::get_bonus(MasterFeat mfeat) const
{
    return bonuses_[*mfeat];
}

void MasterFeatRegistry::clear() noexcept
{
    entries_.clear();
}

void MasterFeatRegistry::set_bonus(MasterFeat mfeat, ModifierVariant bonus)
{
    if (mfeat == MasterFeat::invalid()) return;

    if (bonuses_.size() <= mfeat.idx()) {
        bonuses_.resize(mfeat.idx() + 1);
    }

    bonuses_[mfeat.idx()] = std::move(bonus);
}

} // namespace nw
