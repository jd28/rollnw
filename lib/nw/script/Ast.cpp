#include "Ast.hpp"

#include "Nss.hpp"

extern "C" {
#include <fzy/match.h>
}

namespace nw::script {

void AstNode::complete(const String& needle, Vector<const Declaration*>& out, bool no_filter) const
{
    for (const auto& [name, exp] : env_) {
        if (no_filter || has_match(needle.c_str(), name.c_str())) {
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

const VarDecl* DeclList::locate_decl(StringView name) const
{
    for (auto d : decls) {
        if (d->identifier_.loc.view() == name) {
            return d;
        }
    }
    return nullptr;
}

const VarDecl* StructDecl::locate_member_decl(StringView name) const
{
    const VarDecl* result = nullptr;
    for (auto d : decls) {
        if (auto vdl = dynamic_cast<const DeclList*>(d)) {
            result = vdl->locate_decl(name);
        } else if (auto vd = dynamic_cast<const VarDecl*>(d)) {
            if (vd->identifier_.loc.view() == name) {
                result = vd;
            }
        }
        if (result) { break; }
    }
    return result;
}

StringView Ast::find_comment(size_t line) const noexcept
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
