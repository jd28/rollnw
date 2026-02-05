#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstPrinter.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Parser.hpp>
#include <nw/smalls/Smalls.hpp>

#include <string_view>

using namespace std::literals;

// Declaration tests will go here
TEST_F(SmallsParser, VarDeclWithType)
{
    auto script = make_script("var x: int;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->identifier_.loc.view(), "x");
    EXPECT_EQ(var->type->str(), "int");
    EXPECT_FALSE(var->is_const_);
    EXPECT_EQ(var->init, nullptr);
}

TEST_F(SmallsParser, VarDeclWithInit)
{
    auto script = make_script("var x = 42;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->identifier_.loc.view(), "x");
    EXPECT_FALSE(var->is_const_);
    EXPECT_NE(var->init, nullptr);
}

TEST_F(SmallsParser, VarDeclWithTypeAndInit)
{
    auto script = make_script("var x: int = 42;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->identifier_.loc.view(), "x");
    EXPECT_EQ(var->type->str(), "int");
    EXPECT_FALSE(var->is_const_);
    EXPECT_NE(var->init, nullptr);
}

TEST_F(SmallsParser, ConstDecl)
{
    auto script = make_script("const PI = 3.14;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* var = dynamic_cast<nw::smalls::VarDecl*>(decls[0]);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->identifier_.loc.view(), "PI");
    EXPECT_TRUE(var->is_const_);
    EXPECT_NE(var->init, nullptr);
}

TEST_F(SmallsParser, MultipleVarsWithType)
{
    auto script = make_script("var x, y, z: int;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* decl_list = dynamic_cast<nw::smalls::DeclList*>(decls[0]);
    ASSERT_NE(decl_list, nullptr);
    EXPECT_EQ(decl_list->type->str(), "int");
    EXPECT_FALSE(decl_list->is_const_);
    ASSERT_EQ(decl_list->decls.size(), 3);

    EXPECT_EQ(decl_list->decls[0]->identifier_.loc.view(), "x");
    EXPECT_EQ(decl_list->decls[1]->identifier_.loc.view(), "y");
    EXPECT_EQ(decl_list->decls[2]->identifier_.loc.view(), "z");
}

TEST_F(SmallsParser, MultipleVarsWithInits)
{
    auto script = make_script("var x, y = 1, 2;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* decl_list = dynamic_cast<nw::smalls::DeclList*>(decls[0]);
    ASSERT_NE(decl_list, nullptr);
    EXPECT_FALSE(decl_list->is_const_);
    ASSERT_EQ(decl_list->decls.size(), 2);

    EXPECT_EQ(decl_list->decls[0]->identifier_.loc.view(), "x");
    EXPECT_NE(decl_list->decls[0]->init, nullptr);
    EXPECT_EQ(decl_list->decls[1]->identifier_.loc.view(), "y");
    EXPECT_NE(decl_list->decls[1]->init, nullptr);
}

TEST_F(SmallsParser, MultipleVarsWithTypeAndInits)
{
    auto script = make_script("var x, y: int = 10, 20;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* decl_list = dynamic_cast<nw::smalls::DeclList*>(decls[0]);
    ASSERT_NE(decl_list, nullptr);
    EXPECT_EQ(decl_list->type->str(), "int");
    EXPECT_FALSE(decl_list->is_const_);
    ASSERT_EQ(decl_list->decls.size(), 2);

    EXPECT_EQ(decl_list->decls[0]->identifier_.loc.view(), "x");
    EXPECT_NE(decl_list->decls[0]->init, nullptr);
    EXPECT_EQ(decl_list->decls[1]->identifier_.loc.view(), "y");
    EXPECT_NE(decl_list->decls[1]->init, nullptr);
}

TEST_F(SmallsParser, VarDeclError_NoTypeOrInit)
{
    auto script = make_script("var x;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_GT(script.errors(), 0); // Should have error
}

TEST_F(SmallsParser, VarDeclError_MismatchedInits)
{
    auto script = make_script("var x, y, z = 1, 2;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_GT(script.errors(), 0); // Should have error: 3 vars, 2 inits
}

TEST_F(SmallsParser, VarDeclError_MismatchedInitsMoreVars)
{
    auto script = make_script("var a, b, c, d = 1, 2;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsParser, FunctionNoReturn)
{
    auto script = make_script(R"(
        fn foo() {
            var x = 42;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "foo");
    EXPECT_EQ(func->params.size(), 0);
    EXPECT_EQ(func->return_type, nullptr); // No return type
    EXPECT_NE(func->block, nullptr);
}

TEST_F(SmallsParser, FunctionSingleReturn)
{
    auto script = make_script(R"(fn calculate(): int {
    return 42;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "calculate");
    EXPECT_EQ(func->params.size(), 0);
    ASSERT_NE(func->return_type, nullptr);
    EXPECT_EQ(func->return_type->str(), "int");
    EXPECT_NE(func->block, nullptr);
}

TEST_F(SmallsParser, FunctionMultipleReturns)
{
    auto script = make_script(R"(
        fn swap(): (int, string) {
            return 1, "hello";
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "swap");
    EXPECT_EQ(func->params.size(), 0);
    ASSERT_NE(func->return_type, nullptr);
    // (int, string) now parses as a single tuple type
    EXPECT_EQ(func->return_type->params.size(), 2);
    EXPECT_NE(func->block, nullptr);
}

TEST_F(SmallsParser, FunctionWithParams)
{
    auto script = make_script(R"(
        fn add(x: int, y: int): int {
            return x + y;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "add");
    ASSERT_EQ(func->params.size(), 2);
    EXPECT_EQ(func->params[0]->identifier_.loc.view(), "x");
    EXPECT_EQ(func->params[0]->type->str(), "int");
    EXPECT_EQ(func->params[1]->identifier_.loc.view(), "y");
    EXPECT_EQ(func->params[1]->type->str(), "int");
    ASSERT_NE(func->return_type, nullptr);
    EXPECT_EQ(func->return_type->str(), "int");
    EXPECT_NE(func->block, nullptr);
}

TEST_F(SmallsParser, FunctionWithDefaultParams)
{
    auto script = make_script(R"(
        fn greet(name: string = "World"): string {
            return name;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "greet");
    ASSERT_EQ(func->params.size(), 1);
    EXPECT_EQ(func->params[0]->identifier_.loc.view(), "name");
    EXPECT_EQ(func->params[0]->type->str(), "string");
    EXPECT_NE(func->params[0]->init, nullptr);
}

TEST_F(SmallsParser, FunctionComplexReturnType)
{
    auto script = make_script(R"(
        fn getData(): map!(string, array!(int)) {
            return empty_map;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "getData");
    ASSERT_NE(func->return_type, nullptr);
    EXPECT_EQ(func->return_type->str(), "map!(string, array!(int))");
}

TEST_F(SmallsParser, ReturnMultipleValues)
{
    auto script = make_script(R"(fn swap(x: int, y: string): (string, int) {
    return y, x;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    // Check return type - (string, int) is now a single tuple type
    ASSERT_NE(func->return_type, nullptr);
    EXPECT_EQ(func->return_type->params.size(), 2);

    // Check the return statement in the body
    ASSERT_NE(func->block, nullptr);
    ASSERT_EQ(func->block->nodes.size(), 1);

    auto* return_stmt = dynamic_cast<nw::smalls::JumpStatement*>(func->block->nodes[0]);
    ASSERT_NE(return_stmt, nullptr);
    EXPECT_EQ(return_stmt->op.type, nw::smalls::TokenType::RETURN);

    // Check that we're returning 2 expressions
    ASSERT_EQ(return_stmt->exprs.size(), 2);
}

TEST_F(SmallsParser, FunctionDeclNoBody)
{
    auto script = make_script("fn create_effect(effect_type: int, magnitude: int): Effect;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "create_effect");
    ASSERT_EQ(func->params.size(), 2);
    EXPECT_EQ(func->params[0]->identifier_.loc.view(), "effect_type");
    EXPECT_EQ(func->params[1]->identifier_.loc.view(), "magnitude");
    ASSERT_NE(func->return_type, nullptr);
    EXPECT_EQ(func->return_type->str(), "Effect");
    EXPECT_EQ(func->block, nullptr); // No body
}

TEST_F(SmallsParser, FunctionDeclNoBodyWithAnnotation)
{
    auto script = make_script("[[native]] fn apply_effect(target: GameObject, effect: Effect): void;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "apply_effect");
    ASSERT_EQ(func->params.size(), 2);
    ASSERT_NE(func->return_type, nullptr);
    EXPECT_EQ(func->return_type->str(), "void");
    EXPECT_EQ(func->block, nullptr); // No body
    ASSERT_EQ(func->annotations_.size(), 1);
    EXPECT_EQ(func->annotations_[0].name.loc.view(), "native");
}

TEST_F(SmallsParser, FunctionDeclNoParams)
{
    auto script = make_script("fn get_timestamp(): int;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    ASSERT_EQ(decls.size(), 1);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->identifier_.loc.view(), "get_timestamp");
    EXPECT_EQ(func->params.size(), 0);
    ASSERT_NE(func->return_type, nullptr);
    EXPECT_EQ(func->return_type->str(), "int");
    EXPECT_EQ(func->block, nullptr); // No body
}

// ============================================================================
// Statement Tests
// ============================================================================
