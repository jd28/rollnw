#include "feats.hpp"

#include "nlohmann/json.hpp"

#include <utility>

namespace nw {

// -- Feats -------------------------------------------------------------------
// ----------------------------------------------------------------------------

DEFINE_RULE_TYPE(Feat);
DEFINE_RULE_TYPE(MasterFeat);

// -- Master Feats ------------------------------------------------------------
// ----------------------------------------------------------------------------

MasterFeatRegistry::MasterFeatRegistry(MemoryResource* allocator)
    : allocator_(allocator)
{
}

const ModifierVariant& MasterFeatRegistry::get_bonus(MasterFeat mfeat) const
{
    return bonuses_[*mfeat];
}

void MasterFeatRegistry::clear() noexcept
{
    entries_.clear();
    bonuses_.clear();
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
