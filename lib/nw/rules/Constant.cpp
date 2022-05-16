#include "Constant.hpp"

#include "../kernel/Kernel.hpp"

#include <utility>

namespace nw {

// == Constant ================================================================

bool operator==(const Constant& lhs, const Constant& rhs)
{
    return std::tie(lhs.name, lhs.value) == std::tie(rhs.name, rhs.value);
}

bool operator<(const Constant& lhs, const Constant& rhs)
{
    return std::tie(lhs.name, lhs.value) < std::tie(rhs.name, rhs.value);
}

// == ConstantRegistry ========================================================

Constant ConstantRegistry::add(std::string_view name, Constant::variant_type value)
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return Constant{it->first, it->second};
    }

    auto intstr = kernel::strings().intern(name);
    constants_.emplace(intstr, value);

    return {intstr, value};
}

void ConstantRegistry::clear()
{
    constants_.clear();
}

Constant ConstantRegistry::get(std::string_view name) const
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return Constant{it->first, it->second};
    }

    return {};
}

} // namespace nw
