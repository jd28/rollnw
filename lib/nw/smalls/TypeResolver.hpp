#pragma once

#include "AstResolver.hpp"

namespace nw::smalls {

struct TypeResolver : BaseVisitor {
    explicit TypeResolver(AstResolver& ctx);

    bool try_resolve_variant_call(CallExpression* expr, PathExpression* path_expr);

    void visit(Ast* script) override;
    void visit(FunctionDefinition* decl) override;
    void visit(StructDecl* decl) override;
    void visit(SumDecl* decl) override;
    void visit(VariantDecl* decl) override;
    void visit(VarDecl* decl) override;
    void visit(TypeAlias* decl) override;
    void visit(NewtypeDecl* decl) override;
    void visit(OpaqueTypeDecl* decl) override;
    void visit(AliasedImportDecl* decl) override;
    void visit(SelectiveImportDecl* decl) override;

    void visit(AssignExpression* expr) override;
    void visit(BinaryExpression* expr) override;
    void visit(BraceInitLiteral* expr) override;
    void visit(CallExpression* expr) override;
    void visit(CastExpression* expr) override;
    void visit(ComparisonExpression* expr) override;
    void visit(ConditionalExpression* expr) override;
    void visit(PathExpression* expr) override;
    void visit(EmptyExpression* expr) override;
    void visit(GroupingExpression* expr) override;
    void visit(TupleLiteral* expr) override;
    void visit(IndexExpression* expr) override;
    void visit(LiteralExpression* expr) override;
    void visit(FStringExpression* expr) override;
    void visit(LogicalExpression* expr) override;
    void visit(TypeExpression* expr) override;
    void visit(UnaryExpression* expr) override;
    void visit(IdentifierExpression* expr) override;
    void visit(LambdaExpression* expr) override;

    void visit(BlockStatement* stmt) override;
    void visit(DeclList* stmt) override;
    void visit(EmptyStatement* stmt) override;
    void visit(ExprStatement* stmt) override;
    void visit(IfStatement* stmt) override;
    void visit(ForStatement* stmt) override;
    void visit(ForEachStatement* stmt) override;
    void visit(JumpStatement* stmt) override;
    void visit(LabelStatement* stmt) override;
    void visit(SwitchStatement* stmt) override;

    AstResolver& ctx;
};

} // namespace nw::smalls
