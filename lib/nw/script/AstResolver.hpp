#pragma once

#include "../log.hpp"
#include "../util/string.hpp"
#include "../util/templates.hpp"
#include "Context.hpp"
#include "Nss.hpp"

#include <unordered_map>
#include <vector>

namespace nw::script {

struct ScopeDecl {
    bool ready = false;
    Declaration* decl = nullptr;
};

struct AstResolver : BaseVisitor {
    AstResolver(Nss* parent, std::shared_ptr<Context> ctx)
        : parent_{parent}
        , ctx_{ctx}
    {
    }

    virtual ~AstResolver() = default;

    using ScopeMap = std::unordered_map<std::string, ScopeDecl>;
    using ScopeStack = std::vector<ScopeMap>;

    Nss* parent_ = nullptr;
    std::shared_ptr<Context> ctx_;
    ScopeStack scope_stack_;
    int loop_stack_ = 0;
    int switch_stack_ = 0;

    // == Resolver Helpers ====================================================
    // ========================================================================

    void begin_scope()
    {
        scope_stack_.push_back(ScopeMap{});
    }

    void declare(NssToken token, Declaration* decl)
    {
        auto s = std::string(token.loc.view());
        auto it = scope_stack_.back().find(s);
        if (it != std::end(scope_stack_.back())) {
            if (dynamic_cast<FunctionDecl*>(it->second.decl)
                && dynamic_cast<FunctionDefinition*>(decl)) {
                it->second.decl = decl;
            } else {
                ctx_->semantic_error(parent_,
                    fmt::format("declaring '{}' in the same scope twice", token.loc.view()),
                    token.loc);
                return;
            }
        } else {
            scope_stack_.back().insert({s, {false, decl}});
        }
    }

    void define(NssToken token)
    {
        auto s = std::string(token.loc.view());
        auto it = scope_stack_.back().find(s);
        if (it == std::end(scope_stack_.back())) {
            ctx_->semantic_error(parent_,
                fmt::format("defining unknown variable '{}'", token.loc.view()),
                token.loc);
        }
        it->second.ready = true;
    }

    void end_scope()
    {
        scope_stack_.pop_back();
    }

    Declaration* locate(std::string_view token, Nss* script)
    {
        if (auto decl = script->locate_export(token)) {
            return decl;
        } else {
            for (auto& it : reverse(script->ast().includes)) {
                if (auto decl = locate(token, it)) {
                    return decl;
                }
            }
        }
        return nullptr;
    }

    Declaration* resolve(std::string_view token, SourceLocation loc)
    {
        auto s = std::string(token);

        // Look first through the scope stack in the current script
        for (const auto& map : reverse(scope_stack_)) {
            auto it = map.find(s);
            if (it == std::end(map)) { continue; }
            if (!it->second.ready) {
                ctx_->semantic_error(parent_,
                    fmt::format("using declared variable '{}' in init", token),
                    loc);
            }
            return it->second.decl;
        }

        // Next look through all dependencies
        for (auto it : reverse(parent_->ast().includes)) {
            if (auto decl = locate(token, it)) { return decl; }
        }

        auto nwscript = ctx_->get({"nwscript"}, ctx_);
        if (nwscript) {
            nwscript->resolve();
            return nwscript->locate_export(token);
        }

        return nullptr;
    }

    // == Visitor =============================================================
    // ========================================================================

    virtual void visit(Ast* script) override
    {
        begin_scope();
        for (const auto& decl : script->decls) {
            decl->accept(this);
            if (auto d = dynamic_cast<VarDecl*>(decl.get())) {
                d->is_const_ = true; // All top level var decls are constant.  Only thing that makes sense.
                parent_->add_export(std::string(d->identifier.loc.view()), d);
            } else if (auto d = dynamic_cast<StructDecl*>(decl.get())) {
                parent_->add_export(std::string(d->type.struct_id.loc.view()), d);
            } else if (auto d = dynamic_cast<FunctionDecl*>(decl.get())) {
                parent_->add_export(std::string(d->identifier.loc.view()), d);
            } else if (auto d = dynamic_cast<FunctionDefinition*>(decl.get())) {
                parent_->add_export(std::string(d->decl->identifier.loc.view()), d);
            }
        }
        end_scope();
    }

