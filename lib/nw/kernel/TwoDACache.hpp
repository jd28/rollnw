#pragma once

#include "../formats/TwoDA.hpp"
#include "../resources/Resource.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

#include <memory>

namespace nw::kernel {

struct TwoDACache : public Service {
    TwoDACache() = default;
    TwoDACache(const TwoDACache&) = delete;
    TwoDACache(TwoDACache&&) = default;
    TwoDACache& operator=(const TwoDACache&) = delete;
    TwoDACache& operator=(TwoDACache&&) = default;

    /// Caches a 2da
    bool cache(std::string_view tda);

    /// Clears the cache
    virtual void clear() override;

    /// Gets a cached TwoDA
    const TwoDA* get(std::string_view tda);

    /// Gets a cached TwoDA
    const TwoDA* get(const nw::Resource& tda);

private:
    absl::flat_hash_map<Resource, std::unique_ptr<TwoDA>> cached_2das_;
};

inline TwoDACache& twodas()
{
    auto res = services().twoda_cache.get();
    if (!res) {
        LOG_F(FATAL, "kernel: unable to load twoda cache service");
    }
    return *res;
}

} // namespace nw::kernel
