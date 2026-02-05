#pragma once

#include "Smalls.hpp"

namespace nw::smalls {

struct AstHinter : public BaseVisitor {
    AstHinter(Script* parent, SourceRange range)
        : parent_{parent}
        , range_{range}
    {
    }

    virtual ~AstHinter() = default;

    Script* parent_ = nullptr;
    SourceRange range_;
    Vector<InlayHint> result_;

    // -- Vistor
    virtual void visit(Ast* script) override
    {
        for (auto decl : script->decls) {
            if (decl) { decl->accept(this); }
        }
    }

    // Decls
    virtual void visit(FunctionDefinition* decl) override
    {
        if (decl->block) { decl->block->accept(this); }
    }

    virtual void visit(StructDecl* decl) override
    {
        // No op
    }

    virtual void visit(VarDecl* decl) override
    {
        if (decl->init) { decl->init->accept(this); }
    }

    virtual void visit(TypeAlias*) override { }
    virtual void visit(NewtypeDecl*) override { }
    virtual void visit(SumDecl*) override { }
    virtual void visit(VariantDecl*) override { }
    virtual void visit(OpaqueTypeDecl*) override { }

    virtual void visit(AliasedImportDecl*) override { }
    virtual void visit(SelectiveImportDecl*) override { }

    // Expressions
    virtual void visit(AssignExpression* expr) override
    {
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(BraceInitLiteral* expr) override
    {
        for (auto& item : expr->items) {
            if (item.value) { item.value->accept(this); }
        }
    }

    virtual void visit(BinaryExpression* expr) override
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(CallExpression* expr) override
    {
        auto path = dynamic_cast<PathExpression*>(expr->expr);
        if (!path || path->parts.size() != 1) { return; }
        auto ve = dynamic_cast<IdentifierExpression*>(path->parts.front());
        if (!ve) { return; }

        if (contains_range(range_, expr->range_)) {
            String needle{ve->ident.loc.view()};
            auto exp = ve->env_.find(needle);
            const Declaration* decl = nullptr;
            if (exp && exp->decl) {
                decl = exp->decl;
            }

            const FunctionDefinition* fd = dynamic_cast<const FunctionDefinition*>(decl);
            if (!fd) { return; }

            size_t args = std::min(fd->params.size(), expr->args.size());
            for (size_t i = 0; i < args; ++i) {
                result_.push_back({fd->params[i]->identifier(), expr->args[i]->range_.start});
            }

            for (auto arg : expr->args) {
                if (arg) { arg->accept(this); }
            }
        }
    }

    virtual void visit(CastExpression* expr) override
    {
        if (expr->expr) { expr->expr->accept(this); }
    }

    virtual void visit(ComparisonExpression* expr) override
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(ConditionalExpression* expr) override
    {
        if (expr->test) { expr->test->accept(this); }
        if (expr->true_branch) { expr->true_branch->accept(this); }
        if (expr->false_branch) { expr->false_branch->accept(this); }
    }

    virtual void visit(PathExpression* expr) override
    {
        for (auto* part : expr->parts) {
            if (part) { part->accept(this); }
        }
    }

    virtual void visit(EmptyExpression* expr) override
    {
        // No Op
    }

    virtual void visit(GroupingExpression* expr) override
    {
        if (expr->expr) { expr->expr->accept(this); }
    }

    virtual void visit(TupleLiteral* expr) override
    {
        for (auto* elem : expr->elements) {
            if (elem) { elem->accept(this); }
        }
    }

    virtual void visit(IndexExpression* expr) override
    {
        if (expr->target) { expr->target->accept(this); }
        if (expr->index) { expr->index->accept(this); }
    }

    virtual void visit(LiteralExpression* expr) override
    {
        // No op
    }

    virtual void visit(FStringExpression* expr) override
    {
        for (auto* sub_expr : expr->expressions) {
            if (sub_expr) { sub_expr->accept(this); }
        }
    }

    virtual void visit(LogicalExpression* expr) override
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(UnaryExpression* expr) override
    {
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(TypeExpression*) override
    {
        // No op
    }

    virtual void visit(IdentifierExpression* expr) override
    {
        // No op
    }

    virtual void visit(LambdaExpression* expr) override
    {
        if (expr->body) { expr->body->accept(this); }
    }

    // Statements
    virtual void visit(BlockStatement* stmt) override
    {
        for (auto decl : stmt->nodes) {
            if (decl) { decl->accept(this); }
        }
    }

    virtual void visit(DeclList* stmt) override
    {
        // No op
    }

    virtual void visit(EmptyStatement* stmt) override
    {
    }

    virtual void visit(ExprStatement* stmt) override
    {
        if (stmt->expr) { stmt->expr->accept(this); }
    }

    virtual void visit(IfStatement* stmt) override
    {
        if (stmt->expr) { stmt->expr->accept(this); }
        if (stmt->if_branch) { stmt->if_branch->accept(this); }
        if (stmt->else_branch) {
            stmt->if_branch->accept(this);
        }
    }
    virtual void visit(ForStatement* stmt) override
    {
        if (stmt->init) { stmt->init->accept(this); }
        if (stmt->check) { stmt->check->accept(this); }
        if (stmt->inc) { stmt->inc->accept(this); }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(ForEachStatement* stmt) override
    {
        if (stmt->collection) { stmt->collection->accept(this); }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(JumpStatement* stmt) override
    {
        for (auto expr : stmt->exprs) {
            if (expr) { expr->accept(this); }
        }
    }

    virtual void visit(LabelStatement* stmt) override
    {
        // No Op
    }

    virtual void visit(SwitchStatement* stmt) override
    {
        if (stmt->target) { stmt->target->accept(this); }
        if (stmt->block) { stmt->block->accept(this); }
    }
};

} // namespace nw::smalls
