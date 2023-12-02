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
    Nss* parent_ = nullptr;
    SourcePosition pos_;
    std::string symbol_;

    // Result data
    bool found_ = false;
    Symbol result_;

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
            decl->accept(this);
            if (found_) { break; }
        }
    }

    // Decls
    virtual void visit(FunctionDecl* decl)
    {
        if (contains_position(decl->range_, pos_)) {
            if (decl->type.struct_id.type != NssTokenType::INVALID
                && contains_position(decl->type.struct_id.loc.range, pos_)) {
                std::string struct_name{decl->type.struct_id.loc.view()};
                auto exp = decl->env.find(struct_name);
                if (exp && exp->type) {
                    result_.decl = exp->type;
                    result_.type = struct_name;
                    result_.comment = parent_->ast().find_comment(result_.decl->range_.start.line);
                    result_.kind = SymbolKind::type;
                    result_.view = parent_->view_from_range(result_.decl->range());
                    found_ = true;
                } else {
                    locate_in_dependencies();
                }
            } else if (contains_position(decl->identifier.loc.range, pos_)) {
                result_.decl = decl;
                result_.type = parent_->ctx()->type_name(decl->type_id_);
                result_.comment = parent_->ast().find_comment(decl->range_.start.line);
                result_.kind = SymbolKind::function;
                result_.view = parent_->view_from_range(result_.decl->range());
                found_ = true;
            } else {
                for (auto param : decl->params) {
                    param->accept(this);
                    if (found_) { return; }
                }
            }
        }
    }

    virtual void visit(FunctionDefinition* decl)
    {
        if (contains_position(decl->range_, pos_)) {
            decl->decl_inline->accept(this);
            if (found_) { return; }
            decl->block->accept(this);
        }
    }

    virtual void visit(StructDecl* decl)
    {
        if (decl->type.struct_id.type != NssTokenType::INVALID
            && contains_position(decl->type.struct_id.loc.range, pos_)) {
            result_.decl = decl;
            result_.type = std::string(decl->type.struct_id.loc.view());
            result_.comment = parent_->ast().find_comment(result_.decl->range_.start.line);
            result_.kind = SymbolKind::type;
            result_.view = parent_->view_from_range(result_.decl->range());
            found_ = true;
        } else {
            for (auto decl : decl->decls) {
                decl->accept(this);
                if (found_) { return; }
            }
        }
    }

    virtual void visit(VarDecl* decl)
    {
        if (contains_position(decl->identifier.loc.range, pos_)) {
            result_.decl = decl;
            result_.type = parent_->ctx()->type_name(decl->type_id_);
            result_.comment = parent_->ast().find_comment(decl->range_.start.line);
            result_.view = parent_->view_from_range(result_.decl->range());
            found_ = true;
        } else if (decl->type.struct_id.type != NssTokenType::INVALID
            && contains_position(decl->type.struct_id.loc.range, pos_)) {
            std::string struct_name{decl->type.struct_id.loc.view()};
            auto exp = decl->env.find(struct_name);
            if (exp && exp->type) {
                result_.decl = exp->type;
                result_.type = struct_name;
                result_.comment = parent_->ast().find_comment(result_.decl->range_.start.line);
                result_.kind = SymbolKind::type;
                result_.view = parent_->view_from_range(result_.decl->range());
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
        expr->lhs->accept(this);
        expr->rhs->accept(this);
    }

    virtual void visit(BinaryExpression* expr)
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);
    }

    virtual void visit(CallExpression* expr)
    {
        expr->expr->accept(this);
        if (found_) { return; }
        for (const auto arg : expr->args) {
            arg->accept(this);
            if (found_) { return; }
        }
    }

    virtual void visit(ComparisonExpression* expr)
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);
    }

    virtual void visit(ConditionalExpression* expr)
    {
        expr->test->accept(this);
        expr->true_branch->accept(this);
        expr->false_branch->accept(this);
    }

    virtual void visit(DotExpression* expr)
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);
    }

    virtual void visit(GroupingExpression* expr)
    {
        expr->expr->accept(this);
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
        expr->lhs->accept(this);
        expr->rhs->accept(this);
    }

    virtual void visit(PostfixExpression* expr)
    {
        expr->lhs->accept(this);
    }

    virtual void visit(UnaryExpression* expr)
    {
        expr->rhs->accept(this);
    }

    virtual void visit(VariableExpression* expr)
    {
        if (contains_position(expr->var.loc.range, pos_) && expr->var.loc.view() == symbol_) {
            auto exp = expr->env.find(symbol_);
            if (exp && exp->decl) {
                result_.type = parent_->ctx()->type_name(expr->type_id_);
                result_.decl = exp->decl;
                result_.comment = parent_->ast().find_comment(result_.decl->range_.start.line);
                result_.view = parent_->view_from_range(result_.decl->range());
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
        if (stmt->range.start.line > pos_.line) { return; }
        for (auto decl : stmt->nodes) {
            decl->accept(this);
            if (found_) { return; }
        }
    }

    virtual void visit(DeclList* stmt)
    {
        for (auto decl : stmt->decls) {
            decl->accept(this);
            if (found_) { return; }
        }
    }

    virtual void visit(DoStatement* stmt)
    {
        stmt->expr->accept(this);
        stmt->block->accept(this);
    }

    virtual void visit(EmptyStatement* stmt)
    {
        // No Op
    }

    virtual void visit(ExprStatement* stmt)
    {
        stmt->expr->accept(this);
    }

    virtual void visit(IfStatement* stmt)
    {
        stmt->expr->accept(this);
        if (found_) { return; }
        stmt->if_branch->accept(this);
        if (found_) { return; }
        if (stmt->else_branch) {
            stmt->else_branch->accept(this);
        }
    }

    virtual void visit(ForStatement* stmt)
    {
        stmt->init->accept(this);
        if (found_) { return; }
        stmt->check->accept(this);
        if (found_) { return; }
        stmt->inc->accept(this);
        if (found_) { return; }
        stmt->block->accept(this);
    }

    virtual void visit(JumpStatement* stmt)
    {
        if (stmt->expr) {
            stmt->expr->accept(this);
        }
    }

    virtual void visit(LabelStatement* stmt)
    {
        if (stmt->expr) {
            stmt->expr->accept(this);
        }
    }

    virtual void visit(SwitchStatement* stmt)
    {
        stmt->target->accept(this);
        if (found_) { return; }
        stmt->block->accept(this);
    }

    virtual void visit(WhileStatement* stmt)
    {
        stmt->check->accept(this);
        if (found_) { return; }
        stmt->block->accept(this);
    }
};

} // namespace nw::script
