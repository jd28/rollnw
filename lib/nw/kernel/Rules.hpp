#pragma once

#include "../formats/TwoDA.hpp"
#include "../resources/Resource.hpp"
#include "../rules/system.hpp"
#include "Strings.hpp"

#include <absl/container/flat_hash_map.h>
#include <flecs/flecs.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace nw::kernel {

struct Rules {
    using selector_type = std::function<RuleValue(const Selector&, const flecs::entity)>;

    virtual ~Rules();

    // Intializes rules system
    virtual bool initialize();

    /// Loads rules
    virtual bool load();

    /// Reloads rules
    virtual bool reload();

    /// Clears rules system of all rules and cached 2da files
    virtual void clear();

    /// Caches
    virtual bool cache_2da(const Resource& res);

    /// Gets a cached TwoDA
    virtual TwoDA& get_cached_2da(const Resource& res);

    /// Select
    RuleValue select(const Selector&, const flecs::entity) const;

    /// Set rules selector
    void set_selector(selector_type selector);

private:
    absl::flat_hash_map<Resource, TwoDA> cached_2das_;
    selector_type selector_;
};

} // namespace nw::kernel
