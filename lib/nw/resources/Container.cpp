#include "Container.hpp"

namespace nw {

int Container::extract_by_glob(std::string_view glob, const std::filesystem::path& output) const
{
    return extract(string::glob_to_regex(glob), output);
}

} // namespace nw
