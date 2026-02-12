#include "smalls_fixtures.hpp"

#include "nw/smalls/types.hpp"
#include <nw/config.hpp>
#include <nw/log.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <string_view>

using namespace std::literals;
namespace fs = std::filesystem;

TEST_F(SmallsResolver, SimpleFunction)
{
    auto script = make_script(R"(fn add(x: int, y: int): int {
    return x + y;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, FunctionNoReturn)
{
    auto script = make_script(R"(fn print_message() {
    var x = 42;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, MultipleReturnValues)
{
    auto script = make_script(R"(fn swap(x: int, y: int): (int, int) {
    return y, x;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, TupleDestructuring)
{
    auto script = make_script(R"(fn swap(x: int, y: int): (int, int) {
    return y, x;
}

fn test() {
    var a, b = swap(1, 2);
    return;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, TupleAsParameter)
{
    auto script = make_script(R"(fn swap(x: int, y: int): (int, int) {
    return y, x;
}

fn process_pair(pair: (int, int)) {
    return;
}

fn test() {
    var p = swap(1, 2);
    process_pair(p);
    return;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, TupleLiteralInExpression)
{
    auto script = make_script(R"(fn process_pair(pair: (int, int)) {
    return;
}

fn test() {
    var p = (1, 2);
    process_pair(p);
    return;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, TupleDestructuringAssignment)
{
    auto script = make_script(R"(fn swap(x: int, y: int): (int, int) {
    return y, x;
}

fn test() {
    var a: int;
    var b: int;
    a, b = swap(1, 2);
    return;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, TupleIndexing)
{
    auto script = make_script(R"(fn make_tuple(): (int, float, string) {
    return 42, 3.14, "hello";
}

fn test() {
    var tuple = make_tuple();
    var a = tuple[0];
    var b = tuple[1];
    var c = tuple[2];
    return;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, TupleIndexingOutOfBounds)
{
    auto script = make_script(R"(fn make_tuple(): (int, float) {
    return 42, 3.14;
}

fn test() {
    var tuple = make_tuple();
    var x = tuple[3];
    return;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, LocalVariableDeclaration)
{
    auto script = make_script(R"(fn test() {
    var x: int = 10;
    var y = 20;
    return;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, GlobalVariableDeclaration)
{
    auto script = make_script(R"(var global_x: int = 100;
const PI = 3.14;

fn use_global(): int {
    return global_x;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, FunctionCall)
{
    auto script = make_script(R"(fn helper(): int {
    return 42;
}

fn main(): int {
    return helper();
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, StructDeclaration)
{
    auto script = make_script(R"(type Point {
    x: int;
    y: int;
};

fn create_point(): Point {
    var p: Point;
    return p;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, TypeAlias)
{
    auto script = make_script(R"(type MyInt = int;

fn use_alias(x: MyInt): MyInt {
    return x;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// Error cases

TEST_F(SmallsResolver, ErrorWrongReturnType)
{
    auto script = make_script(R"(fn get_number(): int {
    return "not a number";
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have type error
}

TEST_F(SmallsResolver, ErrorWrongNumberOfReturns)
{
    auto script = make_script(R"(fn swap(x: int, y: int): (int, int) {
    return x;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have error: returning 1 value but expects 2
}

TEST_F(SmallsResolver, ReturnTupleTypeCompatiblePasses)
{
    auto script = make_script(R"(fn pair(a: int, b: int): (int, int) {
    return a, b;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, ReturnTupleTypeMismatchStillErrors)
{
    auto script = make_script(R"(fn bad_pair(a: int, b: int): int {
    return a, b;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, ErrorUndefinedVariable)
{
    auto script = make_script(R"(fn test(): int {
    return undefined_var;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have undefined variable error
}

TEST_F(SmallsResolver, ErrorUndefinedFunction)
{
    auto script = make_script(R"(fn main(): int {
    return undefined_function();
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have undefined function error
}

TEST_F(SmallsResolver, ErrorReturnOutsideFunction)
{
    auto script = make_script(R"(var x: int = 10;
return x;)"sv);

    EXPECT_NO_THROW(script.parse());
    // Parser should catch this, but if not, resolver should
    // This might be a parse error depending on implementation
}

TEST_F(SmallsResolver, ErrorMissingReturn)
{
    auto script = make_script(R"(fn get_value(): int {
    var x = 10;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have error: not all paths return
}

TEST_F(SmallsResolver, ConditionalReturn)
{
    auto script = make_script(R"(fn abs(x: int): int {
    if (x) {
        return x;
    }
    return 0;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, ConstVariable)
{
    auto script = make_script(R"(const MAX_VALUE = 100;

fn get_max(): int {
    return MAX_VALUE;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, StructLiteralPositional)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const origin = Point { 0.0, 0.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Verify the struct literal was resolved to the correct type
    auto& decls = script.ast().decls;
    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[1]);
    ASSERT_NE(var_decl, nullptr);
    auto* struct_lit = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(struct_lit, nullptr);

    // Type should be resolved to Point
    EXPECT_NE(struct_lit->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(struct_lit->type_id_)), "test.Point");
    EXPECT_TRUE(struct_lit->is_const_);
}

TEST_F(SmallsResolver, StructLiteralNamed)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const p1 = Point { x = 1.0, y = 2.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[1]);
    ASSERT_NE(var_decl, nullptr);
    auto* struct_lit = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(struct_lit, nullptr);
    EXPECT_NE(struct_lit->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(struct_lit->type_id_)), "test.Point");
    EXPECT_TRUE(struct_lit->is_const_);
}

TEST_F(SmallsResolver, StructLiteralUnknownType)
{
    auto script = make_script(R"(
        const bad = UnknownType { 1.0, 2.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should error on unknown type
}

TEST_F(SmallsResolver, StructLiteralNested)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        type Line { start, end: Point; };
        const line = Line {
            start = Point { 0.0, 0.0 },
            end = Point { 10.0, 10.0 }
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[2]);
    ASSERT_NE(var_decl, nullptr);
    auto* line_lit = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(line_lit, nullptr);
    EXPECT_NE(line_lit->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(line_lit->type_id_)), "test.Line");

    // Check nested literals are also resolved
    auto* start_lit = dynamic_cast<nw::smalls::BraceInitLiteral*>(line_lit->items[0].value);
    ASSERT_NE(start_lit, nullptr);
    EXPECT_NE(start_lit->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(start_lit->type_id_)), "test.Point");

    auto* end_lit = dynamic_cast<nw::smalls::BraceInitLiteral*>(line_lit->items[1].value);
    ASSERT_NE(end_lit, nullptr);
    EXPECT_NE(end_lit->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(end_lit->type_id_)), "test.Point");
}

TEST_F(SmallsResolver, StructLiteralConstEvaluation)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const origin = Point { 0.0, 0.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Verify the struct literal is marked as const
    auto& decls = script.ast().decls;
    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[1]);
    ASSERT_NE(var_decl, nullptr);
    auto* struct_lit = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(struct_lit, nullptr);

    EXPECT_TRUE(struct_lit->is_const_);
}

TEST_F(SmallsResolver, StructLiteralNonConstField)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        var value = 1.0;
        const p = Point { value, 2.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Verify the struct literal is NOT marked as const (due to non-const field)
    auto& decls = script.ast().decls;
    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[2]);
    ASSERT_NE(var_decl, nullptr);
    auto* struct_lit = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(struct_lit, nullptr);

    EXPECT_FALSE(struct_lit->is_const_);
}

// ============================================================================
// Brace Init Type Inference
// ============================================================================

TEST_F(SmallsResolver, BraceInitInferenceFromVariableDeclaration)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const p: Point = { 0.0, 0.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[1]);
    ASSERT_NE(var_decl, nullptr);
    auto* brace_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(brace_init, nullptr);

    EXPECT_NE(brace_init->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(brace_init->type_id_)), "test.Point");
}

TEST_F(SmallsResolver, BraceInitInferenceFromReturnStatement)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        fn origin(): Point {
            return { 0.0, 0.0 };
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func_decl = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[1]);
    ASSERT_NE(func_decl, nullptr);
    auto* return_stmt = dynamic_cast<nw::smalls::JumpStatement*>(func_decl->block->nodes[0]);
    ASSERT_NE(return_stmt, nullptr);
    auto* brace_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(return_stmt->exprs[0]);
    ASSERT_NE(brace_init, nullptr);

    EXPECT_NE(brace_init->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(brace_init->type_id_)), "test.Point");
}

TEST_F(SmallsResolver, BraceInitInferenceFromAssignment)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        fn test() {
            var p: Point;
            p = { 1.0, 2.0 };
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func_decl = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[1]);
    ASSERT_NE(func_decl, nullptr);
    auto* assign_stmt = dynamic_cast<nw::smalls::ExprStatement*>(func_decl->block->nodes[1]);
    ASSERT_NE(assign_stmt, nullptr);
    auto* assign_expr = dynamic_cast<nw::smalls::AssignExpression*>(assign_stmt->expr);
    ASSERT_NE(assign_expr, nullptr);
    auto* brace_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(assign_expr->rhs);
    ASSERT_NE(brace_init, nullptr);

    EXPECT_NE(brace_init->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(brace_init->type_id_)), "test.Point");
}

TEST_F(SmallsResolver, BraceInitInferenceFromFunctionArgument)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        fn distance(p: Point): float {
            return 0.0;
        }
        fn test(): float {
            return distance({ 3.0, 4.0 });
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func_decl = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[2]);
    ASSERT_NE(func_decl, nullptr);
    auto* return_stmt = dynamic_cast<nw::smalls::JumpStatement*>(func_decl->block->nodes[0]);
    ASSERT_NE(return_stmt, nullptr);
    auto* call_expr = dynamic_cast<nw::smalls::CallExpression*>(return_stmt->exprs[0]);
    ASSERT_NE(call_expr, nullptr);
    auto* brace_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(call_expr->args[0]);
    ASSERT_NE(brace_init, nullptr);

    EXPECT_NE(brace_init->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(brace_init->type_id_)), "test.Point");
}

TEST_F(SmallsResolver, BraceInitInferenceWithNamedFields)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const p: Point = { x = 1.0, y = 2.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[1]);
    ASSERT_NE(var_decl, nullptr);
    auto* brace_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(brace_init, nullptr);

    EXPECT_NE(brace_init->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(brace_init->type_id_)), "test.Point");
}

TEST_F(SmallsResolver, BraceInitInferenceNested)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        type Line { start, end: Point; };
        const line: Line = {
            start = { 0.0, 0.0 },
            end = { 10.0, 10.0 }
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* var_decl = dynamic_cast<nw::smalls::VarDecl*>(decls[2]);
    ASSERT_NE(var_decl, nullptr);
    auto* line_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(var_decl->init);
    ASSERT_NE(line_init, nullptr);

    EXPECT_NE(line_init->type_id_, nw::smalls::invalid_type_id);
    EXPECT_EQ(nw::String(nw::kernel::runtime().type_name(line_init->type_id_)), "test.Line");
}

// ============================================================================
// Brace Init Validation
// ============================================================================

TEST_F(SmallsResolver, BraceInitValidationInvalidFieldName)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const p: Point = { x = 1.0, z = 2.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should error: 'z' is not a field
}

TEST_F(SmallsResolver, BraceInitValidationTooManyPositionalValues)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const p: Point = { 1.0, 2.0, 3.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should error: too many initializers
}

TEST_F(SmallsResolver, BraceInitValidationKeyValueOnStruct)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const p: Point = { "x": 1.0, "y": 2.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should error: key-value syntax invalid for structs
}

TEST_F(SmallsResolver, BraceInitValidationValidFieldNames)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const p: Point = { x = 1.0, y = 2.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0); // Should succeed
}

TEST_F(SmallsResolver, BraceInitValidationFewerPositionalValues)
{
    auto script = make_script(R"(
        type Point { x, y: float; };
        const p: Point = { 1.0 };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0); // Should succeed (partial initialization)
}

// ============================================================================
// Module System - Import Resolution
// ============================================================================

TEST_F(SmallsResolver, SelectiveImportMakesSymbolsAvailable)
{
    auto& runtime = nw::kernel::runtime();

    // Create a module with some exports
    auto* base_module = runtime.load_module_from_source("test/resolver/math", R"(
type Vector {
    x: float;
    y: float;
    z: float;
};

fn length(v: Vector): float {
    return 0.0;
}
)");
    ASSERT_NE(base_module, nullptr);
    EXPECT_EQ(base_module->errors(), 0);

    // Verify base module exports are available
    EXPECT_EQ(base_module->export_count(), 2); // Vector and length

    auto script = make_script(R"(from test.resolver.math import { Vector, length };

fn test() {
    var v: Vector;
    var len = length(v);
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Verify imported symbols are in the exports
    EXPECT_GE(script.export_count(), 3); // Import makes Vector and length available, plus test function
    auto vector_export = script.exports().find("Vector");
    EXPECT_TRUE(vector_export != nullptr);
    auto length_export = script.exports().find("length");
    EXPECT_TRUE(length_export != nullptr);
}

TEST_F(SmallsResolver, AliasedImportAllowsQualifiedAccess)
{
    auto& runtime = nw::kernel::runtime();

    auto* base_module = runtime.load_module_from_source("test/resolver/types", R"(
type Transform {
    x: float;
    y: float;
};
)");
    ASSERT_NE(base_module, nullptr);
    EXPECT_EQ(base_module->errors(), 0);

    auto script = make_script(R"(import test.resolver.types as types;

fn create(): types.Transform {
    var t: types.Transform;
    return t;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SelectiveImportRedeclarationError)
{
    auto& runtime = nw::kernel::runtime();

    auto* base_module = runtime.load_module_from_source("test/resolver/collision", R"(
type Color {
    r: int;
    g: int;
    b: int;
};
)");
    ASSERT_NE(base_module, nullptr);

    auto script = make_script(R"(type Color {
    value: int;
};

from test.resolver.collision import { Color };
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    // This should produce an error because Color is already declared
    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have redeclaration error
}

TEST_F(SmallsResolver, DeclarationAfterImportError)
{
    auto& runtime = nw::kernel::runtime();

    auto* base_module = runtime.load_module_from_source("test/resolver/import_first", R"(
type Vector {
    x: float;
    y: float;
    z: float;
};
)");
    ASSERT_NE(base_module, nullptr);

    auto script = make_script(R"(from test.resolver.import_first import { Vector };

type Vector {
    a: int;
};
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    // This should produce an error because Vector was imported
    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have redeclaration error
}

TEST_F(SmallsResolver, SelectiveImportMissingSymbolError)
{
    auto& runtime = nw::kernel::runtime();

    auto* base_module = runtime.load_module_from_source("test/resolver/missing", R"(
type Point {
    x: float;
    y: float;
};
)");
    ASSERT_NE(base_module, nullptr);

    auto script = make_script(R"(from test.resolver.missing import { Point, Vector };
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    // This should produce an error because Vector is not exported from the module
    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have "not found in module" error
}

TEST_F(SmallsResolver, ArrayTypeResolution)
{
    auto script = make_script(R"(
        fn test() {
            var arr: array!(int);
            var x = arr[0];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* arr_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(arr_decl, nullptr);
    EXPECT_NE(arr_decl->type_id_, nw::smalls::invalid_type_id);

    const nw::smalls::Type* arr_type = rt.get_type(arr_decl->type_id_);
    ASSERT_NE(arr_type, nullptr);
    EXPECT_EQ(arr_type->type_kind, nw::smalls::TK_array);
    EXPECT_TRUE(arr_type->type_params[0].is<nw::smalls::TypeID>());
    EXPECT_EQ(arr_type->type_params[0].as<nw::smalls::TypeID>(), rt.int_type());

    auto* x_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[1]);
    ASSERT_NE(x_decl, nullptr);
    EXPECT_EQ(x_decl->type_id_, rt.int_type());
}

TEST_F(SmallsResolver, MapTypeResolution)
{
    auto script = make_script(R"(
        fn test() {
            var m: map!(string, int);
            var x = m["key"];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* map_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(map_decl, nullptr);
    EXPECT_NE(map_decl->type_id_, nw::smalls::invalid_type_id);

    const nw::smalls::Type* map_type = rt.get_type(map_decl->type_id_);
    ASSERT_NE(map_type, nullptr);
    EXPECT_EQ(map_type->type_kind, nw::smalls::TK_map);
    EXPECT_TRUE(map_type->type_params[0].is<nw::smalls::TypeID>());
    EXPECT_TRUE(map_type->type_params[1].is<nw::smalls::TypeID>());
    EXPECT_EQ(map_type->type_params[0].as<nw::smalls::TypeID>(), rt.string_type());
    EXPECT_EQ(map_type->type_params[1].as<nw::smalls::TypeID>(), rt.int_type());

    auto* x_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[1]);
    ASSERT_NE(x_decl, nullptr);
    EXPECT_EQ(x_decl->type_id_, rt.int_type());
}

TEST_F(SmallsResolver, SizedArrayTypeResolution)
{
    auto script = make_script(R"(
        type Player {
            inventory: int[20];
            scores: float[5];
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto& decls = script.ast().decls;
    auto* struct_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(struct_decl, nullptr);

    // Check inventory field: int[20]
    auto* inventory_decl = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[0]);
    ASSERT_NE(inventory_decl, nullptr);
    EXPECT_NE(inventory_decl->type_id_, nw::smalls::invalid_type_id);

    const nw::smalls::Type* inventory_type = rt.get_type(inventory_decl->type_id_);
    ASSERT_NE(inventory_type, nullptr);
    EXPECT_EQ(inventory_type->type_kind, nw::smalls::TK_fixed_array);
    EXPECT_EQ(inventory_type->name.view(), "int[20]");

    // Verify size and alignment
    EXPECT_EQ(inventory_type->size, sizeof(int32_t) * 20);  // 80 bytes
    EXPECT_EQ(inventory_type->alignment, alignof(int32_t)); // 4 bytes

    // Verify type params
    EXPECT_TRUE(inventory_type->type_params[0].is<nw::smalls::TypeID>());
    EXPECT_TRUE(inventory_type->type_params[1].is<int32_t>());
    EXPECT_EQ(inventory_type->type_params[0].as<nw::smalls::TypeID>(), rt.int_type());
    EXPECT_EQ(inventory_type->type_params[1].as<int32_t>(), 20);

    // Check scores field: float[5]
    auto* scores_decl = dynamic_cast<nw::smalls::VarDecl*>(struct_decl->decls[1]);
    ASSERT_NE(scores_decl, nullptr);

    const nw::smalls::Type* scores_type = rt.get_type(scores_decl->type_id_);
    ASSERT_NE(scores_type, nullptr);
    EXPECT_EQ(scores_type->type_kind, nw::smalls::TK_fixed_array);
    EXPECT_EQ(scores_type->name.view(), "float[5]");
    EXPECT_EQ(scores_type->size, sizeof(float) * 5);   // 20 bytes
    EXPECT_EQ(scores_type->alignment, alignof(float)); // 4 bytes
    EXPECT_EQ(scores_type->type_params[1].as<int32_t>(), 5);
}

TEST_F(SmallsResolver, SizedArrayVsDynamicArray)
{
    auto script = make_script(R"(
        fn test() {
            var sized: int[10];
            var dynamic: array!(int);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    // Sized array
    auto* sized_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(sized_decl, nullptr);
    const nw::smalls::Type* sized_type = rt.get_type(sized_decl->type_id_);
    ASSERT_NE(sized_type, nullptr);
    EXPECT_EQ(sized_type->name.view(), "int[10]");
    EXPECT_EQ(sized_type->type_kind, nw::smalls::TK_fixed_array);
    EXPECT_EQ(sized_type->size, sizeof(int32_t) * 10); // Has size
    EXPECT_TRUE(sized_type->type_params[1].is<int32_t>());

    // Dynamic array
    auto* dynamic_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[1]);
    ASSERT_NE(dynamic_decl, nullptr);
    const nw::smalls::Type* dynamic_type = rt.get_type(dynamic_decl->type_id_);
    ASSERT_NE(dynamic_type, nullptr);
    EXPECT_EQ(dynamic_type->name.view(), "array!(int)");
    EXPECT_EQ(dynamic_type->size, sizeof(nw::smalls::HeapPtr)); // Heap-allocated, stored as HeapPtr
    EXPECT_FALSE(dynamic_type->type_params[1].is<int32_t>());
}

TEST_F(SmallsResolver, SizedArrayNegativeSize)
{
    auto script = make_script(R"(
        fn test() {
            var arr: int[-5];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have "array size must be positive" error
}

TEST_F(SmallsResolver, SizedArrayZeroSize)
{
    auto script = make_script(R"(
        fn test() {
            var arr: int[0];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have "array size must be positive" error
}

TEST_F(SmallsResolver, ArrayIndexTypeMismatch)
{
    auto script = make_script(R"(
        fn test() {
            var arr: array!(int);
            var x = arr["not_an_int"];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have type mismatch error
}

TEST_F(SmallsResolver, ArrayOfStructsTypeResolution)
{
    auto script = make_script(R"(
        type Point {
            x: float;
            y: float;
        };

        fn test() {
            var points: array!(Point);
            var first = points[0];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto& decls = script.ast().decls;

    auto* point_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(point_decl, nullptr);
    EXPECT_EQ(point_decl->identifier(), "Point");

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[1]);
    ASSERT_NE(func, nullptr);

    auto* points_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(points_decl, nullptr);
    EXPECT_NE(points_decl->type_id_, nw::smalls::invalid_type_id);

    const nw::smalls::Type* array_type = rt.get_type(points_decl->type_id_);
    ASSERT_NE(array_type, nullptr);
    EXPECT_EQ(array_type->type_kind, nw::smalls::TK_array);
    EXPECT_TRUE(array_type->type_params[0].is<nw::smalls::TypeID>());

    auto element_type_id = array_type->type_params[0].as<nw::smalls::TypeID>();
    EXPECT_EQ(element_type_id, point_decl->type_id_);

    auto* first_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[1]);
    ASSERT_NE(first_decl, nullptr);
    EXPECT_EQ(first_decl->type_id_, point_decl->type_id_);
}

TEST_F(SmallsResolver, ArrayOfStructsWithInitialization)
{
    auto script = make_script(R"(
        type Point {
            x: float;
            y: float;
        };

        fn test() {
            var points: array!(Point) = { Point{1.0, 2.0}, Point{3.0, 4.0}, Point{5.0, 6.0} };
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto& decls = script.ast().decls;

    auto* point_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(point_decl, nullptr);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[1]);
    ASSERT_NE(func, nullptr);

    auto* points_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(points_decl, nullptr);
    EXPECT_NE(points_decl->type_id_, nw::smalls::invalid_type_id);

    const nw::smalls::Type* array_type = rt.get_type(points_decl->type_id_);
    ASSERT_NE(array_type, nullptr);
    EXPECT_EQ(array_type->type_kind, nw::smalls::TK_array);

    ASSERT_NE(points_decl->init, nullptr);
    auto* init_expr = dynamic_cast<nw::smalls::BraceInitLiteral*>(points_decl->init);
    ASSERT_NE(init_expr, nullptr);
    EXPECT_EQ(init_expr->init_type, nw::smalls::BraceInitType::list);
    EXPECT_EQ(init_expr->items.size(), 3);

    for (size_t i = 0; i < init_expr->items.size(); ++i) {
        auto* item_init = dynamic_cast<nw::smalls::BraceInitLiteral*>(init_expr->items[i].value);
        ASSERT_NE(item_init, nullptr) << "Item " << i << " should be a brace initializer";
        EXPECT_EQ(item_init->type_id_, point_decl->type_id_);
        EXPECT_EQ(item_init->items.size(), 2);
    }
}

TEST_F(SmallsResolver, NestedArrayOfStructs)
{
    auto script = make_script(R"(
        type Point {
            x, y: float;
        };

        type Line {
            start, end: Point;
        };

        fn test() {
            var lines: array!(Line);
            var first_line = lines[0];
            var start_point = first_line.start;
            var x = start_point.x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& rt = nw::kernel::runtime();
    auto& decls = script.ast().decls;

    auto* point_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(point_decl, nullptr);

    auto* line_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[1]);
    ASSERT_NE(line_decl, nullptr);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[2]);
    ASSERT_NE(func, nullptr);

    auto* lines_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[0]);
    ASSERT_NE(lines_decl, nullptr);

    const nw::smalls::Type* array_type = rt.get_type(lines_decl->type_id_);
    ASSERT_NE(array_type, nullptr);
    EXPECT_EQ(array_type->type_kind, nw::smalls::TK_array);

    auto element_type_id = array_type->type_params[0].as<nw::smalls::TypeID>();
    EXPECT_EQ(element_type_id, line_decl->type_id_);

    auto* first_line_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[1]);
    ASSERT_NE(first_line_decl, nullptr);
    EXPECT_EQ(first_line_decl->type_id_, line_decl->type_id_);

    auto* start_point_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[2]);
    ASSERT_NE(start_point_decl, nullptr);
    EXPECT_EQ(start_point_decl->type_id_, point_decl->type_id_);

    auto* x_decl = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[3]);
    ASSERT_NE(x_decl, nullptr);
    EXPECT_EQ(x_decl->type_id_, rt.float_type());
}

// ============================================================================
// Module-Scope Forward Declaration
// ============================================================================

TEST_F(SmallsResolver, FunctionCallsLaterDefinedFunction)
{
    auto script = make_script(R"(
        fn foo(): int {
            return bar();
        }

        fn bar(): int {
            return 42;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* foo = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(foo, nullptr);
    EXPECT_EQ(foo->type_id_, nw::kernel::runtime().int_type());

    auto* bar = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[1]);
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->type_id_, nw::kernel::runtime().int_type());
}

TEST_F(SmallsResolver, MutuallyRecursiveFunctions)
{
    auto script = make_script(R"(
        fn is_even(n: int): int {
            if (n) {
                return is_odd(n - 1);
            }
            return 1;
        }

        fn is_odd(n: int): int {
            if (n) {
                return is_even(n - 1);
            }
            return 0;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, TypesCanReferenceEachOther)
{
    auto script = make_script(R"(
        type Line {
            start: Point;
            end: Point;
        };

        type Point {
            x: float;
            y: float;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* line_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[0]);
    ASSERT_NE(line_decl, nullptr);
    EXPECT_NE(line_decl->type_id_, nw::smalls::invalid_type_id);

    auto* point_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[1]);
    ASSERT_NE(point_decl, nullptr);
    EXPECT_NE(point_decl->type_id_, nw::smalls::invalid_type_id);
}

TEST_F(SmallsResolver, FunctionsCanUseLaterDefinedTypes)
{
    auto script = make_script(R"(
        fn create_point(): Point {
            var p: Point;
            return p;
        }

        type Point {
            x: float;
            y: float;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);

    auto* point_decl = dynamic_cast<nw::smalls::StructDecl*>(decls[1]);
    ASSERT_NE(point_decl, nullptr);

    EXPECT_EQ(func->type_id_, point_decl->type_id_);
}

TEST_F(SmallsResolver, TypeAliasCanReferenceLaterType)
{
    auto script = make_script(R"(
        type Vec2 = Point;

        type Point {
            x: float;
            y: float;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, ComplexForwardReferences)
{
    auto script = make_script(R"(
        fn process(g: Graph): int {
            return traverse(g.root);
        }

        fn traverse(n: Node): int {
            return n.value;
        }

        type Graph {
            root: Node;
            size: int;
        };

        type Node {
            value: int;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, LocalVariablesAreLexical)
{
    auto script = make_script(R"(
        fn test(): int {
            var x = y;
            var y = 10;
            return x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Local variables are still lexical
}

TEST_F(SmallsResolver, GlobalVariablesOrderIndependent)
{
    auto script = make_script(R"(
        var x = y;
        var y = 10;
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0); // Global variables are order-independent
}

TEST_F(SmallsResolver, FunctionCanUseGlobalDeclaredAfter)
{
    auto script = make_script(R"(
        fn get_max(): int {
            return MAX_VALUE;
        }

        const MAX_VALUE = 100;
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0); // Order-independent at module level
}

TEST_F(SmallsResolver, MultipleFunctionsWithForwardReferences)
{
    auto script = make_script(R"(
        fn a(): int { return b() + c(); }
        fn b(): int { return c() + d(); }
        fn c(): int { return d(); }
        fn d(): int { return 1; }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeCanReferenceLaterType)
{
    auto script = make_script(R"(
        type MyInt(BaseInt);
        type BaseInt = int;
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0); // Order-independent
}

TEST_F(SmallsResolver, RecursiveTypeDefinitionError)
{
    auto script = make_script(R"(
        type Node {
            child: Node;
        };
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should error: recursive type definition
}

// ============================================================================
// File-Based Module Loading
// ============================================================================

TEST_F(SmallsResolver, LoadModuleFromFilesystem)
{
    auto* math_module = nw::kernel::runtime().load_module("core.math");
    ASSERT_NE(math_module, nullptr);
    EXPECT_EQ(math_module->errors(), 0);

    // Verify the module has the expected exports
    EXPECT_GE(math_module->export_count(), 4); // Vector, Point, dot, ZERO_VEC
    EXPECT_NE(math_module->exports().find("Vector"), nullptr);
    EXPECT_NE(math_module->exports().find("Point"), nullptr);
    EXPECT_NE(math_module->exports().find("dot"), nullptr);
    EXPECT_NE(math_module->exports().find("ZERO_VEC"), nullptr);
}

TEST_F(SmallsResolver, LoadNestedModuleFromFilesystem)
{

    // Load a nested module (core/types.smalls)
    auto* types_module = nw::kernel::runtime().load_module("core.types");
    ASSERT_NE(types_module, nullptr);
    EXPECT_EQ(types_module->errors(), 0);

    // Verify the module has the expected exports
    EXPECT_GE(types_module->export_count(), 4); // Color, Rect, WHITE, BLACK
    EXPECT_NE(types_module->exports().find("Color"), nullptr);
    EXPECT_NE(types_module->exports().find("Rect"), nullptr);
}

TEST_F(SmallsResolver, SelectiveImportFromFilesystemModule)
{
    auto script = make_script(R"(from core.math import { Vector, Point, dot };

fn test() {
    var v: Vector;
    var p: Point;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, AliasedImportFromFilesystemModule)
{
    auto script = make_script(R"(import core.types as types;

fn test() {
    var c: types.Color;
    var r: types.Rect;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// ============================================================================
// [[native]] Annotation Validation Tests
// ============================================================================

TEST_F(SmallsResolver, NativeOpaqueTypeValid)
{
    // Register a native module named "test" to match the script module
    struct DummyType {
        int x;
    };
    nw::kernel::runtime().module("test").add_opaque_type<DummyType>("GameObject").finalize();

    auto script = make_script("[[native]] type GameObject;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OpaqueTypeWithoutNativeError)
{
    auto script = make_script("type GameObject;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have error: missing [[native]]
}

TEST_F(SmallsResolver, NativeFunctionDeclValid)
{
    // Register a native module with matching functions
    nw::kernel::runtime().module("test").function("get_id", +[](int32_t x) -> int32_t { return x; }).function("set_value", +[](int32_t, float) -> void {}).finalize();

    auto script = make_script(R"(
[[native]] fn get_id(x: int): int;
[[native]] fn set_value(id: int, val: float): void;
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, FunctionDeclWithoutNativeError)
{
    auto script = make_script("fn create_effect(effect_type: int): int;"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have error: missing [[native]]
}

TEST_F(SmallsResolver, NativeFunctionWithBodyError)
{
    auto script = make_script(R"([[native]] fn test(): int {
    return 42;
})"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have error: [[native]] with body
}

TEST_F(SmallsResolver, NativeDeclarationsWithScriptCode)
{
    // Register a native module with matching functions
    nw::kernel::runtime().module("test").function("apply_damage", +[](int32_t, int32_t) -> void { }).finalize();

    auto script = make_script(R"(
// Native C++ binding
[[native]] fn apply_damage(target_id: int, amount: int): void;

// Script constants
const DAMAGE_MULTIPLIER = 2;

// Script wrapper function
fn damage(target_id: int, base_amount: int): void {
    var final_amount = base_amount * DAMAGE_MULTIPLIER;
    apply_damage(target_id, final_amount);
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// ============================================================================
// Default Parameters
// ============================================================================

TEST_F(SmallsResolver, FunctionWithDefaultParameter)
{
    auto script = make_script(R"(
fn greet(name: string = "World"): string {
    return name;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto& decls = script.ast().decls;
    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->params.size(), 1);
    EXPECT_NE(func->params[0]->init, nullptr);
}

TEST_F(SmallsResolver, CallFunctionWithDefaultsUsingFewerArgs)
{
    auto script = make_script(R"(
fn greet(name: string = "World"): string {
    return name;
}

fn test(): string {
    return greet();
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, CallFunctionWithDefaultsUsingAllArgs)
{
    auto script = make_script(R"(
fn greet(name: string = "World"): string {
    return name;
}

fn test(): string {
    return greet("Alice");
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, MultipleDefaultParameters)
{
    auto script = make_script(R"(
fn create_rect(x: int = 0, y: int = 0, w: int = 100, h: int = 50): int {
    return x + y + w + h;
}

fn test() {
    var a = create_rect();
    var b = create_rect(10);
    var c = create_rect(10, 20);
    var d = create_rect(10, 20, 200);
    var e = create_rect(10, 20, 200, 100);
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, MixedRequiredAndDefaultParameters)
{
    auto script = make_script(R"(
fn log_message(message: string, level: int = 0, verbose: int = 0): void {
    return;
}

fn test() {
    log_message("hello");
    log_message("warning", 1);
    log_message("error", 2, 1);
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, DefaultParameterNonConstError)
{
    auto script = make_script(R"(
var global_value = 42;

fn test(x: int = global_value): int {
    return x;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, DefaultParameterConstExpression)
{
    auto script = make_script(R"(
const DEFAULT_SIZE = 100;

fn create_buffer(size: int = DEFAULT_SIZE): int {
    return size;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, DefaultParameterArithmeticExpression)
{
    auto script = make_script(R"(
fn compute(x: int, multiplier: int = 2 * 3 + 1): int {
    return x * multiplier;
}

fn test(): int {
    return compute(5);
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, CallWithTooFewArguments)
{
    auto script = make_script(R"(
fn add(x: int, y: int = 0): int {
    return x + y;
}

fn test(): int {
    return add();
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, CallWithTooManyArguments)
{
    auto script = make_script(R"(
fn add(x: int, y: int = 0): int {
    return x + y;
}

fn test(): int {
    return add(1, 2, 3);
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, CastValidPrimitiveCasts)
{
    auto script = make_script(R"(
fn test() {
    var a = 42 as float;
    var b = 3.14 as int;
    var c = 1 as bool;
    var d = true as int;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, CastInvalidCasts)
{
    auto script = make_script(R"(
fn test() {
    var a = "hello" as int;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, IsTypeCheck)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
fn test() {
    var x = 42;
    var a = x is int;
    var b = x is float;
    var c = x is bool;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(script.ast().decls[0]);
    ASSERT_NE(func, nullptr);

    auto* var_a = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[1]);
    ASSERT_NE(var_a, nullptr);
    EXPECT_EQ(var_a->type_id_, rt.bool_type());

    auto* var_b = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[2]);
    ASSERT_NE(var_b, nullptr);
    EXPECT_EQ(var_b->type_id_, rt.bool_type());

    auto* var_c = dynamic_cast<nw::smalls::VarDecl*>(func->block->nodes[3]);
    ASSERT_NE(var_c, nullptr);
    EXPECT_EQ(var_c->type_id_, rt.bool_type());
}

TEST_F(SmallsResolver, ObjectSubtypeNarrowingInIfAnd)
{
    auto script = make_script(R"(
fn something_else(): bool {
    return true;
}

fn takes_creature(c: Creature): int {
    return 1;
}

fn test(obj: object): int {
    if (obj is Creature && something_else()) {
        return takes_creature(obj);
    }
    return 0;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, ObjectSubtypeNarrowingNotAppliedForOr)
{
    auto script = make_script(R"(
fn something_else(): bool {
    return true;
}

fn takes_creature(c: Creature): int {
    return 1;
}

fn test(obj: object): int {
    if (obj is Creature || something_else()) {
        return takes_creature(obj);
    }
    return 0;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, ObjectSubtypeSwitchPatternBinding)
{
    auto script = make_script(R"(
fn takes_creature(c: Creature): int {
    return 1;
}

fn test(obj: object): int {
    switch (obj) {
    case Creature(c):
        return takes_creature(c);
    default:
        return 0;
    }
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, ObjectSubtypeSwitchRejectsNonSubtypeCase)
{
    auto script = make_script(R"(
fn test(obj: object): int {
    switch (obj) {
    case int(x):
        return x;
    default:
        return 0;
    }
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, NativeSubtypeParameterAcceptsSubtype)
{
    auto script = make_script(R"(
from core.door import { as_door, get_hp };

fn test(obj: object): int {
    return get_hp(as_door(obj));
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, NativeSubtypeParameterRejectsObject)
{
    auto script = make_script(R"(
from core.door import { get_hp };

fn test(obj: object): int {
    return get_hp(obj);
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

// == Generic Functions =======================================================

TEST_F(SmallsResolver, GenericFunctionDetection)
{
    auto script = make_script(R"(
fn identity(x: $T): $T {
    return x;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(script.ast().decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->is_generic());
    ASSERT_EQ(func->type_params.size(), 1);
    EXPECT_EQ(func->type_params[0], "$T");
}

TEST_F(SmallsResolver, GenericFunctionMultipleTypeParams)
{
    auto script = make_script(R"(
fn pair(a: $T, b: $U): ($T, $U) {
    return a, b;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(script.ast().decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->is_generic());
    ASSERT_EQ(func->type_params.size(), 2);
    EXPECT_EQ(func->type_params[0], "$T");
    EXPECT_EQ(func->type_params[1], "$U");
}

TEST_F(SmallsResolver, NonGenericFunction)
{
    auto script = make_script(R"(
fn add(a: int, b: int): int {
    return a + b;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(script.ast().decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_FALSE(func->is_generic());
    EXPECT_TRUE(func->type_params.empty());
}

TEST_F(SmallsResolver, GenericFunctionSameTypeParamMultipleUses)
{
    auto script = make_script(R"(
fn max(a: $T, b: $T): $T {
    if (a > b) {
        return a;
    }
    return b;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* func = dynamic_cast<nw::smalls::FunctionDefinition*>(script.ast().decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->is_generic());
    ASSERT_EQ(func->type_params.size(), 1);
    EXPECT_EQ(func->type_params[0], "$T");
}

// ============================================================================
// Lvalue Mutability Tests
// ============================================================================

TEST_F(SmallsResolver, ConstArrayAssignFails)
{
    auto script = make_script(R"(
        fn test() {
            const arr = {1, 2, 3};
            arr[0] = 5;
        }
    )"sv);
    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());
    EXPECT_GE(script.errors(), 1);
}

TEST_F(SmallsResolver, ConstArrayVariableIndexAssignFails)
{
    auto script = make_script(R"(
        fn test() {
            const arr = {1, 2, 3};
            var i = 0;
            arr[i] = 5;
        }
    )"sv);
    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());
    EXPECT_GE(script.errors(), 1);
}

TEST_F(SmallsResolver, MutableArrayVariableIndexAssignOk)
{
    auto script = make_script(R"(
        fn test() {
            var arr = {1, 2, 3};
            var i = 0;
            arr[i] = 5;
        }
    )"sv);
    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, ConstStructFieldAssignFails)
{
    auto script = make_script(R"(
        type Point { x, y: int; };
        fn test() {
            const p = Point{ 1, 2 };
            p.x = 3;
        }
    )"sv);
    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());
    EXPECT_GE(script.errors(), 1);
}

TEST_F(SmallsResolver, MutableCopyOfConstStructFieldAssignOk)
{
    auto script = make_script(R"(
        type Point { x, y: int; };
        fn test() {
            const p = Point{ 1, 2 };
            var p2 = p;
            p2.x = 3;
        }
    )"sv);
    EXPECT_NO_THROW(script.parse());
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}
