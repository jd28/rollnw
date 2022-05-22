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

} // namespace nw
