#include "Ast.hpp"

#include "Nss.hpp"

extern "C" {
#include <fzy/match.h>
}

namespace nw::script {

void AstNode::complete(const std::string& needle, std::vector<std::string>& out) const
{
    for (const auto& [name, _] : env) {
        if (has_match(needle.c_str(), name.c_str())) {
            out.push_back(name);
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

inline const Declaration* walk_block(const BlockStatement* block, SourcePosition needle)
{
    const Declaration* last_seen = nullptr;
    if (!block || !contains_position(block->range, needle)) { return last_seen; }

    for (const auto decl : block->nodes) {
        if (auto d = dynamic_cast<const Declaration*>(decl)) {
            if (d->range().end < needle) {
                last_seen = d;
            } else {
                break;
            }
        } else if (auto ifs = dynamic_cast<const IfStatement*>(decl)) {
            if (auto result = walk_block(dynamic_cast<const BlockStatement*>(ifs->if_branch), needle)) {
                last_seen = result;
            } else if (auto result = walk_block(dynamic_cast<const BlockStatement*>(ifs->else_branch), needle)) {
                last_seen = result;
            }
        } else if (auto ss = dynamic_cast<const SwitchStatement*>(decl)) {
            if (auto result = walk_block(dynamic_cast<const BlockStatement*>(ss->block), needle)) {
                last_seen = result;
            }
        } else if (auto ss = dynamic_cast<const DoStatement*>(decl)) {
            if (auto result = walk_block(dynamic_cast<const BlockStatement*>(ss->block), needle)) {
                last_seen = result;
            }
        } else if (auto ss = dynamic_cast<const WhileStatement*>(decl)) {
            if (auto result = walk_block(dynamic_cast<const BlockStatement*>(ss->block), needle)) {
                last_seen = result;
            }
        } else if (auto ss = dynamic_cast<const ForStatement*>(decl)) {
            if (auto result = walk_block(dynamic_cast<const BlockStatement*>(ss->block), needle)) {
                last_seen = result;
            }
        } else if (auto bs = dynamic_cast<const BlockStatement*>(decl)) {
            if (auto result = walk_block(bs, needle)) {
                last_seen = result;
            }
        }
    }
    return last_seen;
}

const Declaration* Ast::find_last_declaration(size_t line, size_t character) const
{
    const Declaration* last_seen = nullptr;
    SourcePosition needle{line, character};

    for (const auto& decl : decls) {
        if (auto d = dynamic_cast<const Declaration*>(decl)) {
            if (auto fd = dynamic_cast<const FunctionDefinition*>(decl)) {
                if (fd->block && contains_position(fd->block->range, needle)) {
                    if (auto result = walk_block(fd->block, needle)) {
                        last_seen = result;
                    }
                }
            }

            if (d->range().end < needle) {
                last_seen = d;
            } else {
                break;
            }
        }
    }

    return last_seen;
}

} // namespace nw::script
