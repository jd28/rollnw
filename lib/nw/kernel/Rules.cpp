#include "Rules.hpp"

#include "../util/string.hpp"
#include "Kernel.hpp"

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
    selector_ = selector_type{};
}

bool Rules::initialize()
{
    LOG_F(INFO, "kernel: initializing rules system");
    // Stub
    return true;
}

RuleValue Rules::select(const Selector& selector, const flecs::entity ent) const
{
    if (!selector_) {
        LOG_F(ERROR, "rules: no selector set");
        return {};
    }

    return selector_(selector, ent);
}

void Rules::set_selector(std::function<RuleValue(const Selector&, flecs::entity)> selector)
{
    selector_ = std::move(selector);
}

} // namespace nw::kernel
