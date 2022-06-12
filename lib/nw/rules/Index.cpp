#include "Index.hpp"

#include "../kernel/Kernel.hpp"

#include <utility>

namespace nw {

// == Index ===================================================================

bool operator==(const Index& lhs, const Index& rhs)
{
    return std::tie(lhs.name, lhs.value) == std::tie(rhs.name, rhs.value);
}

bool operator<(const Index& lhs, const Index& rhs)
{
    return std::tie(lhs.name, lhs.value) < std::tie(rhs.name, rhs.value);
}

} // namespace nw
