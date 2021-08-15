#include "Container.hpp"

namespace nw {

int Container::extract(std::string_view glob, const std::filesystem::path& output)
{
    return extract(string::glob_to_regex(glob), output);
}

} // namespace nw
