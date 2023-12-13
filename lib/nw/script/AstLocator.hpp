#pragma once

#include "../log.hpp"
#include "Nss.hpp"
#include "SourceLocation.hpp"

namespace nw::script {

struct AstLocator : public BaseVisitor {
    AstLocator(Nss* parent, std::string symbol, size_t line, size_t character)
        : parent_{parent}
        , symbol_{std::move(symbol)}
        , pos_{line, character}
    {
    }

    // Search data
    const Nss* parent_ = nullptr;
    SourcePosition pos_;
    std::string symbol_;
    bool in_func_decl_ = false;
    bool in_struct_decl_ = false;

    // Result data
    bool found_ = false;
    Symbol result_;
    const Declaration* last_seen_decl = nullptr;
    const DotExpression* dot = nullptr;   // Keep track if our symbol is in a dot expr
    const CallExpression* call = nullptr; // Keep track if our symbol is in a call expr
    size_t active_param = 0;

    void locate_in_dependencies()
    {
        if (!parent_->is_command_script() && parent_->ctx()->command_script_) {
            auto sym = parent_->ctx()->command_script_->locate_export(symbol_, false);
            if (sym.decl) {
                result_ = sym;
            }
        }
        if (!result_.decl) {
            for (const auto it : parent_->ast().includes) {
                if (!it.script) { continue; }
                auto sym = it.script->locate_export(symbol_, false, true);
                if (sym.decl) {
                    result_ = sym;
                    break;
                }
            }
        }
        found_ = !!result_.decl;
    }

    // -- Visitor -------------------------------------------------------------

    virtual void visit(Ast* script)
    {
        for (auto decl : script->decls) {
            if (decl) { decl->accept(this); }
            if (found_) { break; }
        }
    }

    // Decls
    virtual void visit(FunctionDecl* decl)
    {
        if (decl->range_.end < pos_) { last_seen_decl = decl; }
        if (contains_position(decl->range_, pos_)) {
            if (decl->type.struct_id.type != NssTokenType::INVALID
                && contains_position(decl->type.struct_id.loc.range, pos_)) {
                std::string struct_name{decl->type.struct_id.loc.view()};
                auto exp = decl->env_.find(struct_name);
                if (exp && exp->type) {
                    result_ = parent_->declaration_to_symbol(exp->type);
                    found_ = true;
                } else {
                    locate_in_dependencies();
                }
            } else if (contains_position(decl->identifier_.loc.range, pos_)) {
                result_ = parent_->declaration_to_symbol(decl);
                found_ = true;
            } else {
                in_func_decl_ = true;
                for (auto param : decl->params) {
                    if (param) { param->accept(this); }
                    if (found_) { return; }
                }
                in_func_decl_ = false;
            }
        }
    }

    virtual void visit(FunctionDefinition* decl)
    {
        if (decl->range_.end < pos_) { last_seen_decl = decl; }

        if (contains_position(decl->range_, pos_)) {
            if (decl->decl_inline) { decl->decl_inline->accept(this); }
            if (found_) { return; }
            if (decl->block) { decl->block->accept(this); }
        }
    }

    virtual void visit(StructDecl* decl)
    {
        if (decl->range_.end < pos_) { last_seen_decl = decl; }

        if (decl->type.struct_id.type != NssTokenType::INVALID
            && contains_position(decl->type.struct_id.loc.range, pos_)) {
            result_ = parent_->declaration_to_symbol(decl);
            found_ = true;
        } else {
            in_struct_decl_ = true;
            for (auto d : decl->decls) {
                if (d) { d->accept(this); }
                if (found_) { return; }
            }
            in_struct_decl_ = false;
        }
    }

