#include "Ast.hpp"

#include "Nss.hpp"

extern "C" {
#include <fzy/match.h>
}

namespace nw::script {

void AstNode::complete(const std::string& needle, std::vector<const Declaration*>& out) const
{
    for (const auto& [name, exp] : env_) {
        if (has_match(needle.c_str(), name.c_str())) {
            if (exp.decl) { out.push_back(exp.decl); }
            if (exp.type) { out.push_back(exp.type); }
        }
    }
}

SourceRange Declaration::range() const noexcept
{
    return range_;
}

SourceRange Declaration::selection_range() const noexcept
{
    return range_selection_;
}

std::string_view Ast::find_comment(size_t line) const noexcept
{
    for (const auto& comment : comments) {
        if (line == comment.range_.range.end.line
            || (line > 0 && line - 1 == comment.range_.range.end.line)) {
            return comment.comment_;
        }
    }
    return {};
}

} // namespace nw::script
