#include "EffectArray.hpp"

#include <algorithm>

namespace nw {

bool EffectArray::add(Effect* effect)
{
    if (!effect) { return false; }
    auto needle = effect->handle();

    auto it = std::lower_bound(std::begin(effects_), std::end(effects_), needle);
    effects_.insert(it, needle);

    return true;
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

int EffectArray::remove(ObjectHandle obj)
{
    int count = 0;
    auto it = std::remove_if(std::begin(effects_), std::end(effects_), [&count, obj](const EffectHandle& handle) {
        if (obj == handle.effect->creator) {
            ++count;
            return true;
        }
        return false;
    });
    effects_.erase(it, std::end(effects_));
    return count;
}

size_t EffectArray::size() const noexcept
{
    return effects_.size();
}

} // namespace nw
