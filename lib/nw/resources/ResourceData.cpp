#include "ResourceData.hpp"

namespace nw {

ResourceData ResourceData::copy() const
{
    ResourceData result;
    result.name = name;
    result.bytes = bytes;
    return result;
}

ResourceData ResourceData::from_file(const std::filesystem::path& path)
{
    ResourceData result;
    result.name = Resource::from_path(path);
    result.bytes = ByteArray::from_file(path);
    return result;
}

} // namespace nw
