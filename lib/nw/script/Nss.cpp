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

size_t Nss::errors() const noexcept
{
    return parser_.errors_;
}

void Nss::parse()
{
    script_ = parser_.parse_program();
}

Script& Nss::script() { return script_; }
const Script& Nss::script() const { return script_; }

} // namespace nw::script
