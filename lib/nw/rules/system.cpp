#include "system.hpp"

#include "../kernel/Kernel.hpp"

namespace nw {

// == Selector ================================================================

bool operator==(const Selector& lhs, const Selector& rhs)
{
    return std::tie(lhs.type, lhs.subtype) == std::tie(rhs.type, rhs.subtype);
}

bool operator<(const Selector& lhs, const Selector& rhs)
{
    return std::tie(lhs.type, lhs.subtype) < std::tie(rhs.type, rhs.subtype);
}

// == Qualifier ===============================================================

// == Requirement =============================================================

Requirement::Requirement(bool conjunction_)
    : conjunction{conjunction_}
{
}

Requirement::Requirement(std::initializer_list<Qualifier> quals, bool conjunction_)
    : conjunction{conjunction_}
{
    for (const auto& q : quals) {
        qualifiers.push_back(q);
    }
}

void Requirement::add(Qualifier qualifier)
{
    qualifiers.push_back(std::move(qualifier));
}

} // namespace nw
