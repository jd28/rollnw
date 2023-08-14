#pragma once

#include <cstddef>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

namespace nw {

struct Tokenizer {
    Tokenizer();
    Tokenizer(std::string_view buffer, std::string_view comment, bool skip_newline = true);

    std::string_view current() const;
    bool is_newline(std::string_view tk) const;
    size_t line() const;
    std::string_view next();
    void put_back(std::string_view token);
    void set_buffer(std::string_view buffer);

private:
    std::string_view current_;
    std::stack<std::string_view> stack_;
    size_t pos_ = 0, start_ = 0, end_ = 0, line_count_ = 1;
    std::string_view buffer_;
    std::string_view comment_;
    bool skip_newline_ = true;
};

} // namespace nw
