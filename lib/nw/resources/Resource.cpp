#include "Resource.hpp"

#include "../log.hpp"
#include "../util/string.hpp"

#include <ostream>
#include <tuple>

namespace nw {

Resource::Resource() noexcept
    : resref{}
    , type{ResourceType::invalid}
{
}

Resource::Resource(Resref resref, ResourceType::type type) noexcept
    : resref{resref}
    , type{type}
{
}

Resource::Resource(std::string_view resref, ResourceType::type type) noexcept
    : Resource(Resref(resref), type)
{
}

bool Resource::valid() const noexcept
{
    return type != ResourceType::invalid && !resref.empty();
}

std::string Resource::filename() const
{
    return resref.string() + "." + ResourceType::to_string(type);
}

bool operator==(const nw::Resource& lhs, const nw::Resource& rhs)
{
    return lhs.resref == rhs.resref && lhs.type == rhs.type;
}

bool operator!=(const nw::Resource& lhs, const nw::Resource& rhs)
{
    return !(lhs == rhs);
}

bool operator<(const Resource& lhs, const Resource& rhs) noexcept
{
    auto sv1 = lhs.resref.view();
    auto sv2 = rhs.resref.view();
    return std::tie(sv1, lhs.type) < std::tie(sv2, rhs.type);
}

std::ostream& operator<<(std::ostream& out, const Resource& res)
{
    out << res.filename();
    return out;
}

} // namespace nw
