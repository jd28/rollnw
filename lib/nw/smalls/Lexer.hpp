#pragma once

#include "Context.hpp"
#include "Token.hpp"

namespace nw::smalls {

struct Script;

struct lexical_error : public std::runtime_error {
    lexical_error(String what_, SourceLocation location_)
        : std::runtime_error{std::move(what_)}
        , location{location_}
    {
    }

    SourceLocation location;
};

struct Lexer {
    explicit Lexer(StringView buffer, Context* ctx, Script* parent = nullptr);

    Token next();
    const Token& current() const;
    const char* data() const;

    Vector<size_t> line_map;

private:
    char get(size_t pos) const;
    Token handle_identifier();
    Token handle_number();

    Context* ctx_ = nullptr;
    Script* parent_ = nullptr;
    StringView buffer_;
    size_t pos_ = 0, line_ = 1, last_line_pos_ = 0;
    Token current_;

    const char* scan_to_endl(const char* data, size_t size);
    const char* scan_to_target(const char* data, char target, size_t size, bool no_eol);
};

} // namespace nw::smalls
