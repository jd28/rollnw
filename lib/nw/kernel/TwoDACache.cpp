#include "TwoDACache.hpp"

#include "Resources.hpp"

namespace nw::kernel {

bool TwoDACache::cache(std::string_view tda)
{
    Resource res{tda, ResourceType::twoda};
    auto it = cached_2das_.find(res);
    if (it != std::end(cached_2das_) && it->second->is_valid() && it->second->rows() != 0) {
        return true;
    }

    auto t = std::make_unique<TwoDA>(kernel::resman().demand(res));
    if (t->is_valid()) {
        cached_2das_[res] = std::move(t);
        return true;
    }

    return false;
}

void TwoDACache::clear()
{
    cached_2das_.clear();
}

const TwoDA* TwoDACache::get(std::string_view tda)
{
    Resource res{tda, ResourceType::twoda};
    return get(res);
}

const TwoDA* TwoDACache::get(const Resource& tda)
{
    if (tda.type != ResourceType::twoda) {
        return nullptr;
    }
    auto it = cached_2das_.find(tda);
    if (it != std::end(cached_2das_)) {
        return it->second.get();
    } else {
        auto t = std::make_unique<TwoDA>(kernel::resman().demand(tda));
        if (t->is_valid()) {
            cached_2das_[tda] = std::move(t);
            return cached_2das_[tda].get();
        }
    }

    return nullptr;
}

} // namespace nw::kernel
