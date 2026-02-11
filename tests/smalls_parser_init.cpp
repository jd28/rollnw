#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstPrinter.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Parser.hpp>
#include <nw/smalls/Smalls.hpp>

#include <string_view>

using namespace std::literals;

// Brace initialization tests (consolidates old StructLiteral and new BraceInit tests)
TEST_F(SmallsParser, LiteralInteger)
{
    auto script = make_script(R"(
        fn test() {
            var x = 42;
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

    auto* literal = dynamic_cast<nw::smalls::LiteralExpression*>(var_decl->init);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->literal.type, nw::smalls::TokenType::INTEGER_LITERAL);
}
TEST_F(SmallsParser, LiteralFloat)
{
    auto script = make_script(R"(
        fn test() {
            var x = 3.14;
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

    auto* literal = dynamic_cast<nw::smalls::LiteralExpression*>(var_decl->init);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->literal.type, nw::smalls::TokenType::FLOAT_LITERAL);
}
TEST_F(SmallsParser, LiteralString)
{
    auto script = make_script(R"(
        fn test() {
            var x = "hello";
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

    auto* literal = dynamic_cast<nw::smalls::LiteralExpression*>(var_decl->init);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->literal.type, nw::smalls::TokenType::STRING_LITERAL);
}
TEST_F(SmallsParser, BraceInitList)
{
    auto script = make_script(R"(
        fn test() {
            var a = { 1, 2, 3 };
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

    auto* brace_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(brace_init, nullptr);
    EXPECT_EQ(brace_init->init_type, nw::smalls::BraceInitType::list);
    ASSERT_EQ(brace_init->items.size(), 3);

    // All items should have nullptr keys (list items have no keys)
    for (const auto& item : brace_init->items) {
        EXPECT_EQ(item.key, nullptr);
        EXPECT_NE(item.value, nullptr);
    }
}
TEST_F(SmallsParser, BraceInitField)
{
    auto script = make_script(R"(
        fn test() {
            var p = { x = 1.0, y = 2.0 };
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

    auto* brace_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(brace_init, nullptr);
    EXPECT_EQ(brace_init->init_type, nw::smalls::BraceInitType::field);
    ASSERT_EQ(brace_init->items.size(), 2);

    // All items should have valid field names as IdentifierExpression
    auto* key0 = dynamic_cast<nw::smalls::IdentifierExpression*>(brace_init->items[0].key);
    ASSERT_NE(key0, nullptr);
    EXPECT_EQ(key0->ident.loc.view(), "x");
    EXPECT_NE(brace_init->items[0].value, nullptr);

    auto* key1 = dynamic_cast<nw::smalls::IdentifierExpression*>(brace_init->items[1].key);
    ASSERT_NE(key1, nullptr);
    EXPECT_EQ(key1->ident.loc.view(), "y");
    EXPECT_NE(brace_init->items[1].value, nullptr);
}
TEST_F(SmallsParser, BraceInitKV)
{
    auto script = make_script(R"(
        fn test() {
            var m = { "key1": 100, "key2": 200 };
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

    auto* brace_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(brace_init, nullptr);
    EXPECT_EQ(brace_init->init_type, nw::smalls::BraceInitType::kv);
    ASSERT_EQ(brace_init->items.size(), 2);

    // KV items should have expression keys (string literals in this case)
    ASSERT_NE(brace_init->items[0].key, nullptr);
    auto* key0 = dynamic_cast<nw::smalls::LiteralExpression*>(brace_init->items[0].key);
    ASSERT_NE(key0, nullptr);
    EXPECT_EQ(key0->literal.type, nw::smalls::TokenType::STRING_LITERAL);
    EXPECT_NE(brace_init->items[0].value, nullptr);

    ASSERT_NE(brace_init->items[1].key, nullptr);
    auto* key1 = dynamic_cast<nw::smalls::LiteralExpression*>(brace_init->items[1].key);
    ASSERT_NE(key1, nullptr);
    EXPECT_EQ(key1->literal.type, nw::smalls::TokenType::STRING_LITERAL);
    EXPECT_NE(brace_init->items[1].value, nullptr);
}
TEST_F(SmallsParser, BraceInitEmpty)
{
    auto script = make_script(R"(
        fn test() {
            var empty = { };
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* brace_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(brace_init, nullptr);
    EXPECT_EQ(brace_init->items.size(), 0);

    auto view = script.view_from_range(brace_init->range_);
    ASSERT_FALSE(view.empty());
    EXPECT_EQ(view.front(), '{');
    EXPECT_EQ(view.back(), '}');
}
TEST_F(SmallsParser, BraceInitNested)
{
    auto script = make_script(R"(
        fn test() {
            var line = {
                start = { x = 0.0, y = 0.0 },
                end = { x = 10.0, y = 10.0 }
            };
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* outer = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->init_type, nw::smalls::BraceInitType::field);
    ASSERT_EQ(outer->items.size(), 2);

    // Check nested brace inits
    auto* start = dynamic_cast<nw::smalls::BraceInitLiteral*>(outer->items[0].value);
    ASSERT_NE(start, nullptr);
    EXPECT_EQ(start->init_type, nw::smalls::BraceInitType::field);
    ASSERT_EQ(start->items.size(), 2);

    auto* end = dynamic_cast<nw::smalls::BraceInitLiteral*>(outer->items[1].value);
    ASSERT_NE(end, nullptr);
    EXPECT_EQ(end->init_type, nw::smalls::BraceInitType::field);
    ASSERT_EQ(end->items.size(), 2);
}

// ============================================================================
// Import Tests
// ============================================================================
TEST_F(SmallsParser, BraceInitTypedList)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const origin = Point { 0.0, 0.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 2);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[1]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->init, nullptr);

    auto* struct_lit = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(struct_lit, nullptr);
    EXPECT_EQ(struct_lit->type.loc.view(), "Point");
    ASSERT_EQ(struct_lit->items.size(), 2);

    auto name = dynamic_cast<nw::smalls::IdentifierExpression*>(struct_lit->items[0].key);
    ASSERT_EQ(name, nullptr);
    ASSERT_NE(struct_lit->items[0].value, nullptr);

    name = dynamic_cast<nw::smalls::IdentifierExpression*>(struct_lit->items[1].key);
    ASSERT_EQ(name, nullptr);
    ASSERT_NE(struct_lit->items[1].value, nullptr);
}
TEST_F(SmallsParser, BraceInitTypedField)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const p1 = Point { x = 1.0, y = 2.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 2);

    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[1]);
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->init, nullptr);

    auto* struct_lit = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(struct_lit, nullptr);
    EXPECT_EQ(struct_lit->type.loc.view(), "Point");
    ASSERT_EQ(struct_lit->items.size(), 2);

    // Check named syntax
    auto name = dynamic_cast<nw::smalls::IdentifierExpression*>(struct_lit->items[0].key);
    ASSERT_NE(name, nullptr);
    EXPECT_NE(name->ident.type, nw::smalls::TokenType::INVALID);
    EXPECT_EQ(name->ident.loc.view(), "x");

    name = dynamic_cast<nw::smalls::IdentifierExpression*>(struct_lit->items[1].key);
    ASSERT_NE(name, nullptr);
    EXPECT_NE(name->ident.type, nw::smalls::TokenType::INVALID);
    EXPECT_EQ(name->ident.loc.view(), "y");
    ASSERT_NE(struct_lit->items[0].value, nullptr);
    ASSERT_NE(struct_lit->items[1].value, nullptr);
}
TEST_F(SmallsParser, BraceInitMixedSyntaxError)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const bad = Point { x = 1.0, 2.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_GT(script.errors(), 0);
}

// ============================================================================
// Anonymous Brace Init Literal Tests
// ============================================================================
