#pragma once

#include "Context.hpp"

namespace nw::script {

struct LspContext : public Context {
    LspContext(std::string command_script = "nwscript");
    virtual ~LspContext() = default;

    virtual void lexical_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceRange range) override;
    virtual void parse_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceRange range) override;
    virtual void semantic_diagnostic(Nss* script, std::string_view msg, bool is_warning, SourceRange range) override;
};

} // namespace nw::script
