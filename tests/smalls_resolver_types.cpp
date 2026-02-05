#include "smalls_fixtures.hpp"

#include "nw/smalls/types.hpp"
#include <nw/log.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <string_view>

using namespace std::literals;

// ============================================================================
// Sum Type Declaration Tests
// ============================================================================

TEST_F(SmallsResolver, SumTypeBasic)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(string);
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SumTypeUnitVariants)
{
    auto script = make_script(R"(
        type Color = Red | Green | Blue;
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SumTypeMixedVariants)
{
    auto script = make_script(R"(
        type Shape = Circle(float) | Rectangle(float, float) | Point;
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SumTypeInVariableDecl)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(string);

        fn test() {
            var r: Result = Result.Ok(0);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SumTypeInVariableDeclRequiresInit)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(string);

        fn test() {
            var r: Result;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 1);
}

TEST_F(SmallsResolver, SumTypeInFunctionParameter)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(string);

        fn process(r: Result) {
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SumTypeWithStructPayload)
{
    auto script = make_script(R"(
        type Point {
            x: float;
            y: float;
        };

        type Shape = Circle(Point) | Rectangle(Point, Point);
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// ============================================================================
// Sum Type Error Tests
// ============================================================================

TEST_F(SmallsResolver, SumTypeDuplicateVariantNames)
{
    auto script = make_script(R"(
        type BadSum = Ok(int) | Err(string) | Ok(float);
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should error: duplicate variant name 'Ok'
}

TEST_F(SmallsResolver, SumTypeUnknownPayloadType)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(UnknownType);
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should error: unknown type 'UnknownType'
}

TEST_F(SmallsResolver, SumTypeMultipleUnknownTypes)
{
    auto script = make_script(R"(
        type BadSum = First(UnknownA) | Second(UnknownB) | Third(int);
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should error: multiple unknown types
}

TEST_F(SmallsResolver, VariantConstructionPayloadSingle)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(string);

        fn test() {
            var r: Result = Result.Ok(42);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, VariantConstructionPayloadTuple)
{
    auto script = make_script(R"(
        type Shape = Circle(float) | Rectangle(float, float) | Point;

        fn test() {
            var s: Shape = Shape.Rectangle(10.0, 20.0);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, VariantConstructionUnit)
{
    auto script = make_script(R"(
        type Color = Red | Green | Blue;

        fn test() {
            var c = Color.Red;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, VariantConstructionMixed)
{
    auto script = make_script(R"(
        type Option = Some(int) | None;

        fn test() {
            var o1: Option = Option.Some(42);
            var o2: Option = Option.None;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, VariantConstructionWrongArgCount)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(string);

        fn test() {
            var r: Result = Result.Ok(42, 100);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, VariantConstructionWrongArgType)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(string);

        fn test() {
            var r: Result = Result.Ok("wrong");
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, VariantConstructionUnitWithArgs)
{
    auto script = make_script(R"(
        type Color = Red | Green | Blue;

        fn test() {
            var c: Color = Color.Red(42);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, VariantConstructionPayloadWithoutArgs)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(string);

        fn test() {
            var r: Result = Result.Ok;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, VariantConstructionUnknownVariant)
{
    auto script = make_script(R"(
        type Result = Ok(int) | Err(string);

        fn test() {
            var r: Result = Result.Unknown(42);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeConstruction)
{
    auto script = make_script(R"(
        type Feat(int);

        fn test() {
            var f: Feat = Feat(42);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeConstructionWrongArgCount)
{
    auto script = make_script(R"(
        type Feat(int);

        fn test() {
            var f: Feat = Feat(42, 100);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeConstructionWrongArgType)
{
    auto script = make_script(R"(
        type Feat(int);

        fn test() {
            var f: Feat = Feat("wrong");
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeImplicitConversionInAssignment)
{
    auto script = make_script(R"(
        type Feat(int);

        fn test() {
            var f: Feat = 42;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeImplicitConversionInFunctionArg)
{
    auto script = make_script(R"(
        type Feat(int);

        fn takes_feat(value: Feat) { }

        fn test() {
            takes_feat(42);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeImplicitConversionInReturn)
{
    auto script = make_script(R"(
        type Feat(int);

        fn give_feat(): Feat {
            return 42;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeTupleDestructuringMismatch)
{
    auto script = make_script(R"(
        type Feat(int);

        fn test() {
            var f: Feat = Feat(1);
            var x: int = 2;
            f, x = (1, 2);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeCastWrapRejected)
{
    auto script = make_script(R"(
        type Feat(int);

        fn test() {
            var f: Feat = (42 as Feat);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, NewtypeCastUnwrapAllowed)
{
    auto script = make_script(R"(
        type Feat(int);

        fn test() {
            var f: Feat = Feat(42);
            var x: int = (f as int);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorEqWithoutHash)
{
    auto script = make_script(R"(
        type ID { value: int; };

        [[operator(eq)]]
        fn id_eq(a: ID, b: ID): bool {
            return a.value == b.value;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    // eq without hash is allowed — hash is only needed if the type is used as a map key
    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorLtWithoutEq)
{
    auto script = make_script(R"(
        type ID { value: int; };

        [[operator(lt)]]
        fn id_lt(a: ID, b: ID): bool {
            return a.value < b.value;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorEqWrongReturnType)
{
    auto script = make_script(R"(
        type ID { value: int; };

        [[operator(eq)]]
        fn id_eq(a: ID, b: ID): int {
            return 0;
        }

        [[operator(hash)]]
        fn id_hash(a: ID): int {
            return a.value;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorHashWrongReturnType)
{
    auto script = make_script(R"(
        type ID { value: int; };

        [[operator(eq)]]
        fn id_eq(a: ID, b: ID): bool {
            return a.value == b.value;
        }

        [[operator(hash)]]
        fn id_hash(a: ID): bool {
            return true;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorStrWrongReturnType)
{
    auto script = make_script(R"(
        type ID { value: int; };

        [[operator(str)]]
        fn id_str(a: ID): int {
            return a.value;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorEqMismatchedParamTypes)
{
    auto script = make_script(R"(
        type A { value: int; };
        type B { value: int; };

        [[operator(eq)]]
        fn bad_eq(a: A, b: B): bool {
            return true;
        }

        [[operator(hash)]]
        fn a_hash(a: A): int {
            return a.value;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, GlobalVarAccessInFunction)
{
    auto script = make_script(R"(
        const ANSWER = 42;

        fn test(): int {
            return ANSWER;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SumTypeNested)
{
    auto script = make_script(R"(
        type Inner = A(int) | B;
        type Outer = Wrap(Inner) | None;

        fn test() {
            var o: Outer = Outer.None;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SumTypeGenericInstantiation)
{
    auto script = make_script(R"(
        type Option!($T) = Some($T) | None;

        fn test() {
            var o1: Option!(int) = Option!(int).None;
            var o2: Option!(Option!(int)) = Option!(Option!(int)).None;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SumTypeGenericVariantInferenceSuccess)
{
    auto script = make_script(R"(
        type Option!($T) = Some($T) | None;

        fn test() {
            var o = Option.Some(42);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, SumTypeGenericVariantInferenceFailsForUnitWithoutContext)
{
    auto script = make_script(R"(
        type Option!($T) = Some($T) | None;

        fn test() {
            var o = Option.None;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, AnyTypeImplicitConversion)
{
    auto script = make_script(R"(
fn takes_any(x: any) {}

fn test() {
    takes_any(42);
    takes_any(3.14);
    takes_any("hello");
    takes_any(true);
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, AnyTypeCasting)
{
    auto& rt = nw::kernel::runtime();
    auto script = make_script(R"(
fn get_value(): any {
    return 42;
}

fn test() {
    var x = get_value();
    var y = x as int;
    var z = x is int;
}
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    auto* test_func = dynamic_cast<nw::smalls::FunctionDefinition*>(script.ast().decls[1]);
    ASSERT_NE(test_func, nullptr);

    auto* var_x = dynamic_cast<nw::smalls::VarDecl*>(test_func->block->nodes[0]);
    ASSERT_NE(var_x, nullptr);
    EXPECT_EQ(var_x->type_id_, rt.any_type());

    auto* var_y = dynamic_cast<nw::smalls::VarDecl*>(test_func->block->nodes[1]);
    ASSERT_NE(var_y, nullptr);
    EXPECT_EQ(var_y->type_id_, rt.int_type());

    auto* var_z = dynamic_cast<nw::smalls::VarDecl*>(test_func->block->nodes[2]);
    ASSERT_NE(var_z, nullptr);
    EXPECT_EQ(var_z->type_id_, rt.bool_type());
}
