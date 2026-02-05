#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstPrinter.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Parser.hpp>
#include <nw/smalls/Smalls.hpp>

using namespace nw::smalls;

TEST_F(SmallsParser, FixedArrayBasic)
{
    nw::String code = R"(
        var grid: int[16];
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_EQ(script.ast().decls.size(), 1u);

    auto* var_decl = dynamic_cast<VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_decl, nullptr);
    EXPECT_EQ(var_decl->identifier(), "grid");

    auto* type_expr = var_decl->type;
    ASSERT_NE(type_expr, nullptr);
    EXPECT_TRUE(type_expr->is_fixed_array);
    ASSERT_EQ(type_expr->params.size(), 1u);

    // Element type should be "int" (wrapped in a TypeExpression)
    auto* elem_type = dynamic_cast<TypeExpression*>(type_expr->name);
    ASSERT_NE(elem_type, nullptr);
    auto* name_path = dynamic_cast<PathExpression*>(elem_type->name);
    ASSERT_NE(name_path, nullptr);
    ASSERT_EQ(name_path->parts.size(), 1u);
    auto* name_expr = dynamic_cast<IdentifierExpression*>(name_path->parts[0]);
    ASSERT_NE(name_expr, nullptr);
    EXPECT_EQ(name_expr->ident.loc.view(), "int");

    // Size should be 16
    auto* size_lit = dynamic_cast<LiteralExpression*>(type_expr->params[0]);
    ASSERT_NE(size_lit, nullptr);
    EXPECT_EQ(size_lit->literal.type, TokenType::INTEGER_LITERAL);
    EXPECT_EQ(size_lit->literal.loc.view(), "16");
}

TEST_F(SmallsParser, FixedArrayStruct)
{
    nw::String code = R"(
        type Point { x, y: float; };
        var points: Point[10];
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_EQ(script.ast().decls.size(), 2u);

    auto* var_decl = dynamic_cast<VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);
    EXPECT_EQ(var_decl->identifier(), "points");

    auto* type_expr = var_decl->type;
    ASSERT_NE(type_expr, nullptr);
    EXPECT_TRUE(type_expr->is_fixed_array);
    ASSERT_EQ(type_expr->params.size(), 1u);

    // Element type should be "Point" (wrapped in a TypeExpression)
    auto* elem_type = dynamic_cast<TypeExpression*>(type_expr->name);
    ASSERT_NE(elem_type, nullptr);
    auto* name_path = dynamic_cast<PathExpression*>(elem_type->name);
    ASSERT_NE(name_path, nullptr);
    ASSERT_EQ(name_path->parts.size(), 1u);
    auto* name_expr = dynamic_cast<IdentifierExpression*>(name_path->parts[0]);
    ASSERT_NE(name_expr, nullptr);
    EXPECT_EQ(name_expr->ident.loc.view(), "Point");

    // Size should be 10
    auto* size_lit = dynamic_cast<LiteralExpression*>(type_expr->params[0]);
    ASSERT_NE(size_lit, nullptr);
    EXPECT_EQ(size_lit->literal.type, TokenType::INTEGER_LITERAL);
    EXPECT_EQ(size_lit->literal.loc.view(), "10");
}

TEST_F(SmallsParser, FixedArrayExpression)
{
    nw::String code = R"(
        const SIZE = 32;
        var buffer: int[SIZE];
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_EQ(script.ast().decls.size(), 2u);

    auto* var_decl = dynamic_cast<VarDecl*>(script.ast().decls[1]);
    ASSERT_NE(var_decl, nullptr);

    auto* type_expr = var_decl->type;
    ASSERT_NE(type_expr, nullptr);
    EXPECT_TRUE(type_expr->is_fixed_array);
    ASSERT_EQ(type_expr->params.size(), 1u);

    // Size should be a variable expression "SIZE"
    auto* size_path = dynamic_cast<PathExpression*>(type_expr->params[0]);
    ASSERT_NE(size_path, nullptr);
    ASSERT_EQ(size_path->parts.size(), 1u);
    auto* size_var = dynamic_cast<IdentifierExpression*>(size_path->parts[0]);
    ASSERT_NE(size_var, nullptr);
    EXPECT_EQ(size_var->ident.loc.view(), "SIZE");
}

TEST_F(SmallsParser, DynamicArrayStillWorks)
{
    nw::String code = R"(
        var nums: array!(int);
    )";

    auto script = make_script(code);
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_EQ(script.ast().decls.size(), 1u);

    auto* var_decl = dynamic_cast<VarDecl*>(script.ast().decls[0]);
    ASSERT_NE(var_decl, nullptr);

    auto* type_expr = var_decl->type;
    ASSERT_NE(type_expr, nullptr);
    EXPECT_FALSE(type_expr->is_fixed_array); // Should NOT be a fixed array
    ASSERT_EQ(type_expr->params.size(), 1u);

    // Type name should be "array"
    auto* name_path = dynamic_cast<PathExpression*>(type_expr->name);
    ASSERT_NE(name_path, nullptr);
    ASSERT_EQ(name_path->parts.size(), 1u);
    auto* name_expr = dynamic_cast<IdentifierExpression*>(name_path->parts[0]);
    ASSERT_NE(name_expr, nullptr);
    EXPECT_EQ(name_expr->ident.loc.view(), "array");
}
