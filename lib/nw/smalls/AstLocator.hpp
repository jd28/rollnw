#pragma once

#include "Smalls.hpp"
#include "SourceLocation.hpp"
#include "runtime.hpp"

namespace nw::smalls {

struct AstLocator : public BaseVisitor {
    AstLocator(Script* parent, String symbol, size_t line, size_t character)
        : parent_{parent}
        , symbol_{std::move(symbol)}
        , pos_{line, character}
    {
    }

    // Search data
    const Script* parent_ = nullptr;
    String symbol_;
    SourcePosition pos_;
    bool in_func_decl_ = false;
    bool in_struct_decl_ = false;

    // Result data
    bool found_ = false;
    Symbol result_;
    const Declaration* last_seen_decl = nullptr;
    const PathExpression* path = nullptr;         // Keep track if our symbol is in a path expr
    const CallExpression* call = nullptr;         // Keep track if our symbol is in a call expr
    const BraceInitLiteral* brace_init = nullptr; // Keep track if our symbol is in a brace init
    size_t active_param = 0;

    // -- Visitor -------------------------------------------------------------

    virtual void visit(Ast* script) override
    {
        for (auto decl : script->decls) {
            if (decl) { decl->accept(this); }
            if (found_) { break; }
        }
    }

    // Decls
    virtual void visit(FunctionDefinition* decl) override
    {
        if (decl->range_.end < pos_) { last_seen_decl = decl; }

        if (contains_position(decl->range_, pos_)) {
            // Check return types
            if (decl->return_type) {
                if (decl->return_type->name && contains_position(decl->return_type->name->range_, pos_)) {
                    decl->return_type->name->accept(this);
                    if (found_) { return; }
                }
                for (auto* param : decl->return_type->params) {
                    if (param && contains_position(param->range_, pos_)) {
                        param->accept(this);
                        if (found_) { return; }
                    }
                }
            }

            // Check identifier
            if (contains_position(decl->identifier_.loc.range, pos_)) {
                result_ = parent_->declaration_to_symbol(decl);
                found_ = true;
                return;
            }

            // Check parameters
            in_func_decl_ = true;
            for (auto param : decl->params) {
                if (param) { param->accept(this); }
                if (found_) {
                    in_func_decl_ = false;
                    return;
                }
            }
            in_func_decl_ = false;

            // Check body
            if (decl->block) { decl->block->accept(this); }
        }
    }

