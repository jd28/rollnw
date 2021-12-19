#include "Resource.hpp"

#include "../log.hpp"
#include "../util/string.hpp"

#include <nlohmann/json.hpp>

#include <ostream>
#include <tuple>

namespace nw {

Resource::Resource() noexcept
    : resref{}
    , type{ResourceType::invalid}
{
}

Resource::Resource(const Resref& resref, ResourceType::type type) noexcept
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

void from_json(const nlohmann::json& j, Resource& r)
{
    const auto& str = j.get<std::string>();
    auto it = str.find('.');
    if (it != std::string::npos) {
        r = Resource{str.substr(0, it), ResourceType::from_extension(str.substr(it))};
    }
}

void to_json(nlohmann::json& j, const Resource& r)
{
    j = r.filename();
}

} // namespace nw
