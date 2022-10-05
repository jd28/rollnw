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

    /// Returns how many errors were found during parsing
    size_t errors() const noexcept;

    /// Parses script file
    void parse();

    /// Gets parsed script
    Script& script();

    /// Gets parsed script
    const Script& script() const;

    /// Gets text of script
    std::string_view text() const noexcept;

private:
    ByteArray bytes_;
    NssParser parser_;
    Script script_;
};

} // namespace nw::script
