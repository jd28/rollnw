#pragma once

#include "../log.hpp"
#include "Nss.hpp"

#include <vector>

namespace nw::script {

struct AstHinter : public BaseVisitor {
    AstHinter(Nss* parent, SourceRange range)
        : parent_{parent}
        , range_{range}
    {
    }

    virtual ~AstHinter() = default;

    Nss* parent_ = nullptr;
    SourceRange range_;
    std::vector<InlayHint> result_;

    const Declaration* locate_in_dependencies(const std::string& needle)
    {
        if (!parent_->is_command_script() && parent_->ctx()->command_script_) {
            auto sym = parent_->ctx()->command_script_->locate_export(needle, false);
            if (sym.decl) {
                return sym.decl;
            }
        }
        for (const auto it : parent_->ast().includes) {
            if (!it.script) { continue; }
            auto sym = it.script->locate_export(needle, false, true);
            if (sym.decl) {
                return sym.decl;
            }
        }
        return nullptr;
    }

    // -- Vistor
    virtual void visit(Ast* script)
    {
        for (auto decl : script->decls) {
            if (decl) { decl->accept(this); }
        }
    }

    // Decls
    virtual void visit(FunctionDecl* decl)
    {
        // No op
    }

    virtual void visit(FunctionDefinition* decl)
    {
        if (decl->block) { decl->block->accept(this); }
    }

    virtual void visit(StructDecl* decl)
    {
        // No op
    }

    virtual void visit(VarDecl* decl)
    {
        if (decl->init) { decl->init->accept(this); }
    }

    // Expressions
    virtual void visit(AssignExpression* expr)
    {
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(BinaryExpression* expr)
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(CallExpression* expr)
    {
        auto ve = dynamic_cast<VariableExpression*>(expr->expr);
        if (!ve) { return; }

        if (contains_range(range_, expr->range_)) {
            std::string needle{ve->var.loc.view()};
            auto exp = ve->env_.find(needle);
            const Declaration* decl = nullptr;
            if (exp && exp->decl) {
                decl = exp->decl;
            } else {
                decl = locate_in_dependencies(needle);
            }

            const FunctionDecl* fd = nullptr;
            if (auto d = dynamic_cast<const FunctionDecl*>(decl)) {
                fd = d;
            } else if (auto d = dynamic_cast<const FunctionDefinition*>(decl)) {
                fd = d->decl_inline;
            }
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

    virtual void visit(ComparisonExpression* expr)
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(ConditionalExpression* expr)
    {
        if (expr->test) { expr->test->accept(this); }
        if (expr->true_branch) { expr->true_branch->accept(this); }
        if (expr->false_branch) { expr->false_branch->accept(this); }
    }

    virtual void visit(DotExpression* expr)
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(EmptyExpression* expr)
    {
        // No Op
    }

    virtual void visit(GroupingExpression* expr)
    {
        if (expr->expr) { expr->expr->accept(this); }
    }

    virtual void visit(LiteralExpression* expr)
    {
        // No op
    }

    virtual void visit(LiteralVectorExpression* expr)
    {
        // No op
    }

    virtual void visit(LogicalExpression* expr)
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(PostfixExpression* expr)
    {
        if (expr->lhs) { expr->lhs->accept(this); }
    }

    virtual void visit(UnaryExpression* expr)
    {
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(VariableExpression* expr)
    {
        // No op
    }

    // Statements
    virtual void visit(BlockStatement* stmt)
    {
        for (auto decl : stmt->nodes) {
            if (decl) { decl->accept(this); }
        }
    }

    virtual void visit(DeclList* stmt)
    {
        // No op
    }

    virtual void visit(DoStatement* stmt)
    {
        // No op
        if (stmt->expr) { stmt->expr->accept(this); }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(EmptyStatement* stmt)
    {
    }

    virtual void visit(ExprStatement* stmt)
    {
        if (stmt->expr) { stmt->expr->accept(this); }
    }

    virtual void visit(IfStatement* stmt)
    {
        if (stmt->expr) { stmt->expr->accept(this); }
        if (stmt->if_branch) { stmt->if_branch->accept(this); }
        if (stmt->else_branch) {
            stmt->if_branch->accept(this);
        }
    }
    virtual void visit(ForStatement* stmt)
    {
        if (stmt->init) { stmt->init->accept(this); }
        if (stmt->check) { stmt->check->accept(this); }
        if (stmt->inc) { stmt->inc->accept(this); }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(JumpStatement* stmt)
    {
        if (stmt->expr) { stmt->expr->accept(this); }
    }

    virtual void visit(LabelStatement* stmt)
    {
        // No Op
    }

    virtual void visit(SwitchStatement* stmt)
    {
        if (stmt->target) { stmt->target->accept(this); }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(WhileStatement* stmt)
    {
        if (stmt->check) { stmt->check->accept(this); }
        if (stmt->block) { stmt->block->accept(this); }
    }
};

} // namespace nw::script