    // Decls
    virtual void visit(FunctionDecl* decl) override
    {
        decl->type_id_ = ctx_->type_id(decl->type);
        declare(decl->identifier, decl);
        define(decl->identifier);

        begin_scope();
        for (auto& p : decl->params) {
            p->accept(this);
        }
        end_scope();
    }

    virtual void visit(FunctionDefinition* decl) override
    {
        // Check to see if there's been a function declaration, if so got to match.
        auto fd = resolve(decl->decl->identifier.loc.view(), decl->decl->identifier.loc);

        decl->decl->type_id_ = ctx_->type_id(decl->decl->type);
        declare(decl->decl->identifier, decl);
        define(decl->decl->identifier);

        begin_scope();
        for (auto& p : decl->decl->params) {
            p->accept(this);
        }
        decl->block->accept(this);
        end_scope();
    }

    virtual void visit(StructDecl* decl) override
    {
        declare(decl->type.struct_id, decl);
        decl->type_id_ = ctx_->type_id(decl->type);
        begin_scope();
        for (auto& it : decl->decls) {
            it->accept(this);
        }
        end_scope();
        // Define down here so there's no recursive elements
        define(decl->type.struct_id);
    }

    virtual void visit(VarDecl* decl) override
    {
        decl->is_const_ = decl->type.type_qualifier.type == NssTokenType::CONST_;
        decl->type_id_ = ctx_->type_id(decl->type);

        if (decl->is_const_ && !decl->init) {
            ctx_->semantic_error(parent_, "constant variable declaration with no initializer");
        }

        declare(decl->identifier, decl);
        if (decl->init) {
            decl->init->accept(this);
            if (decl->type_id_ != decl->init->type_id_) {
                ctx_->semantic_error(parent_, "mismatched types in variable initializer", decl->identifier.loc);
            }
        }
        define(decl->identifier);
    }

