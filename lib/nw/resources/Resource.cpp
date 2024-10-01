#include "Resource.hpp"

#include "../log.hpp"
#include "../util/platform.hpp"
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

Resource::Resource(const Resref& resref_, ResourceType::type type_) noexcept
    : resref{resref_}
    , type{type_}
{
}

Resource::Resource(StringView resref_, ResourceType::type type_) noexcept
    : Resource(Resref(resref_), type_)
{
}

Resource Resource::from_filename(StringView filename)
{
    auto it = filename.find('.');
    if (it != String::npos) {
        return Resource{filename.substr(0, it), ResourceType::from_extension(filename.substr(it))};
    }
    return {};
}

Resource Resource::from_path(const std::filesystem::path& path)
{
    String ext = path_to_string(path.extension());
    String stem = path_to_string(path.stem());
    return {stem, ResourceType::from_extension(ext)};
}

bool Resource::valid() const noexcept
{
    return type != ResourceType::invalid && !resref.empty();
}

String Resource::filename() const
{
    return resref.string() + "." + ResourceType::to_string(type);
}

std::ostream& operator<<(std::ostream& out, const Resource& res)
{
    out << res.filename();
    return out;
}

void from_json(const nlohmann::json& j, Resource& r)
{
    const auto& str = j.get<String>();
    r = Resource::from_filename(str);
}

void to_json(nlohmann::json& j, const Resource& r)
{
    j = r.filename();
}

} // namespace nw
