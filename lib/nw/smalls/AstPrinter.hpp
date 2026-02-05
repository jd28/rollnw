#pragma once

#include "Ast.hpp"
#include "runtime.hpp"

#include <absl/strings/str_join.h>

namespace nw::smalls {

/// Prints a resolved AST using fully qualified type names.
/// NOTE: This printer only works correctly on ASTs that have been through
/// the resolver (AstResolver), as it relies on type_id_ being populated.
struct AstPrinter : BaseVisitor {
    ~AstPrinter() = default;
    std::stringstream ss;
    int depth = 0;

    virtual void visit(Ast* script) override
    {
        ss << '\n'
           << "(script ";
        ++depth;
        for (const auto& d : script->decls) {
            d->accept(this);
        }
        --depth;
        ss << ")";
    }

    // Decls
    virtual void visit(FunctionDefinition* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(fn " << decl->identifier_.loc.view();

        if (decl->return_type) {
            ss << ": " << decl->return_type->str();
        }

        // Print parameters
        ++depth;
        for (const auto& p : decl->params) {
            p->accept(this);
        }

        // Print body
        if (decl->block) {
            decl->block->accept(this);
        }
        --depth;

        ss << ")";
    }

    virtual void visit(StructDecl* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(type "
           << (decl->type ? decl->type->str() : "<no-type>");
        ++depth;
        for (const auto& d : decl->decls) {
            d->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(VarDecl* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(decl " << decl->identifier_.loc.view() << " : ";
        ss << (decl->type ? decl->type->str() : "<inferred>");

        if (decl->init) {
            ss << " =";
            ++depth;
            decl->init->accept(this);
            --depth;
        }

        ss << ")";
    }

    virtual void visit(TypeAlias* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(alias "
           << (decl->type ? decl->type->str() : "<no-type>")
           << " = " << decl->aliased_type->str() << ")";
    }

    virtual void visit(NewtypeDecl* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(newtype "
           << (decl->type ? decl->type->str() : "<no-type>")
           << "(" << decl->wrapped_type->str() << "))";
    }

    virtual void visit(OpaqueTypeDecl* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(opaque "
           << (decl->type ? decl->type->str() : "<no-type>")
           << ")";
    }

    virtual void visit(SumDecl* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(sum "
           << (decl->type ? decl->type->str() : "<no-type>");
        ++depth;
        for (const auto& v : decl->variants) {
            v->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(VariantDecl* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(variant " << decl->identifier();
        if (decl->payload) {
            ++depth;
            decl->payload->accept(this);
            --depth;
        }
        ss << ")";
    }

    virtual void visit(AliasedImportDecl* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(import " << decl->module_path
           << " as " << decl->alias.loc.view() << ")";
    }

    virtual void visit(SelectiveImportDecl* decl) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(from " << decl->module_path << " import {";
        for (size_t i = 0; i < decl->imported_symbols.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << decl->imported_symbols[i].loc.view();
        }
        ss << "})";
    }

    // Expressions
    virtual void visit(AssignExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(assign " << expr->op.loc.view();
        ++depth;
        expr->lhs->accept(this);
        expr->rhs->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(BinaryExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(binary " << expr->op.loc.view();
        ++depth;
        expr->lhs->accept(this);
        expr->rhs->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(CallExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(call ";
        ++depth;
        expr->expr->accept(this);

        if (expr->args.size()) {
            ss << '\n'
               << String(size_t(depth * 2), ' ')
               << "(params";
            ++depth;
            for (const auto& arg : expr->args) {
                arg->accept(this);
            }
            --depth;
        }
        ss << ")";
        --depth;
    }

    virtual void visit(CastExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(" << expr->op.loc.view();
        ++depth;
        if (expr->expr) expr->expr->accept(this);
        ss << " " << expr->op.loc.view() << " ";
        if (expr->target_type) {
            ss << expr->target_type->str();
        }
        --depth;
        ss << ")";
    }

    virtual void visit(ComparisonExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(binary " << expr->op.loc.view();
        ++depth;
        expr->lhs->accept(this);
        expr->rhs->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(ConditionalExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(cond";
        ++depth;
        expr->test->accept(this);

        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(then";
        ++depth;
        expr->true_branch->accept(this);
        --depth;
        ss << ")";

        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(else";
        ++depth;
        expr->false_branch->accept(this);
        --depth;
        ss << ")";
        --depth;
        ss << ")";
    }

    virtual void visit(PathExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(path";

        ++depth;
        for (auto* part : expr->parts) {
            if (part) {
                part->accept(this);
            }
        }
        --depth;
        ss << ")";
    }

    virtual void visit(EmptyExpression*) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "<empty expr>";
    }

    virtual void visit(GroupingExpression* expr) override
    {
        expr->expr->accept(this);
    }

    virtual void visit(TupleLiteral* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(tuple";
        depth++;
        for (auto* elem : expr->elements) {
            elem->accept(this);
        }
        depth--;
        ss << ")";
    }

    virtual void visit(IndexExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(index";
        depth++;
        expr->target->accept(this);
        expr->index->accept(this);
        depth--;
        ss << ")";
    }

    virtual void visit(LiteralExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(literal " << expr->literal.loc.view() << ")";
    }

    virtual void visit(FStringExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(fstring";
        ++depth;
        for (size_t i = 0; i < expr->parts.size(); ++i) {
            if (!expr->parts[i].empty()) {
                ss << '\n'
                   << String(size_t(depth * 2), ' ') << "(part \"" << expr->parts[i] << "\")";
            }
            if (i < expr->expressions.size() && expr->expressions[i]) {
                expr->expressions[i]->accept(this);
            }
        }
        --depth;
        ss << ")";
    }

    virtual void visit(LogicalExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(logical " << expr->op.loc.view() << " ";
        ++depth;
        expr->lhs->accept(this);
        ss << " ";
        expr->rhs->accept(this);
        ss << ")";
        --depth;
    }

    virtual void visit(BraceInitLiteral* expr) override
    {
        const char* type_str = expr->init_type == BraceInitType::field ? "field"
            : expr->init_type == BraceInitType::kv                     ? "kv"
                                                                       : "list";
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(brace-init " << type_str;
        ++depth;
        for (auto& item : expr->items) {
            if (item.key) {
                ss << '\n'
                   << String(size_t(depth * 2), ' ') << "(item";
                ++depth;
                ss << '\n'
                   << String(size_t(depth * 2), ' ') << "(key";
                ++depth;
                item.key->accept(this);
                --depth;
                ss << ")";
                ss << '\n'
                   << String(size_t(depth * 2), ' ') << "(value";
                ++depth;
                if (item.value) item.value->accept(this);
                --depth;
                ss << ")";
                --depth;
                ss << ")";
            } else {
                if (item.value) item.value->accept(this);
            }
        }
        --depth;
        ss << ")";
    }

    virtual void visit(UnaryExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(unary " << expr->op.loc.view();
        ++depth;
        expr->rhs->accept(this);
        --depth;
        ss << ")";
    }

    virtual void visit(TypeExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ')
           << "(type "
           << expr->str()
           << ")";
    }

    virtual void visit(IdentifierExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ')
           << "(ident "
           << expr->ident.loc.view()
           << ")";
    }

    virtual void visit(LambdaExpression* expr) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(lambda";
        ++depth;
        for (auto* param : expr->params) {
            param->accept(this);
        }
        if (expr->return_type) {
            ss << '\n'
               << String(size_t(depth * 2), ' ') << ": " << expr->return_type->str();
        }
        if (expr->body) {
            expr->body->accept(this);
        }
        --depth;
        ss << ")";
    }

    // Statements
    virtual void visit(BlockStatement* stmt) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ')
           << "(block";

        ++depth;
        for (const auto& n : stmt->nodes) {
            n->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(DeclList* stmt) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ')
           << "(decls";
        ++depth;
        for (const auto& decl : stmt->decls) {
            decl->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(EmptyStatement*) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(EmptySatement)";
    }

    virtual void visit(ExprStatement* stmt) override
    {
        stmt->expr->accept(this);
    }

    virtual void visit(IfStatement* stmt) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(if";
        ++depth;
        stmt->expr->accept(this);

        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(then";
        ++depth;
        stmt->if_branch->accept(this);
        --depth;
        ss << ")";

        if (stmt->else_branch) {
            ss << '\n'
               << String(size_t(depth * 2), ' ')
               << "(else";
            ++depth;
            stmt->else_branch->accept(this);
            --depth;
            ss << ")";
        }
        --depth;
        ss << ")";
    }

    virtual void visit(ForStatement* stmt) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ')
           << "(for";
        ++depth;
        if (stmt->init) {
            ss << '\n'
               << String(size_t(depth * 2), ' ') << "(init";
            ++depth;
            stmt->init->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->check) {
            ss << '\n'
               << String(size_t(depth * 2), ' ') << "(check";
            ++depth;
            stmt->check->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->inc) {
            ss << '\n'
               << String(size_t(depth * 2), ' ')
               << "(increment";
            ++depth;
            stmt->inc->accept(this);
            --depth;
            ss << ")";
        }

        stmt->block->accept(this);
        --depth;
    }

    virtual void visit(ForEachStatement* stmt) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ')
           << "(foreach";
        ++depth;
        if (stmt->var) {
            ss << '\n'
               << String(size_t(depth * 2), ' ') << "(var";
            ++depth;
            stmt->var->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->key_var) {
            ss << '\n'
               << String(size_t(depth * 2), ' ') << "(key";
            ++depth;
            stmt->key_var->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->value_var) {
            ss << '\n'
               << String(size_t(depth * 2), ' ') << "(value";
            ++depth;
            stmt->value_var->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->collection) {
            ss << '\n'
               << String(size_t(depth * 2), ' ') << "(collection";
            ++depth;
            stmt->collection->accept(this);
            --depth;
            ss << ")";
        }
        if (stmt->block) {
            stmt->block->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(JumpStatement* stmt) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(" << stmt->op.loc.view();
        ++depth;
        for (auto expr : stmt->exprs) {
            if (expr) {
                expr->accept(this);
            }
        }
        --depth;
        ss << ")";
    }

    virtual void visit(LabelStatement* stmt) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(" << stmt->type.loc.view();
        ++depth;
        if (stmt->expr) {
            stmt->expr->accept(this);
        }
        --depth;
        ss << ")";
    }

    virtual void visit(SwitchStatement* stmt) override
    {
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(switch";
        ++depth;
        ss << '\n'
           << String(size_t(depth * 2), ' ') << "(target";
        ++depth;
        stmt->target->accept(this);
        --depth;
        ss << ")";

        stmt->block->accept(this);

        --depth;
    }
};

} // namespace nw::script
