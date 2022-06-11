#include "Nss.hpp"

#include <cstring>
#include <fstream>
#include <string_view>

namespace nw::script {

Nss::Nss(const std::filesystem::path& filename)
    : bytes_{ByteArray::from_file(filename)}
    , parser_{bytes_.string_view()}
{
    is_loaded_ = load();
}

bool Nss::load()
{
    return true;
}

Script Nss::parse()
{
    return parser_.parse_program();
}

} // namespace nw::script
