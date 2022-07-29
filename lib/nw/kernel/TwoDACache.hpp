#pragma once

#include "../formats/TwoDA.hpp"
#include "../resources/Resource.hpp"
#include "Kernel.hpp"

#include <absl/container/flat_hash_map.h>

namespace nw::kernel {

struct TwoDACache : public Service {
    /// Caches a 2da
    bool cache(std::string_view tda);

    /// Clears the cache
    virtual void clear() override;

    /// Gets a cached TwoDA
    const TwoDA* get(std::string_view tda);

    /// Gets a cached TwoDA
    const TwoDA* get(const nw::Resource& tda);

private:
    absl::flat_hash_map<Resource, TwoDA> cached_2das_;
};

} // namespace nw::kernel
