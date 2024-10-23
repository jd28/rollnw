#pragma once

#include "../formats/StaticTwoDA.hpp"
#include "../resources/Resource.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

#include <memory>

namespace nw::kernel {

struct TwoDACache : public Service {
    const static std::type_index type_index;

    TwoDACache(MemoryResource* memory);
    TwoDACache(const TwoDACache&) = delete;
    TwoDACache(TwoDACache&&) = default;
    TwoDACache& operator=(const TwoDACache&) = delete;
    TwoDACache& operator=(TwoDACache&&) = default;

    /// Clears the cache
    void clear();

    /// Gets a cached TwoDA
    const StaticTwoDA* get(StringView tda);

    /// Gets a cached TwoDA
    const StaticTwoDA* get(const nw::Resource& tda);

private:
    absl::flat_hash_map<Resource, std::unique_ptr<StaticTwoDA>> cached_2das_;
};

inline TwoDACache& twodas()
{
    auto res = services().get_mut<TwoDACache>();
    if (!res) {
        throw std::runtime_error("kernel: unable to twoda cache service");
    }
    return *res;
}

} // namespace nw::kernel
