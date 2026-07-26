#pragma once

#include "Ast.hpp"

namespace nw::smalls {

/// Visitor that descends into every child node and does nothing else.
///
/// Derive tooling visitors from this rather than from ``NullVisitor``.
/// ``NullVisitor`` implements every ``visit`` as an empty body, so any node
/// kind a derived visitor does not override terminates the walk at that node
/// and every descendant is skipped.  Deriving from ``WalkVisitor`` fails safe:
/// an unhandled node loses only itself, and a node kind added to
/// ``BaseVisitor`` later degrades instead of silently blanking a subtree.
///
/// Overrides call the base implementation to continue the descent:
/// @code
/// void visit(FunctionDefinition* decl) override
/// {
///     record(decl);
///     WalkVisitor::visit(decl);
/// }
/// @endcode
///
/// @note ``DeclList`` shares one ``TypeExpression`` with each of its child
/// ``VarDecl`` nodes, so a multi-declarator statement such as ``var a, b: int``
/// walks that type expression once per declarator.  Consumers that must not
/// double count are responsible for deduplicating.
struct WalkVisitor : BaseVisitor {
    void accept_node(AstNode* node)
    {
        if (node) { node->accept(this); }
    }

    template <typename Container>
    void accept_all(const Container& nodes)
    {
        for (auto* node : nodes) { accept_node(node); }
    }

    /// Walks the expressions held by a declaration's annotation arguments.
    virtual void accept_annotations(Declaration* decl)
    {
        for (auto& annotation : decl->annotations_) {
            for (auto& arg : annotation.args) {
                accept_node(arg.value);
            }
        }
    }

    /// Called once for every node the walk reaches, before its children.
    /// Consumers that need every node override this instead of all forty
    /// ``visit`` overloads.
    virtual void enter(AstNode*) { }

    /// @note ``Ast`` is not an ``AstNode``, so it does not reach ``enter``.
    void visit(Ast* script) override
    {
        accept_all(script->decls);
    }

    // -- Declarations --------------------------------------------------------

    void visit(FunctionDefinition* decl) override
    {
        enter(decl);
        accept_annotations(decl);
        accept_all(decl->params);
        accept_node(decl->return_type);
        accept_node(decl->block);
    }

    void visit(StructDecl* decl) override
    {
        enter(decl);
        accept_annotations(decl);
        accept_node(decl->type);
        accept_all(decl->decls);
    }

    void visit(SumDecl* decl) override
    {
        enter(decl);
        accept_annotations(decl);
        accept_node(decl->type);
        accept_all(decl->variants);
    }

    void visit(VariantDecl* decl) override
    {
        enter(decl);
        accept_node(decl->payload);
    }

    void visit(VarDecl* decl) override
    {
        enter(decl);
        accept_annotations(decl);
        accept_node(decl->type);
        accept_node(decl->init);
    }

    void visit(TypeAlias* decl) override
    {
        enter(decl);
        accept_annotations(decl);
        accept_node(decl->type);
        accept_node(decl->aliased_type);
    }

    void visit(NewtypeDecl* decl) override
    {
        enter(decl);
        accept_annotations(decl);
        accept_node(decl->type);
        accept_node(decl->wrapped_type);
    }

    void visit(OpaqueTypeDecl* decl) override
    {
        enter(decl);
        accept_annotations(decl);
        accept_node(decl->type);
    }

    void visit(AliasedImportDecl* decl) override
    {
        enter(decl);
        accept_annotations(decl);
    }

    void visit(SelectiveImportDecl* decl) override
    {
        enter(decl);
        accept_annotations(decl);
    }

    /// @note Does not walk ``DeclList::type``; each child ``VarDecl`` holds the
    /// same pointer and walks it.
    void visit(DeclList* decl) override
    {
        enter(decl);
        accept_annotations(decl);
        accept_all(decl->decls);
    }

    // -- Expressions ---------------------------------------------------------

    void visit(AssignExpression* expr) override
    {
        enter(expr);
        accept_node(expr->lhs);
        accept_node(expr->rhs);
    }

