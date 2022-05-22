#pragma once

#include "../../formats/TwoDA.hpp"
#include "../../resources/Resource.hpp"

#include <absl/container/flat_hash_map.h>

namespace nw {

struct TwoDACache {
    /// Caches a 2da
    bool cache(std::string_view tda);

    /// Clears the cache
    void clear();

    /// Gets a cached TwoDA
    const TwoDA* get(std::string_view tda) const;

private:
    absl::flat_hash_map<Resource, TwoDA> cached_2das_;
};

} // namespace nw
