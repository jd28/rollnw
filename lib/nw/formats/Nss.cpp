#include "Nss.hpp"

#include <cstring>
#include <fstream>
#include <string_view>

namespace nw::script {

Nss::Nss(const std::filesystem::path& filename)
    : bytes_{ByteArray::from_file(filename)}
    , parser_{bytes_.string_view()}
{
}

Nss::Nss(std::string_view script)
    : parser_{script}
{
}

Nss::Nss(ByteArray bytes)
    : bytes_{std::move(bytes)}
    , parser_{bytes_.string_view()}
{
}

Script Nss::parse()
{
    return parser_.parse_program();
}

} // namespace nw::script
