#pragma once

#include "Smalls.hpp"
#include "WalkVisitor.hpp"
#include "runtime.hpp"

namespace nw::smalls {

/// Produces inlay hints for a source range.
///
/// Derives from ``WalkVisitor`` so an unhandled node kind loses only its own
/// hint instead of every hint beneath it. The previous hand-written traversal
/// had explicit no-ops for ``DeclList`` and ``LabelStatement``, which silently
/// dropped every hint inside a multi-declarator statement or a ``case`` arm.
struct AstHinter : public WalkVisitor {
    AstHinter(Script* parent, SourceRange range, InlayHintOptions options = {})
        : parent_{parent}
        , range_{range}
        , options_{options}
    {
    }

    virtual ~AstHinter() = default;

    /// Declaring overloads below hides the rest, including ``visit(Ast*)``.
    using WalkVisitor::visit;

    Script* parent_ = nullptr;
    SourceRange range_;
    InlayHintOptions options_;
    Vector<InlayHint> result_;

    // -- Declarations --------------------------------------------------------

    void visit(FunctionDefinition* decl) override
    {
        // A parameter's written type is already on screen; only its default
        // value can carry hints.
        for (auto* param : decl->params) {
            if (param && param->init) { param->init->accept(this); }
        }
        if (decl->block) { decl->block->accept(this); }
    }

    void visit(VarDecl* decl) override
    {
        add_variable_type_hint(decl);
        if (decl->init) { decl->init->accept(this); }
    }

    void visit(StructDecl*) override { }
    void visit(SumDecl*) override { }
    void visit(VariantDecl*) override { }
    void visit(TypeAlias*) override { }
    void visit(NewtypeDecl*) override { }
    void visit(OpaqueTypeDecl*) override { }
    void visit(AliasedImportDecl*) override { }
    void visit(SelectiveImportDecl*) override { }

    // -- Expressions ---------------------------------------------------------

    void visit(CallExpression* expr) override
    {
        add_parameter_hints(expr);
        WalkVisitor::visit(expr);
    }

    void visit(LambdaExpression* expr) override
    {
        add_lambda_return_hint(expr);
        WalkVisitor::visit(expr);
    }

    void visit(TypeExpression*) override { }
    void visit(IdentifierExpression*) override { }

    // -- Statements ----------------------------------------------------------

    void visit(ForEachStatement* stmt) override
    {
        if (options_.foreach_types) {
            add_binding_type_hint(stmt->var, stmt->element_type);
            add_binding_type_hint(stmt->key_var, stmt->key_type);
            add_binding_type_hint(stmt->value_var, stmt->value_type);
        }
        if (stmt->collection) { stmt->collection->accept(this); }
        if (stmt->block) { stmt->block->accept(this); }
    }

private:
    bool in_range(SourceRange range) const
    {
        return contains_range(range_, range);
    }

    StringView type_name_of(TypeID type_id) const
    {
        if (type_id == invalid_type_id) { return {}; }
        return nw::kernel::runtime().type_name(type_id);
    }

    /// Resolves the declaration a type name refers to, so the hint can be
    /// clickable rather than inert text.
    const Declaration* declaration_for_type(StringView name) const
    {
        if (!parent_ || name.empty()) { return nullptr; }
        return parent_->locate_export(String(name), true).decl;
    }

    void add_type_hint(SourcePosition position, StringView name)
    {
        if (name.empty()) { return; }
        result_.push_back(
            {String(name), position, InlayHintKind::type, declaration_for_type(name)});
    }

    /// Hints the type of a ``var`` the author did not write one for.
    void add_variable_type_hint(VarDecl* decl)
    {
        if (!options_.variable_types || decl->type || !in_range(decl->range_)) {
            return;
        }
        add_type_hint(decl->identifier_.loc.range.end, type_name_of(decl->type_id_));
    }

    void add_binding_type_hint(VarDecl* decl, TypeID type_id)
    {
        if (!decl || decl->type || !in_range(decl->range_)) { return; }
        add_type_hint(decl->identifier_.loc.range.end, type_name_of(type_id));
    }

    void add_lambda_return_hint(LambdaExpression* expr)
    {
        if (!options_.lambda_return_types || expr->return_type || !in_range(expr->range_)) {
            return;
        }
        add_type_hint(expr->range_.start, type_name_of(expr->type_id_));
    }

    /// Resolves the function a call targets, including a module-qualified call
    /// such as ``Array.len(x)``, which the previous implementation skipped.
    static const FunctionDefinition* called_function(CallExpression* expr)
    {
        if (expr->resolved_func) { return expr->resolved_func; }

        IdentifierExpression* identifier = nullptr;
        if (auto* path = dynamic_cast<PathExpression*>(expr->expr)) {
            identifier = path->last_identifier();
        } else {
            identifier = dynamic_cast<IdentifierExpression*>(expr->expr);
        }
        if (!identifier) { return nullptr; }

        if (auto* decl = dynamic_cast<const FunctionDefinition*>(identifier->decl)) {
            return decl;
        }
        auto exported = identifier->env_.find(String(identifier->ident.loc.view()));
        if (exported && exported->decl) {
            return dynamic_cast<const FunctionDefinition*>(exported->decl);
        }
        return nullptr;
    }

    /// True when the argument text already reads as the parameter name, which is
    /// the single largest source of inlay-hint noise.
    static bool argument_restates_parameter(Expression* arg, StringView parameter)
    {
        auto* identifier = as_identifier(arg);
        if (!identifier) { return false; }
        auto text = identifier->ident.loc.view();
        if (text == parameter) { return true; }
        // `p_value` for `value` reads as the same thing.
        return text.size() > parameter.size() && text.ends_with(parameter);
    }

    void add_parameter_hints(CallExpression* expr)
    {
        if (!options_.parameter_names || !in_range(expr->range_)) { return; }

        const FunctionDefinition* function = called_function(expr);
        if (!function) { return; }

        size_t count = std::min(function->params.size(), expr->args.size());
        for (size_t i = 0; i < count; ++i) {
            const auto* param = function->params[i];
            if (!param || !expr->args[i]) { continue; }
            auto name = param->identifier_.loc.view();
            if (name.empty() || argument_restates_parameter(expr->args[i], name)) {
                continue;
            }
            result_.push_back({String(name), expr->args[i]->range_.start,
                InlayHintKind::parameter, param});
        }
    }
};

} // namespace nw::smalls
