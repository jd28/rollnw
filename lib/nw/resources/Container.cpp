#include "Container.hpp"

#include "../log.hpp"
#include "../util/platform.hpp"

namespace nw {

Container::Container()
    : working_dir_{create_unique_tmp_path()}
{
}

Container::~Container()
{
    std::filesystem::remove_all(working_dir_);
}

int Container::extract_by_glob(std::string_view glob, const std::filesystem::path& output) const
{
    return extract(string::glob_to_regex(glob), output);
}

const std::filesystem::path& Container::working_directory() const
{
    return working_dir_;
}

} // namespace nw
