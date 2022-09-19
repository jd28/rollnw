#pragma once

#include "NssLexer.hpp"
#include "NssParser.hpp"

#include "../util/ByteArray.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace nw::script {

struct Nss {
    explicit Nss(const std::filesystem::path& filename);
    explicit Nss(std::string_view script);
    explicit Nss(ByteArray bytes);

    Script parse();

private:
    ByteArray bytes_;
    NssParser parser_;
};

} // namespace nw::script
