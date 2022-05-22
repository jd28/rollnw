#pragma once

#include "../rules/system.hpp"

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

    /// Clears rules system of all rules and cached 2da files
    virtual void clear();

    /// Select
    RuleValue select(const Selector&, const flecs::entity) const;

    /// Set rules selector
    void set_selector(selector_type selector);

private:
    selector_type selector_;
};

} // namespace nw::kernel
