#include "ResourceData.hpp"

namespace nw {

ResourceData ResourceData::from_file(const std::filesystem::path& path)
{
    ResourceData result;
    result.name = Resource::from_path(path);
    result.bytes = ByteArray::from_file(path);
    return result;
}

} // namespace nw
