#pragma once

#include "NssLexer.hpp"
#include "NssParser.hpp"

#include "../util/ByteArray.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace nw {

struct Nss {
    Nss(const std::filesystem::path& filename);

    bool load();
    Script parse();

private:
    ByteArray bytes_;
    NssParser parser_;

    bool is_loaded_ = false;
};

} // namespace nw
