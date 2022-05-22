#include "TwoDACache.hpp"

#include "../Kernel.hpp"

namespace nw {

bool TwoDACache::cache(std::string_view tda)
{
    Resource res{tda, ResourceType::twoda};
    auto it = cached_2das_.find(res);
    if (it != std::end(cached_2das_) && it->second.is_valid() && it->second.rows() != 0) {
        return true;
    }

    auto t = TwoDA{kernel::resman().demand(res)};
    if (t.is_valid()) {
        cached_2das_[res] = std::move(t);
        return true;
    }
    return false;
}

void TwoDACache::clear()
{
    cached_2das_.clear();
}

const TwoDA* TwoDACache::get(std::string_view tda) const
{
    Resource res{tda, ResourceType::twoda};
    auto it = cached_2das_.find(res);
    if (it != std::end(cached_2das_)) {
        return &it->second;
    }

    return nullptr;
}

} // namespace nw
