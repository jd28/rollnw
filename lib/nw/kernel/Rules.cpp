#include "Rules.hpp"

#include "../formats/TwoDA.hpp"
#include "Kernel.hpp"

namespace nw::kernel {

Rules::~Rules()
{
    clear();
}

void Rules::clear()
{
    constants_.clear();
}

bool Rules::initialize()
{
    // Stub
    return true;
}

Constant Rules::get_constant(std::string_view name) const
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return {it->first, it->second};
    }

    return {};
}

Constant Rules::register_constant(std::string_view name, int value)
{
    absl::string_view v{name.data(), name.size()};
    auto it = constants_.find(v);
    if (it != std::end(constants_)) {
        return {it->first, it->second};
    }

    auto intstr = kernel::strings().intern(name);
    constants_.emplace(intstr, value);

    return {intstr, value};
}

} // namespace nw::kernel
