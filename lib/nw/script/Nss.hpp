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

    size_t errors() const noexcept;
    void parse();
    Script& script();
    const Script& script() const;

private:
    ByteArray bytes_;
    NssParser parser_;
    Script script_;
};

} // namespace nw::script
