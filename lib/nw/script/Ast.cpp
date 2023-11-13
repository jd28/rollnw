#include "Ast.hpp"

#include "Nss.hpp"

extern "C" {
#include <fzy/match.h>
}

namespace nw::script {

std::vector<std::string> AstNode::complete(const std::string& needle) const
{
    std::vector<std::string> result;
    for (const auto& [name, _] : env) {
        if (has_match(needle.c_str(), name.c_str())) {
            result.push_back(name);
        }
    }
    return result;
}

} // namespace nw::script
