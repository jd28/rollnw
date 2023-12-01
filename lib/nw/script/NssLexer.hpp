#pragma once

#include "Context.hpp"
#include "Token.hpp"

namespace nw::script {

struct Nss;

struct lexical_error : public std::runtime_error {
    lexical_error(std::string what_, SourceLocation location_)
        : std::runtime_error{std::move(what_)}
        , location{location_}
    {
    }

    SourceLocation location;
};

struct NssLexer {
    explicit NssLexer(std::string_view buffer, Context* ctx, Nss* parent = nullptr);

    NssToken next();
    const NssToken& current() const;
    const char* data() const;

    std::vector<size_t> line_map;

private:
    char get(size_t pos) const;
    NssToken handle_identifier();
    NssToken handle_number();

    Context* ctx_ = nullptr;
    Nss* parent_ = nullptr;
    std::string_view buffer_;
    size_t pos_ = 0, line_ = 1, last_line_pos_ = 0;
    NssToken current_;
};

} // namespace nw::script
