#include "Ast.hpp"

#include "Ast.hpp"
#include "Smalls.hpp"

extern "C" {
#include <fzy/match.h>
}

namespace nw::smalls {

void AstNode::complete(const String& needle, Vector<const Declaration*>& out, bool no_filter) const
{
    for (const auto& [name, exp] : env_) {
        if (no_filter || has_match(needle.c_str(), name.c_str())) {
            if (exp.decl) { out.push_back(exp.decl); }
        }
    }
}

// ---- Expression ------------------------------------------------------------

AssignExpression::AssignExpression(Expression* lhs_, Token token, Expression* rhs_)
    : lhs{lhs_}
    , op{token}
    , rhs{rhs_}
{
}

BinaryExpression::BinaryExpression(
    Expression* lhs_,
    Token token,
    Expression* rhs_)
    : lhs{lhs_}
    , op{token}
    , rhs{rhs_}
{
}

ComparisonExpression::ComparisonExpression(
    Expression* lhs_,
    Token token,
    Expression* rhs_)
    : lhs{lhs_}
    , op{token}
    , rhs{rhs_}
{
}

ConditionalExpression::ConditionalExpression(
    Expression* expr_,
    Expression* true_branch_,
    Expression* false_branch_)
    : test{expr_}
    , true_branch{true_branch_}
    , false_branch{false_branch_}
{
}

PathExpression::PathExpression(MemoryResource* allocator)
    : parts{allocator}
{
}

GroupingExpression::GroupingExpression(Expression* expr_)
    : expr{expr_}
{
}

TupleLiteral::TupleLiteral(MemoryResource* allocator)
    : elements{allocator}
{
}

IndexExpression::IndexExpression(Expression* target_, Expression* index_)
    : target{target_}
    , index{index_}
{
}

LiteralExpression::LiteralExpression(Token token)
    : literal{token}
{
}

FStringExpression::FStringExpression(MemoryResource* allocator)
    : parts{allocator}
    , expressions{allocator}
{
}

LogicalExpression::LogicalExpression(
    Expression* lhs_,
    Token token,
    Expression* rhs_)
    : lhs{lhs_}
    , op{token}
    , rhs{rhs_}
{
}

BraceInitLiteral::BraceInitLiteral(MemoryResource* allocator)
    : items{allocator}
{
}

UnaryExpression::UnaryExpression(Token token, Expression* rhs_)
    : op{token}
    , rhs{rhs_}
{
}

IdentifierExpression::IdentifierExpression(Token token, MemoryResource* allocator)
    : ident{token}
    , type_params{allocator}
{
}

CallExpression::CallExpression(Expression* expr_, MemoryResource* allocator)
    : expr{expr_}
    , args{allocator}
    , comma_ranges{allocator}
{
}

CastExpression::CastExpression(MemoryResource* allocator)
{
}

// ---- Statements ------------------------------------------------------------

BlockStatement::BlockStatement(MemoryResource* allocator)
    : nodes{allocator}
{
}

SourceRange Declaration::range() const noexcept
{
    return range_;
}

SourceRange Declaration::selection_range() const noexcept
{
    return range_selection_;
}

DeclList::DeclList(MemoryResource* allocator)
    : decls{allocator}
{
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

FunctionDefinition::FunctionDefinition(MemoryResource* allocator)
    : params{allocator}
{
}

LambdaExpression::LambdaExpression(MemoryResource* allocator)
    : params{allocator}
{
}

JumpStatement::JumpStatement(MemoryResource* allocator)
    : exprs{allocator}
{
}

LabelStatement::LabelStatement(MemoryResource* allocator)
    : bindings{allocator}
{
}

StructDecl::StructDecl(MemoryResource* allocator)
    : decls{allocator}
{
}

SumDecl::SumDecl(MemoryResource* allocator)
    : variants{allocator}
{
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

const VariantDecl* SumDecl::locate_variant(StringView name) const
{
    for (auto v : variants) {
        if (v->identifier_.loc.view() == name) {
            return v;
        }
    }
    return nullptr;
}

Ast::Ast(Context* ctx)
    : ctx_(ctx)
{
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

// Helper to recursively extract string from expression (for qualified names)
static String expr_to_string(Expression* expr)
{
    if (!expr) {
        return "";
    }

    if (auto ident = dynamic_cast<IdentifierExpression*>(expr)) {
        return String(ident->ident.loc.view());
    } else if (auto path = dynamic_cast<PathExpression*>(expr)) {
        String result;
        for (size_t i = 0; i < path->parts.size(); ++i) {
            if (i > 0) {
                result += ".";
            }
            result += expr_to_string(path->parts[i]);
        }
        return result;
    } else if (auto lit = dynamic_cast<LiteralExpression*>(expr)) {
        return String(lit->literal.loc.view());
    } else if (auto type_expr = dynamic_cast<TypeExpression*>(expr)) {
        return type_expr->str();
    }

    return "<expr>";
}

String TypeExpression::str() const
{
    if (is_function_type) {
        String result = "fn(";
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0) result += ", ";
            result += expr_to_string(params[i]);
        }
        result += ")";
        if (fn_return_type) {
            result += ": ";
            result += fn_return_type->str();
        }
        return result;
    }

    if (!name) {
        return "<invalid>";
    }

    if (is_fixed_array && !params.empty()) {
        String result = expr_to_string(name);
        result += "[";
        result += expr_to_string(params[0]);
        result += "]";
        return result;
    } else {
        String result = expr_to_string(name);

        if (!params.empty()) {
            result += "!(";
            for (size_t i = 0; i < params.size(); ++i) {
                if (i > 0) result += ", ";
                result += expr_to_string(params[i]);
            }
            result += ")";
        }

        return result;
    }
}

bool is_mutable_lvalue(const Expression* expr)
{
    if (!expr) return false;

    if (dynamic_cast<const IdentifierExpression*>(expr)) {
        return !expr->is_const_;
    }

    if (auto* index_expr = dynamic_cast<const IndexExpression*>(expr)) {
        return is_mutable_lvalue(index_expr->target);
    }

    if (auto* path = dynamic_cast<const PathExpression*>(expr)) {
        if (!path->parts.empty()) {
            return is_mutable_lvalue(path->parts[0]);
        }
        return false;
    }

    if (auto* group = dynamic_cast<const GroupingExpression*>(expr)) {
        return is_mutable_lvalue(group->expr);
    }

    return false;
}

} // namespace nw::smalls
