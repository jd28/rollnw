#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstPrinter.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Parser.hpp>
#include <nw/smalls/Smalls.hpp>

#include <string_view>

using namespace std::literals;

// Expression tests will go here
TEST_F(SmallsParser, BinaryAddition)
{
    auto script = make_script(R"(
        fn test() {
            var x = 1 + 2;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* binary = dynamic_cast<nw::smalls::BinaryExpression*>(var_decl->init);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->op.type, nw::smalls::TokenType::PLUS);
    EXPECT_NE(binary->lhs, nullptr);
    EXPECT_NE(binary->rhs, nullptr);
}

TEST_F(SmallsParser, BinaryMultiplication)
{
    auto script = make_script(R"(
        fn test() {
            var x = 3 * 4;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* binary = dynamic_cast<nw::smalls::BinaryExpression*>(var_decl->init);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->op.type, nw::smalls::TokenType::TIMES);
}

TEST_F(SmallsParser, ComparisonExpression)
{
    auto script = make_script(R"(
        fn test() {
            var x = 5 > 3;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* comp = dynamic_cast<nw::smalls::ComparisonExpression*>(var_decl->init);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->op.type, nw::smalls::TokenType::GT);
    EXPECT_NE(comp->lhs, nullptr);
    EXPECT_NE(comp->rhs, nullptr);
}

TEST_F(SmallsParser, LogicalAndExpression)
{
    auto script = make_script(R"(
        fn test() {
            var x = true && false;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* logical = dynamic_cast<nw::smalls::LogicalExpression*>(var_decl->init);
    ASSERT_NE(logical, nullptr);
    EXPECT_EQ(logical->op.type, nw::smalls::TokenType::ANDAND);
    EXPECT_NE(logical->lhs, nullptr);
    EXPECT_NE(logical->rhs, nullptr);
}

TEST_F(SmallsParser, LogicalOrExpression)
{
    auto script = make_script(R"(
        fn test() {
            var x = true || false;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* logical = dynamic_cast<nw::smalls::LogicalExpression*>(var_decl->init);
    ASSERT_NE(logical, nullptr);
    EXPECT_EQ(logical->op.type, nw::smalls::TokenType::OROR);
}

TEST_F(SmallsParser, UnaryNegation)
{
    auto script = make_script(R"(
        fn test() {
            var x = -42;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    // Constant unary negation is folded into the literal during parsing
    auto* literal = dynamic_cast<nw::smalls::LiteralExpression*>(var_decl->init);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->literal.type, nw::smalls::TokenType::INTEGER_LITERAL);
    EXPECT_EQ(literal->data.get<int32_t>(), -42);
}

TEST_F(SmallsParser, UnaryNot)
{
    auto script = make_script(R"(
        fn test() {
            var x = !true;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* unary = dynamic_cast<nw::smalls::UnaryExpression*>(var_decl->init);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->op.type, nw::smalls::TokenType::NOT);
}

TEST_F(SmallsParser, AssignmentExpression)
{
    auto script = make_script(R"(
        fn test() {
            x = 42;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* expr_stmt = dynamic_cast<nw::smalls::ExprStatement*>(func->block->nodes[0]);
    ASSERT_NE(expr_stmt, nullptr);

    auto* assign = dynamic_cast<nw::smalls::AssignExpression*>(expr_stmt->expr);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->op.type, nw::smalls::TokenType::EQ);
    EXPECT_NE(assign->lhs, nullptr);
    EXPECT_NE(assign->rhs, nullptr);
}

TEST_F(SmallsParser, CompoundAssignment)
{
    auto script = make_script(R"(
        fn test() {
            x += 10;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* expr_stmt = dynamic_cast<nw::smalls::ExprStatement*>(func->block->nodes[0]);
    ASSERT_NE(expr_stmt, nullptr);

    auto* assign = dynamic_cast<nw::smalls::AssignExpression*>(expr_stmt->expr);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->op.type, nw::smalls::TokenType::PLUSEQ);
}

TEST_F(SmallsParser, CallExpression)
{
    auto script = make_script(R"(
        fn test() {
            foo(1, 2, 3);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* expr_stmt = dynamic_cast<nw::smalls::ExprStatement*>(func->block->nodes[0]);
    ASSERT_NE(expr_stmt, nullptr);

    auto* call = dynamic_cast<nw::smalls::CallExpression*>(expr_stmt->expr);
    ASSERT_NE(call, nullptr);
    EXPECT_NE(call->expr, nullptr);
    ASSERT_EQ(call->args.size(), 3);
}

TEST_F(SmallsParser, PathExpression)
{
    auto script = make_script(R"(
        fn test() {
            var x = point.x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* path = dynamic_cast<nw::smalls::PathExpression*>(var_decl->init);
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->parts.size(), 2);
    EXPECT_NE(path->parts[0], nullptr);
    EXPECT_NE(path->parts[1], nullptr);
}

TEST_F(SmallsParser, ConditionalExpression)
{
    auto script = make_script(R"(
        fn test() {
            var x = a ? b : c;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* cond = dynamic_cast<nw::smalls::ConditionalExpression*>(var_decl->init);
    ASSERT_NE(cond, nullptr);
    EXPECT_NE(cond->test, nullptr);
    EXPECT_NE(cond->true_branch, nullptr);
    EXPECT_NE(cond->false_branch, nullptr);
}

TEST_F(SmallsParser, GroupingExpression)
{
    auto script = make_script(R"(
        fn test() {
            var x = (1 + 2) * 3;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* binary = dynamic_cast<nw::smalls::BinaryExpression*>(var_decl->init);
    ASSERT_NE(binary, nullptr);

    auto* grouping = dynamic_cast<nw::smalls::GroupingExpression*>(binary->lhs);
    ASSERT_NE(grouping, nullptr);
    EXPECT_NE(grouping->expr, nullptr);
}

TEST_F(SmallsParser, IdentifierExpression)
{
    auto script = make_script(R"(
        fn test() {
            var y = x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* path = dynamic_cast<nw::smalls::PathExpression*>(var_decl->init);
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->parts.size(), 1);
    auto* ident = dynamic_cast<nw::smalls::IdentifierExpression*>(path->parts[0]);
    ASSERT_NE(ident, nullptr);
    EXPECT_EQ(ident->ident.loc.view(), "x");
}

// ============================================================================
// Complex Expression Tests
// ============================================================================
TEST_F(SmallsParser, OperatorPrecedence)
{
    auto script = make_script(R"(
        fn test() {
            var x = 1 + 2 * 3;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    // Should parse as 1 + (2 * 3)
    auto* add = dynamic_cast<nw::smalls::BinaryExpression*>(var_decl->init);
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add->op.type, nw::smalls::TokenType::PLUS);

    auto* mult = dynamic_cast<nw::smalls::BinaryExpression*>(add->rhs);
    ASSERT_NE(mult, nullptr);
    EXPECT_EQ(mult->op.type, nw::smalls::TokenType::TIMES);
}

TEST_F(SmallsParser, ChainedComparison)
{
    auto script = make_script(R"(
        fn test() {
            var x = a == b && c != d;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* logical = dynamic_cast<nw::smalls::LogicalExpression*>(var_decl->init);
    ASSERT_NE(logical, nullptr);
    EXPECT_EQ(logical->op.type, nw::smalls::TokenType::ANDAND);

    auto* eq = dynamic_cast<nw::smalls::ComparisonExpression*>(logical->lhs);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->op.type, nw::smalls::TokenType::EQEQ);

    auto* neq = dynamic_cast<nw::smalls::ComparisonExpression*>(logical->rhs);
    ASSERT_NE(neq, nullptr);
    EXPECT_EQ(neq->op.type, nw::smalls::TokenType::NOTEQ);
}

TEST_F(SmallsParser, ChainedMemberAccess)
{
    auto script = make_script(R"(
        fn test() {
            var x = get_struct().member1.member2;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->block, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* path = dynamic_cast<nw::smalls::PathExpression*>(var_decl->init);
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(path->parts.size(), 3);

    auto* call = dynamic_cast<nw::smalls::CallExpression*>(path->parts[0]);
    ASSERT_NE(call, nullptr);

    auto* member1 = dynamic_cast<nw::smalls::IdentifierExpression*>(path->parts[1]);
    ASSERT_NE(member1, nullptr);

    auto* member2 = dynamic_cast<nw::smalls::IdentifierExpression*>(path->parts[2]);
    ASSERT_NE(member2, nullptr);
}

TEST_F(SmallsParser, ArrayIndexing)
{
    auto script = make_script(R"(
        fn test() {
            var x = arr[0];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* index = dynamic_cast<nw::smalls::IndexExpression*>(var_decl->init);
    ASSERT_NE(index, nullptr);
    EXPECT_NE(index->target, nullptr);
    EXPECT_NE(index->index, nullptr);

    auto* target = dynamic_cast<nw::smalls::PathExpression*>(index->target);
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(target->parts.size(), 1);

    auto* idx = dynamic_cast<nw::smalls::LiteralExpression*>(index->index);
    ASSERT_NE(idx, nullptr);
}

TEST_F(SmallsParser, MapIndexing)
{
    auto script = make_script(R"(
        fn test() {
            var x = map["key"];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* index = dynamic_cast<nw::smalls::IndexExpression*>(var_decl->init);
    ASSERT_NE(index, nullptr);
    EXPECT_NE(index->target, nullptr);
    EXPECT_NE(index->index, nullptr);

    auto* target = dynamic_cast<nw::smalls::PathExpression*>(index->target);
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(target->parts.size(), 1);

    auto* key = dynamic_cast<nw::smalls::LiteralExpression*>(index->index);
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(key->literal.type, nw::smalls::TokenType::STRING_LITERAL);
}

TEST_F(SmallsParser, TupleLiteralEmpty)
{
    auto script = make_script(R"(
        fn test() {
            var x = ();
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* tuple = dynamic_cast<nw::smalls::TupleLiteral*>(var_decl->init);
    ASSERT_NE(tuple, nullptr);
    EXPECT_EQ(tuple->elements.size(), 0);
}

TEST_F(SmallsParser, TupleLiteralSingle)
{
    auto script = make_script(R"(
        fn test() {
            var x = (42,);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* tuple = dynamic_cast<nw::smalls::TupleLiteral*>(var_decl->init);
    ASSERT_NE(tuple, nullptr);
    EXPECT_EQ(tuple->elements.size(), 1);
}

TEST_F(SmallsParser, TupleLiteralTwo)
{
    auto script = make_script(R"(
        fn test() {
            var x = (1, 2);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* tuple = dynamic_cast<nw::smalls::TupleLiteral*>(var_decl->init);
    ASSERT_NE(tuple, nullptr);
    EXPECT_EQ(tuple->elements.size(), 2);
}

TEST_F(SmallsParser, TupleLiteralMultiple)
{
    auto script = make_script(R"(
        fn test() {
            var coord = (1.0, 2.0, 3.0);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* tuple = dynamic_cast<nw::smalls::TupleLiteral*>(var_decl->init);
    ASSERT_NE(tuple, nullptr);
    EXPECT_EQ(tuple->elements.size(), 3);
}

TEST_F(SmallsParser, GroupingNotTuple)
{
    auto script = make_script(R"(
        fn test() {
            var x = (42);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* grouping = dynamic_cast<nw::smalls::GroupingExpression*>(var_decl->init);
    ASSERT_NE(grouping, nullptr);
}

TEST_F(SmallsParser, TupleLiteralNested)
{
    auto script = make_script(R"(
        fn test() {
            var x = ((1, 2), (3, 4));
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* tuple = dynamic_cast<nw::smalls::TupleLiteral*>(var_decl->init);
    ASSERT_NE(tuple, nullptr);
    EXPECT_EQ(tuple->elements.size(), 2);

    auto* first = dynamic_cast<nw::smalls::TupleLiteral*>(tuple->elements[0]);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->elements.size(), 2);

    auto* second = dynamic_cast<nw::smalls::TupleLiteral*>(tuple->elements[1]);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->elements.size(), 2);
}

TEST_F(SmallsParser, CastExpression)
{
    auto script = make_script(R"(
        fn test() {
            var x = 42 as float;
            var y = 3.14 as int;
            var z = value as MyType;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 3);

    auto* var_x = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_x, nullptr);
    auto* cast_x = dynamic_cast<nw::smalls::CastExpression*>(var_x->init);
    ASSERT_NE(cast_x, nullptr);
    EXPECT_EQ(cast_x->op.type, nw::smalls::TokenType::AS);
    ASSERT_NE(cast_x->expr, nullptr);
    ASSERT_NE(cast_x->target_type, nullptr);

    auto* var_y = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[1]);
    ASSERT_NE(var_y, nullptr);
    auto* cast_y = dynamic_cast<nw::smalls::CastExpression*>(var_y->init);
    ASSERT_NE(cast_y, nullptr);
    EXPECT_EQ(cast_y->op.type, nw::smalls::TokenType::AS);

    auto* var_z = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[2]);
    ASSERT_NE(var_z, nullptr);
    auto* cast_z = dynamic_cast<nw::smalls::CastExpression*>(var_z->init);
    ASSERT_NE(cast_z, nullptr);
    EXPECT_EQ(cast_z->op.type, nw::smalls::TokenType::AS);
}

TEST_F(SmallsParser, IsExpression)
{
    auto script = make_script(R"(
        fn test() {
            var x = value is int;
            var y = data is MyType;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 2);

    auto* var_x = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_x, nullptr);
    auto* is_x = dynamic_cast<nw::smalls::CastExpression*>(var_x->init);
    ASSERT_NE(is_x, nullptr);
    EXPECT_EQ(is_x->op.type, nw::smalls::TokenType::IS);
    ASSERT_NE(is_x->expr, nullptr);
    ASSERT_NE(is_x->target_type, nullptr);

    auto* var_y = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[1]);
    ASSERT_NE(var_y, nullptr);
    auto* is_y = dynamic_cast<nw::smalls::CastExpression*>(var_y->init);
    ASSERT_NE(is_y, nullptr);
    EXPECT_EQ(is_y->op.type, nw::smalls::TokenType::IS);
}