    virtual void visit(VarDecl* decl)
    {
        if (decl->range_.end < pos_) { last_seen_decl = decl; }

        if (contains_position(decl->identifier_.loc.range, pos_)) {
            result_ = parent_->declaration_to_symbol(decl);
            if (in_func_decl_) {
                result_.kind = SymbolKind::param;
            } else if (in_struct_decl_) {
                result_.kind = SymbolKind::field;
            }
            found_ = true;
        } else if (decl->type.struct_id.type != NssTokenType::INVALID
            && contains_position(decl->type.struct_id.loc.range, pos_)) {
            std::string struct_name{decl->type.struct_id.loc.view()};
            auto exp = decl->env_.find(struct_name);
            if (exp && exp->type) {
                result_ = parent_->declaration_to_symbol(exp->type);
                found_ = true;
            } else {
                locate_in_dependencies();
            }
        } else if (decl->init) {
            decl->init->accept(this);
        }
    }

    // Expressions
    virtual void visit(AssignExpression* expr)
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(BinaryExpression* expr)
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(CallExpression* expr)
    {
        if (contains_position(expr->range_, pos_)) {
            call = expr;
        }

        if (expr->expr) { expr->expr->accept(this); }
        if (found_) { return; }
        for (size_t i = 0; i < expr->args.size(); ++i) {
            if (expr->args[i]) {
                if (expr->args[i]->range_.start <= pos_) {
                    active_param = i;
                }
                expr->args[i]->accept(this);
            }
            if (found_) { return; }
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
        if (found_) { return; }
        if (expr->rhs) { expr->rhs->accept(this); }
        if (found_) { dot = expr; }
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
        // No Op
    }

    virtual void visit(LiteralVectorExpression* expr)
    {
        // No Op
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
        if (contains_position(expr->var.loc.range, pos_) && expr->var.loc.view() == symbol_) {
            auto exp = expr->env_.find(symbol_);
            if (exp && exp->decl) {
                result_ = parent_->declaration_to_symbol(exp->decl);
            } else {
                locate_in_dependencies();
            }
            result_.node = expr;
            found_ = true;
        }
    }

    // Statements
    virtual void visit(BlockStatement* stmt)
    {
        // Block is a good place to bail if we've not found the symbol
        if (stmt->range_.start.line > pos_.line) { return; }
        for (auto decl : stmt->nodes) {
            if (decl) { decl->accept(this); }
            if (found_) { return; }
        }
    }

    virtual void visit(DeclList* stmt)
    {
        for (auto decl : stmt->decls) {
            if (decl) { decl->accept(this); }
            if (found_) { return; }
        }
    }

    virtual void visit(DoStatement* stmt)
    {
        if (stmt->expr) { stmt->expr->accept(this); }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(EmptyStatement* stmt)
    {
        // No Op
    }

    virtual void visit(ExprStatement* stmt)
    {
        if (stmt->expr) { stmt->expr->accept(this); }
    }

    virtual void visit(IfStatement* stmt)
    {
        if (stmt->expr) { stmt->expr->accept(this); }
        if (found_) { return; }
        if (stmt->if_branch) { stmt->if_branch->accept(this); }
        if (found_) { return; }
        if (stmt->else_branch) { stmt->else_branch->accept(this); }
    }

    virtual void visit(ForStatement* stmt)
    {
        if (stmt->init) { stmt->init->accept(this); }
        if (found_) { return; }
        if (stmt->check) { stmt->check->accept(this); }
        if (found_) { return; }
        if (stmt->inc) { stmt->inc->accept(this); }
        if (found_) { return; }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(JumpStatement* stmt)
    {
        if (stmt->expr) { stmt->expr->accept(this); }
    }

    virtual void visit(LabelStatement* stmt)
    {
        if (stmt->expr) { stmt->expr->accept(this); }
    }

    virtual void visit(SwitchStatement* stmt)
    {
        if (stmt->target) { stmt->target->accept(this); }
        if (found_) { return; }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(WhileStatement* stmt)
    {
        if (stmt->check) { stmt->check->accept(this); }
        if (found_) { return; }
        if (stmt->block) { stmt->block->accept(this); }
    }
};

} // namespace nw::script