    virtual void visit(StructDecl* decl) override
    {
        if (decl->range_.end < pos_) { last_seen_decl = decl; }

        if (decl->type && decl->type->name && contains_position(decl->type->name->range_, pos_)) {
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

    virtual void visit(VarDecl* decl) override
    {
        if (decl->range_.end < pos_ && !in_struct_decl_) { last_seen_decl = decl; }

        if (contains_position(decl->identifier_.loc.range, pos_)) {
            result_ = parent_->declaration_to_symbol(decl);
            if (in_func_decl_) {
                result_.kind = SymbolKind::param;
            } else if (in_struct_decl_) {
                result_.kind = SymbolKind::field;
            }
            found_ = true;
        } else {
            if (decl->type && decl->type->name && contains_position(decl->type->name->range_, pos_)) {
                decl->type->name->accept(this);
                if (found_) { return; }
            }
        }

        if (decl->init) {
            decl->init->accept(this);
        }
    }

    virtual void visit(TypeAlias* decl) override
    {
        if (decl->type && decl->type->name && contains_position(decl->type->name->range_, pos_)) {
            result_ = parent_->declaration_to_symbol(decl);
            result_.kind = SymbolKind::type;
            found_ = true;
        }
    }

    virtual void visit(NewtypeDecl* decl) override
    {
        if (decl->type && decl->type->name && contains_position(decl->type->name->range_, pos_)) {
            result_ = parent_->declaration_to_symbol(decl);
            result_.kind = SymbolKind::type;
            found_ = true;
        }
    }

    virtual void visit(SumDecl*) override
    {
        // TODO: Implement in Phase 9 (LSP Support)
    }

    virtual void visit(VariantDecl*) override
    {
        // TODO: Implement in Phase 9 (LSP Support)
    }

    virtual void visit(OpaqueTypeDecl* decl) override
    {
        if (decl->type && decl->type->name && contains_position(decl->type->name->range_, pos_)) {
            result_ = parent_->declaration_to_symbol(decl);
            result_.kind = SymbolKind::type;
            found_ = true;
        }
    }

    virtual void visit(AliasedImportDecl* decl) override
    {
        if (decl->range_.end < pos_) { last_seen_decl = decl; }

        // Check if clicking on the alias identifier
        if (contains_position(decl->alias.loc.range, pos_)) {
            result_ = parent_->declaration_to_symbol(decl);
            result_.kind = SymbolKind::variable; // Treat imports as variables
            found_ = true;
        }
    }

    virtual void visit(SelectiveImportDecl* decl) override
    {
        if (decl->range_.end < pos_) { last_seen_decl = decl; }

        // Check if clicking on any of the imported symbol names
        for (const auto& symbol_token : decl->imported_symbols) {
            if (contains_position(symbol_token.loc.range, pos_) && symbol_token.loc.view() == symbol_) {
                // Try to find the declaration in the loaded module
                if (decl->loaded_module) {
                    auto symbol_name = String(symbol_token.loc.view());
                    auto module_exports = decl->loaded_module->exports();
                    auto export_ptr = module_exports.find(symbol_name);
                    if (export_ptr) {
                        result_ = decl->loaded_module->export_to_symbol(symbol_token.loc.view(), *export_ptr);
                        found_ = true;
                        return;
                    }
                }
            }
        }
    }

    // Expressions
    virtual void visit(AssignExpression* expr) override
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(BraceInitLiteral* expr) override
    {
        if (contains_position(expr->range_, pos_)) {
            brace_init = expr;
        }

        if (expr->type.type != TokenType::INVALID
            && contains_position(expr->type.loc.range, pos_)
            && expr->type.loc.view() == symbol_) {
            auto exp = expr->env_.find(symbol_);
            if (exp && exp->decl) {
                result_ = parent_->declaration_to_symbol(exp->decl);
                found_ = true;
                return;
            }
        }

        for (auto& item : expr->items) {
            if (item.key) { item.key->accept(this); }
            if (found_) { return; }
            if (item.value) { item.value->accept(this); }
            if (found_) { return; }
        }
    }

    virtual void visit(BinaryExpression* expr) override
    {
        if (expr->lhs) { expr->lhs->accept(this); }
        if (expr->rhs) { expr->rhs->accept(this); }
    }

    virtual void visit(CallExpression* expr) override
    {
        if (expr->expr) { expr->expr->accept(this); }
        if (found_) { return; }

        for (size_t i = 0; i < expr->args.size(); ++i) {
            if (expr->args[i]) {
                expr->args[i]->accept(this);
            }
            if (found_) { return; }
        }

        if (!call && contains_position(expr->arg_range, pos_)) {
            call = expr;
            size_t i = 1;
            for (const auto cr : expr->comma_ranges) {
                if (cr.end <= pos_) {
                    active_param = i;
                } else {
                    break;
                }
                ++i;
            }
        }
    }

    virtual void visit(CastExpression* expr) override
    {
        if (expr->expr) { expr->expr->accept(this); }
        if (found_) { return; }
        if (expr->target_type) { expr->target_type->accept(this); }
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
            if (found_) { return; }
        }

        if (expr->parts.size() < 2) {
            return;
        }

        auto* last_ident = dynamic_cast<IdentifierExpression*>(expr->parts.back());
        if (!last_ident
            || last_ident->ident.loc.view() != symbol_
            || !contains_position(last_ident->ident.loc.range, pos_)) {
            return;
        }

        auto* base_expr = expr->parts[expr->parts.size() - 2];
        if (auto lhs_var = dynamic_cast<IdentifierExpression*>(base_expr)) {
            String lhs_name{lhs_var->ident.loc.view()};
            auto lhs_exp = expr->env_.find(lhs_name);

            if (lhs_exp && lhs_exp->decl) {
                if (auto import_decl = dynamic_cast<const AliasedImportDecl*>(lhs_exp->decl)) {
                    if (import_decl->loaded_module) {
                        auto exports = import_decl->loaded_module->exports();
                        auto type_export = exports.find(symbol_);
                        if (type_export) {
                            result_ = import_decl->loaded_module->export_to_symbol(symbol_, *type_export);
                            result_.provider = import_decl->loaded_module;
                            found_ = true;
                            return;
                        }
                    }
                }
            }
        }
        if (auto lhs_var = dynamic_cast<IdentifierExpression*>(base_expr)) {
            String lhs_name{lhs_var->ident.loc.view()};
            for (auto* import_decl : parent_->ast().imports) {
                if (!import_decl) { continue; }
                if (auto alias_decl = dynamic_cast<AliasedImportDecl*>(import_decl)) {
                    if (alias_decl->alias.loc.view() == lhs_name && alias_decl->loaded_module) {
                        auto exports = alias_decl->loaded_module->exports();
                        auto type_export = exports.find(symbol_);
                        if (type_export) {
                            result_ = alias_decl->loaded_module->export_to_symbol(symbol_, *type_export);
                            result_.provider = alias_decl->loaded_module;
                            found_ = true;
                            return;
                        }
                    }
                }
            }
        }

        auto& rt = nw::kernel::runtime();
        const Type* type = rt.get_type(base_expr->type_id_);
        if (type && type->type_kind == TK_struct) {
            auto struct_id = type->type_params[0].as<StructID>();
            const StructDef* struct_def = rt.type_table_.get(struct_id);
            if (struct_def && struct_def->decl) {
                path = expr;
                auto vd = struct_def->decl->locate_member_decl(symbol_);
                if (vd) {
                    result_ = parent_->declaration_to_symbol(vd);
                    result_.kind = SymbolKind::field;
                    result_.view = parent_->view_from_range(vd->range_selection_);
                    found_ = true;
                }
            }
        }
    }

    virtual void visit(EmptyExpression*) override
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

    virtual void visit(LiteralExpression*) override
    {
        // No Op
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

    virtual void visit(TypeExpression* expr) override
    {
        // Visit the name expression to handle qualified types like module.Type
        if (expr->name) {
            expr->name->accept(this);
        }
    }

    virtual void visit(IdentifierExpression* expr) override
    {
        if (contains_position(expr->ident.loc.range, pos_) && expr->ident.loc.view() == symbol_) {
            if (brace_init && brace_init->init_type == BraceInitType::field) {
                auto& rt = nw::kernel::runtime();
                const Type* type = rt.get_type(brace_init->type_id_);
                if (type && type->type_kind == TK_struct) {
                    auto struct_id = type->type_params[0].as<StructID>();
                    const StructDef* struct_def = rt.type_table_.get(struct_id);
                    if (struct_def && struct_def->decl) {
                        auto vd = struct_def->decl->locate_member_decl(symbol_);
                        if (vd) {
                            result_ = parent_->declaration_to_symbol(vd);
                            result_.kind = SymbolKind::field;
                            result_.view = parent_->view_from_range(vd->range_selection_);
                            result_.node = expr;
                            found_ = true;
                            return;
                        }
                    }
                }
            }

            auto exp = expr->env_.find(symbol_);
            if (exp && exp->decl) {
                result_ = parent_->declaration_to_symbol(exp->decl);
                result_.node = expr;
                found_ = true;
                return;
            }
        }

        if (!found_) {
            auto export_symbol = parent_->locate_export(symbol_, false);
            if (export_symbol.decl) {
                result_ = export_symbol;
                result_.node = expr;
                found_ = true;
            }
        }
    }

    virtual void visit(LambdaExpression* expr) override
    {
        if (contains_position(expr->range_, pos_)) {
            for (auto* param : expr->params) {
                if (param) { param->accept(this); }
                if (found_) { return; }
            }
            if (expr->body) { expr->body->accept(this); }
        }
    }

    // Statements
    virtual void visit(BlockStatement* stmt) override
    {
        // Block is a good place to bail if we've not found the symbol
        if (stmt->range_.start.line > pos_.line) { return; }
        for (auto decl : stmt->nodes) {
            if (decl) { decl->accept(this); }
            if (found_) { return; }
        }
    }

    virtual void visit(DeclList* stmt) override
    {
        for (auto decl : stmt->decls) {
            if (decl) { decl->accept(this); }
            if (found_) { return; }
        }
    }

    virtual void visit(EmptyStatement*) override
    {
        // No Op
    }

    virtual void visit(ExprStatement* stmt) override
    {
        if (stmt->expr) { stmt->expr->accept(this); }
    }

    virtual void visit(IfStatement* stmt) override
    {
        if (stmt->expr) { stmt->expr->accept(this); }
        if (found_) { return; }
        if (stmt->if_branch) { stmt->if_branch->accept(this); }
        if (found_) { return; }
        if (stmt->else_branch) { stmt->else_branch->accept(this); }
    }

    virtual void visit(ForStatement* stmt) override
    {
        if (stmt->init) { stmt->init->accept(this); }
        if (found_) { return; }
        if (stmt->check) { stmt->check->accept(this); }
        if (found_) { return; }
        if (stmt->inc) { stmt->inc->accept(this); }
        if (found_) { return; }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(ForEachStatement* stmt) override
    {
        if (stmt->var) { stmt->var->accept(this); }
        if (found_) { return; }
        if (stmt->key_var) { stmt->key_var->accept(this); }
        if (found_) { return; }
        if (stmt->value_var) { stmt->value_var->accept(this); }
        if (found_) { return; }
        if (stmt->collection) { stmt->collection->accept(this); }
        if (found_) { return; }
        if (stmt->block) { stmt->block->accept(this); }
    }

    virtual void visit(JumpStatement* stmt) override
    {
        for (auto expr : stmt->exprs) {
            if (expr) { expr->accept(this); }
            if (found_) { return; }
        }
    }

    virtual void visit(LabelStatement* stmt) override
    {
        if (stmt->expr) { stmt->expr->accept(this); }
    }

    virtual void visit(SwitchStatement* stmt) override
    {
        if (stmt->target) { stmt->target->accept(this); }
        if (found_) { return; }
        if (stmt->block) { stmt->block->accept(this); }
    }
};

} // namespace nw::smalls
