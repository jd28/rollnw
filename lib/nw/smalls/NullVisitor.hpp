#pragma once

#include "Ast.hpp"

namespace nw::smalls {

struct NullVisitor : BaseVisitor {
    void visit(Ast*) override { }
    void visit(FunctionDefinition*) override { }
    void visit(StructDecl*) override { }
    void visit(SumDecl*) override { }
    void visit(VariantDecl*) override { }
    void visit(VarDecl*) override { }
    void visit(TypeAlias*) override { }
    void visit(NewtypeDecl*) override { }
    void visit(OpaqueTypeDecl*) override { }
    void visit(AliasedImportDecl*) override { }
    void visit(SelectiveImportDecl*) override { }
    void visit(AssignExpression*) override { }
    void visit(BinaryExpression*) override { }
    void visit(BraceInitLiteral*) override { }
    void visit(CallExpression*) override { }
    void visit(CastExpression*) override { }
    void visit(ComparisonExpression*) override { }
    void visit(ConditionalExpression*) override { }
    void visit(PathExpression*) override { }
    void visit(EmptyExpression*) override { }
    void visit(GroupingExpression*) override { }
    void visit(TupleLiteral*) override { }
    void visit(IndexExpression*) override { }
    void visit(LiteralExpression*) override { }
    void visit(FStringExpression*) override { }
    void visit(LogicalExpression*) override { }
    void visit(TypeExpression*) override { }
    void visit(UnaryExpression*) override { }
    void visit(IdentifierExpression*) override { }
    void visit(LambdaExpression*) override { }
    void visit(BlockStatement*) override { }
    void visit(DeclList*) override { }
    void visit(EmptyStatement*) override { }
    void visit(ExprStatement*) override { }
    void visit(IfStatement*) override { }
    void visit(ForStatement*) override { }
    void visit(ForEachStatement*) override { }
    void visit(JumpStatement*) override { }
    void visit(LabelStatement*) override { }
    void visit(SwitchStatement*) override { }
};

} // namespace nw::smalls
