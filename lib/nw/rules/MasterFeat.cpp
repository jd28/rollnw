#include "MasterFeat.hpp"

namespace nw {

const ModifierVariant& MasterFeatRegistry::get_bonus(MasterFeat mfeat) const
{
    auto mf = static_cast<size_t>(mfeat);
    return bonuses_[mf];
}

void MasterFeatRegistry::clear() noexcept
{
    entries_.clear();
}

void MasterFeatRegistry::set_bonus(MasterFeat mfeat, ModifierVariant bonus)
{
    if (mfeat == MasterFeat::invalid) return;

    auto mf = static_cast<size_t>(mfeat);

    if (bonuses_.size() <= mf) {
        bonuses_.resize(mf + 1);
    }

    bonuses_[mf] = std::move(bonus);
}

} // namespace nw
