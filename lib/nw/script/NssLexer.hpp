#pragma once

#include "Token.hpp"

namespace nw::script {

struct NssLexer {
    explicit NssLexer(std::string_view buffer);

    NssToken next();
    const NssToken& current() const;
    const char* data() const;

private:
    char get(size_t pos) const;
    NssToken handle_identifier();
    NssToken handle_number();

    std::string_view buffer_;
    size_t pos_ = 0, line_ = 0, last_line_pos_ = 0;
    NssToken current_;
};

} // namespace nw::script
