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

size_t Nss::warnings() const noexcept
{
    return parser_.warnings_;
}

void Nss::parse()
{
    script_ = parser_.parse_program();
}

NssParser& Nss::parser() { return parser_; }
const NssParser& Nss::parser() const { return parser_; }

Script& Nss::script() { return script_; }
const Script& Nss::script() const { return script_; }

std::string_view Nss::text() const noexcept { return bytes_.string_view(); }

} // namespace nw::script
