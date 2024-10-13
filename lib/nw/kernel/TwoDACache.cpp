#include "TwoDACache.hpp"

#include "Resources.hpp"

namespace nw::kernel {

const std::type_index TwoDACache::type_index{typeid(TwoDACache)};

TwoDACache::TwoDACache(MemoryResource* memory)
    : Service(memory)
{
}

void TwoDACache::clear()
{
    cached_2das_.clear();
}

const TwoDA* TwoDACache::get(StringView tda)
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