    void visit(BinaryExpression* expr) override
    {
        enter(expr);
        accept_node(expr->lhs);
        accept_node(expr->rhs);
    }

    void visit(ComparisonExpression* expr) override
    {
        enter(expr);
        accept_node(expr->lhs);
        accept_node(expr->rhs);
    }

    void visit(LogicalExpression* expr) override
    {
        enter(expr);
        accept_node(expr->lhs);
        accept_node(expr->rhs);
    }

    void visit(BraceInitLiteral* expr) override
    {
        enter(expr);
        for (auto& item : expr->items) {
            accept_node(item.key);
            accept_node(item.value);
        }
    }

    void visit(CallExpression* expr) override
    {
        enter(expr);
        accept_node(expr->expr);
        accept_all(expr->args);
    }

    void visit(CastExpression* expr) override
    {
        enter(expr);
        accept_node(expr->expr);
        accept_node(expr->target_type);
    }

    void visit(ConditionalExpression* expr) override
    {
        enter(expr);
        accept_node(expr->test);
        accept_node(expr->true_branch);
        accept_node(expr->false_branch);
    }

    void visit(PathExpression* expr) override
    {
        enter(expr);
        accept_all(expr->parts);
    }

    void visit(EmptyExpression* node) override { enter(node); }

    void visit(GroupingExpression* expr) override
    {
        enter(expr);
        accept_node(expr->expr);
    }

    void visit(TupleLiteral* expr) override
    {
        enter(expr);
        accept_all(expr->elements);
    }

    void visit(IndexExpression* expr) override
    {
        enter(expr);
        accept_node(expr->target);
        accept_node(expr->index);
    }

    void visit(LiteralExpression* node) override { enter(node); }

    void visit(FStringExpression* expr) override
    {
        enter(expr);
        accept_all(expr->expressions);
    }

    void visit(TypeExpression* expr) override
    {
        enter(expr);
        accept_node(expr->name);
        accept_all(expr->params);
        accept_node(expr->fn_return_type);
    }

    void visit(UnaryExpression* expr) override
    {
        enter(expr);
        accept_node(expr->rhs);
    }

    void visit(IdentifierExpression* expr) override
    {
        enter(expr);
        accept_all(expr->type_params);
    }

    void visit(LambdaExpression* expr) override
    {
        enter(expr);
        accept_all(expr->params);
        accept_node(expr->return_type);
        accept_node(expr->body);
    }

    // -- Statements ----------------------------------------------------------

    void visit(BlockStatement* stmt) override
    {
        enter(stmt);
        accept_all(stmt->nodes);
    }

    void visit(EmptyStatement* node) override { enter(node); }

    void visit(ExprStatement* stmt) override
    {
        enter(stmt);
        accept_node(stmt->expr);
    }

    void visit(IfStatement* stmt) override
    {
        enter(stmt);
        accept_node(stmt->expr);
        accept_node(stmt->if_branch);
        accept_node(stmt->else_branch);
    }

    void visit(ForStatement* stmt) override
    {
        enter(stmt);
        accept_node(stmt->init);
        accept_node(stmt->check);
        accept_node(stmt->inc);
        accept_node(stmt->block);
    }

    void visit(ForEachStatement* stmt) override
    {
        enter(stmt);
        accept_node(stmt->var);
        accept_node(stmt->key_var);
        accept_node(stmt->value_var);
        accept_node(stmt->collection);
        accept_node(stmt->block);
    }

    void visit(JumpStatement* stmt) override
    {
        enter(stmt);
        accept_all(stmt->exprs);
    }

    void visit(LabelStatement* stmt) override
    {
        enter(stmt);
        accept_node(stmt->expr);
        accept_all(stmt->bindings);
        accept_node(stmt->guard);
    }

    void visit(SwitchStatement* stmt) override
    {
        enter(stmt);
        accept_node(stmt->target);
        accept_node(stmt->block);
    }
};

} // namespace nw::smalls
