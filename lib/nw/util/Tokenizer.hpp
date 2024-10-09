#pragma once

#include "../config.hpp"

#include <cstddef>
#include <stack>
#include <string>
#include <string_view>

namespace nw {

struct Tokenizer {
    Tokenizer();
    Tokenizer(StringView buffer, StringView comment, bool skip_newline = true);

    StringView current() const;
    bool is_newline(StringView tk) const;
    size_t line() const;
    StringView next();
    void put_back(StringView token);
    void set_buffer(StringView buffer);

private:
    StringView current_;
    std::stack<StringView> stack_;
    size_t pos_ = 0, start_ = 0, end_ = 0, line_count_ = 1;
    StringView buffer_;
    StringView comment_;
    bool skip_newline_ = true;
};

} // namespace nw
