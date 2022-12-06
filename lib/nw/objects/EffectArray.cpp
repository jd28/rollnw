#include "EffectArray.hpp"

#include <algorithm>

namespace nw {

EffectArray::iterator EffectArray::begin() { return effects_.begin(); }
EffectArray::const_iterator EffectArray::begin() const { return effects_.begin(); }

EffectArray::iterator EffectArray::end() { return effects_.end(); }
EffectArray::const_iterator EffectArray::end() const { return effects_.end(); }

bool EffectArray::add(Effect* effect)
{
    if (!effect) { return false; }
    auto needle = effect->handle();

    auto it = std::lower_bound(std::begin(effects_), std::end(effects_), needle);
    effects_.insert(it, needle);

    return true;
}

void EffectArray::erase(EffectArray::iterator first, EffectArray::iterator last)
{
    effects_.erase(first, last);
}

bool EffectArray::remove(Effect* effect)
{
    if (!effect) { return false; }
    auto needle = effect->handle();
    int count = 0;

    auto it = std::remove_if(std::begin(effects_), std::end(effects_), [&count, needle](const EffectHandle& handle) {
        if (needle == handle) {
            ++count;
            return true;
        }
        return false;
    });
    effects_.erase(it, std::end(effects_));
    return count > 0;
}

size_t EffectArray::size() const noexcept
{
    return effects_.size();
}

} // namespace nw