    // Expressions
    virtual void visit(AssignExpression* expr) override
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);
        if (expr->lhs->type_id_ != expr->rhs->type_id_) {
            ctx_->semantic_error(parent_,
                fmt::format("attempting to assign a value of type '{}' to a variable of type '{}'",
                    ctx_->type_name(expr->lhs->type_id_), ctx_->type_name(expr->rhs->type_id_)),
                expr->op.loc);
        }
        expr->type_id_ = expr->lhs->type_id_;
    }

    virtual void visit(BinaryExpression* expr) override
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        expr->is_const_ = expr->lhs->is_const_ && expr->rhs->is_const_;

        if (expr->lhs->type_id_ != expr->rhs->type_id_) {
            ctx_->semantic_error(parent_, "mismatched types in binary-expression", expr->op.loc);
        }
    }

    virtual void visit(CallExpression* expr) override
    {
        expr->expr->accept(this);
        expr->type_id_ = expr->expr->type_id_;
        for (const auto& arg : expr->args) {
            arg->accept(this);
        }
    }

    virtual void visit(ConditionalExpression* expr) override
    {
        expr->test->accept(this);
        if (expr->test->type_id_ != ctx_->type_id("int")) {
            ctx_->semantic_error(parent_,
                fmt::format("could not convert value to integer bool"));
        }

        expr->true_branch->accept(this);
        expr->false_branch->accept(this);

        if (expr->true_branch->type_id_ != expr->false_branch->type_id_) {
            ctx_->semantic_error(parent_,
                fmt::format("conditional expression mismatched types"));
        }
    }

    virtual void visit(DotExpression* expr) override
    {
        auto resolve_struct_member = [this](VariableExpression* var, StructDecl* str) {
            for (const auto& it : str->decls) {
                if (it->identifier.loc.view() == var->var.loc.view()) {
                    var->type_id_ = it->type_id_;
                    var->is_const_ = it->is_const_;
                    return true;
                }
            }
            return false;
        };

        StructDecl* struct_decl = nullptr;

        auto ex_lhs = dynamic_cast<VariableExpression*>(expr->lhs.get());

        if (ctx_->struct_stack_.size() == 0) {
            ex_lhs->accept(this);
            if (!ex_lhs) {
                ctx_->semantic_error(parent_, "ill-formed dot operator", ex_lhs->var.loc);
                return;
            }

            auto st_decl = resolve(ctx_->type_name(ex_lhs->type_id_), ex_lhs->var.loc);
            struct_decl = dynamic_cast<StructDecl*>(st_decl);
        } else {
            struct_decl = ctx_->struct_stack_.back();
            bool found = resolve_struct_member(ex_lhs, struct_decl);
            if (!found) {
                ctx_->semantic_error(parent_,
                    fmt::format("'{}' is not a member of struct '{}'",
                        ex_lhs->var.loc.view(), struct_decl->type.struct_id.loc.view()),
                    ex_lhs->var.loc);
            }

            auto st_decl = resolve(ctx_->type_name(ex_lhs->type_id_), ex_lhs->var.loc);
            struct_decl = dynamic_cast<StructDecl*>(st_decl);
        }

        if (!struct_decl) {
            ctx_->semantic_error(parent_,
                fmt::format("dot operator on non-struct type, '{}'", ex_lhs->type_id_),
                ex_lhs->var.loc);
            return;
        }

        ctx_->struct_stack_.push_back(struct_decl);

        if (dynamic_cast<DotExpression*>(expr->rhs.get())) {
            expr->rhs->accept(this);
        } else {
            auto ex_rhs = dynamic_cast<VariableExpression*>(expr->rhs.get());
            bool found = resolve_struct_member(ex_rhs, struct_decl);
            if (!found) {
                ctx_->semantic_error(parent_,
                    fmt::format("'{}' is not a member of struct '{}'",
                        ex_rhs->var.loc.view(), struct_decl->type.struct_id.loc.view()),
                    ex_rhs->var.loc);
            }
        }

        expr->type_id_ = expr->rhs->type_id_;
        expr->is_const_ = expr->rhs->is_const_;

        ctx_->struct_stack_.pop_back();
    }

    virtual void visit(GroupingExpression* expr) override
    {
        expr->expr->accept(this);
        expr->type_id_ = expr->expr->type_id_;
        expr->is_const_ = expr->expr->is_const_;
    }

    virtual void visit(LiteralExpression* expr) override
    {
        expr->is_const_ = true;
        if (expr->literal.type == NssTokenType::FLOAT_CONST) {
            expr->type_id_ = ctx_->type_id("float");
        } else if (expr->literal.type == NssTokenType::INTEGER_CONST) {
            expr->type_id_ = ctx_->type_id("int");
        } else if (expr->literal.type == NssTokenType::STRING_CONST) {
            expr->type_id_ = ctx_->type_id("string");
        } else if (expr->literal.type == NssTokenType::OBJECT_SELF_CONST
            || expr->literal.type == NssTokenType::OBJECT_INVALID_CONST) {
            expr->type_id_ = ctx_->type_id("object");
        }
    }

    virtual void visit(LiteralVectorExpression* expr) override
    {
        expr->is_const_ = true;
        expr->type_id_ = ctx_->type_id("vector");
    }

    virtual void visit(LogicalExpression* expr) override
    {
        expr->lhs->accept(this);
        expr->rhs->accept(this);

        if (expr->lhs->type_id_ != expr->rhs->type_id_) {
            ctx_->semantic_error(parent_, "mismatched types", {});
        }

        expr->type_id_ = expr->lhs->type_id_;
        expr->is_const_ = expr->lhs->is_const_ && expr->rhs->is_const_;
    }

    virtual void visit(PostfixExpression* expr) override
    {
        expr->lhs->accept(this);
        expr->type_id_ = expr->lhs->type_id_;
        expr->is_const_ = expr->lhs->is_const_;
    }

    virtual void visit(UnaryExpression* expr) override
    {
        expr->rhs->accept(this);
        expr->type_id_ = expr->rhs->type_id_;
        expr->is_const_ = expr->rhs->is_const_;
    }

    virtual void visit(VariableExpression* expr) override
    {
        auto decl = resolve(expr->var.loc.view(), expr->var.loc);
        if (decl) {
            expr->type_id_ = decl->type_id_;
            expr->is_const_ = decl->is_const_;
        } else {
            ctx_->semantic_error(parent_,
                fmt::format("unable to resolve identifier '{}'", expr->var.loc.view()),
                expr->var.loc);
        }
    }

    // Statements
    virtual void visit(BlockStatement* stmt) override
    {
        stmt->type_id_ = ctx_->type_id("void");
        for (auto& s : stmt->nodes) {
            s->accept(this);
        }
    }

    virtual void visit(DeclStatement* stmt) override
    {
        size_t ti = invalid_type_id;
        for (auto& s : stmt->decls) {
            // types of all must be the same;
            s->accept(this);
            if (ti == invalid_type_id) {
                ti = s->type_id_;
            } else {
                if (ti != s->type_id_) {
                }
            }
        }
    }

    virtual void visit(DoStatement* stmt) override
    {
        ++loop_stack_;
        begin_scope();
        stmt->block->accept(this);
        end_scope();
        stmt->expr->accept(this);
        --loop_stack_;
    }

    virtual void visit(EmptyStatement* stmt) override
    {
        stmt->type_id_ = ctx_->type_id("void");
    }

    virtual void visit(ExprStatement* stmt) override
    {
        stmt->expr->accept(this);
    }

    virtual void visit(IfStatement* stmt) override
    {
        stmt->type_id_ = ctx_->type_id("void");
        stmt->expr->accept(this);

        if (stmt->expr->type_id_ != ctx_->type_id("int")) {
            ctx_->semantic_error(parent_,
                fmt::format("could not convert value to integer bool"));
        }

        stmt->if_branch->accept(this);
        if (stmt->else_branch) {
            stmt->else_branch->accept(this);
        }
    }

    virtual void visit(ForStatement* stmt) override
    {
        ++loop_stack_;
        begin_scope();
        stmt->init->accept(this);
        stmt->check->accept(this);
        stmt->inc->accept(this);
        stmt->block->accept(this);
        end_scope();
        --loop_stack_;
    }

    virtual void visit(JumpStatement* stmt) override
    {
        if (stmt->expr) {
            stmt->expr->accept(this);
            stmt->type_id_ = stmt->expr->type_id_;
        } else {
            stmt->type_id_ = ctx_->type_id("void");
        }

        if (stmt->op.type == NssTokenType::CONTINUE
            && loop_stack_ == 0) {
            ctx_->semantic_error(parent_, "continue statement not within a loop", stmt->op.loc);
        } else if (stmt->op.type == NssTokenType::BREAK
            && (loop_stack_ == 0 || switch_stack_ == 0)) {
            ctx_->semantic_error(parent_, "break statement not within loop or switch", stmt->op.loc);

            // This shouldn't even be possible and would be a parser error
            ctx_->semantic_error(parent_, "return statement not within function definition", stmt->op.loc);
        }
    }

    virtual void visit(LabelStatement* stmt) override
    {
        if (stmt->type.type == NssTokenType::CASE && switch_stack_ == 0) {
            ctx_->semantic_error(parent_, "case statement not within switch", stmt->type.loc);
        }
        stmt->expr->accept(this);

        if (stmt->expr->type_id_ != ctx_->type_id("int")
            && stmt->expr->type_id_ != ctx_->type_id("string")) {
            ctx_->semantic_error(parent_,
                fmt::format("could not convert value to integer or string"));
        } else if (!stmt->expr->is_const_) {
            ctx_->semantic_error(parent_, "case expression must be constant expression");
        }
    }

    virtual void visit(SwitchStatement* stmt) override
    {
        stmt->type_id_ = ctx_->type_id("void");
        ++switch_stack_;
        stmt->target->accept(this);

        // this could have string type also, but when the string case statements
        // were added to NWscript, the NWN:EE team half-assed this from what
        // I could tell.
        if (stmt->target->type_id_ != ctx_->type_id("int")) {
            ctx_->semantic_error(parent_,
                fmt::format("could not convert value to integer"));
        }

        begin_scope();
        stmt->block->accept(this);
        end_scope();
        --switch_stack_;
    }

    virtual void visit(WhileStatement* stmt) override
    {
        stmt->type_id_ = ctx_->type_id("void");
        ++loop_stack_;
        stmt->check->accept(this);

        begin_scope();
        stmt->block->accept(this);
        end_scope();
        --loop_stack_;
    }
};

} // namespace nw::script