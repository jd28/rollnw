#include "IndexRegistry.hpp"

#include "../Kernel.hpp"

namespace nw {

// == IndexRegistry ========================================================

Index IndexRegistry::add(std::string_view name, size_t value)
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return Index{it->first, it->second};
    }

    auto intstr = kernel::strings().intern(name);
    constants_.emplace(intstr, value);

    return {intstr, value};
}

void IndexRegistry::clear()
{
    constants_.clear();
}

Index IndexRegistry::get(std::string_view name) const
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return Index{it->first, it->second};
    }

    return {};
}

} // namespace nw
