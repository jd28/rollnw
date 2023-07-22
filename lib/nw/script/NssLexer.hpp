#pragma once

#include "Context.hpp"
#include "Token.hpp"

namespace nw::script {

struct Nss;

struct NssLexer {
    explicit NssLexer(std::string_view buffer,
        std::shared_ptr<Context> ctx = std::make_shared<Context>(),
        Nss* parent = nullptr);

    NssToken next();
    const NssToken& current() const;
    const char* data() const;

private:
    char get(size_t pos) const;
    NssToken handle_identifier();
    NssToken handle_number();

    std::shared_ptr<Context> ctx_;
    Nss* parent_ = nullptr;
    std::string_view buffer_;
    size_t pos_ = 0, line_ = 0, last_line_pos_ = 0;
    NssToken current_;
};

} // namespace nw::script
