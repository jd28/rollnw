#include "Rules.hpp"

#include "../formats/TwoDA.hpp"
#include "../util/string.hpp"
#include "Kernel.hpp"

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
    cached_2das_.clear();
}

bool Rules::initialize()
{
    // Stub
    return true;
}

bool Rules::load()
{
    LOG_F(INFO, "kernel: initializing rules system");
    return true;
}

bool Rules::reload()
{
    clear();
    return load();
}

bool Rules::cache_2da(const Resource& res)
{
    auto it = cached_2das_.find(res);
    if (it != std::end(cached_2das_) && it->second.is_valid() && it->second.rows() != 0) {
        return true;
    }

    auto tda = TwoDA{resman().demand(res)};
    if (tda.is_valid()) {
        cached_2das_[res] = std::move(tda);
        return true;
    }
    return false;
}

TwoDA& Rules::get_cached_2da(const Resource& res)
{
    return cached_2das_[res];
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
