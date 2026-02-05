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

TEST_F(SmallsResolver, OperatorAlias_BinaryArithmetic)
{
    auto script = make_script(R"(
type Point { x, y: float; };

[[operator(plus)]]
fn add(a: Point, b: Point): Point {
    return Point { a.x + b.x, a.y + b.y };
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorAlias_UnaryNegation)
{
    auto script = make_script(R"(
type Point { x, y: float; };

[[operator(minus)]]
fn negate(p: Point): Point {
    return Point { -p.x, -p.y };
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorAlias_Commutative)
{
    auto script = make_script(R"(
type Point { x, y: float; };

[[operator(times, commutative)]]
fn scale(p: Point, s: float): Point {
    return Point { p.x * s, p.y * s };
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorAlias_StringConversion)
{
    auto script = make_script(R"#(
type Point { x, y: float; };

[[operator(str)]]
fn point_to_string(p: Point): string {
    return "Point()";
}
)#"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorAlias_Comparison)
{
    auto script = make_script(R"(
type ID { value: int; };

[[operator(eq)]]
fn id_equals(a: ID, b: ID): bool {
    return a.value == b.value;
}

[[operator(ne)]]
fn id_not_equals(a: ID, b: ID): bool {
    return a.value != b.value;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorAlias_ErrorNoParameters)
{
    auto script = make_script(R"(
[[operator(plus)]]
fn bad_operator(): int {
    return 42;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have error
}

TEST_F(SmallsResolver, OperatorAlias_ErrorTooManyParameters)
{
    auto script = make_script(R"(
[[operator(plus)]]
fn bad_operator(a: int, b: int, c: int): int {
    return a + b + c;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have error
}

TEST_F(SmallsResolver, OperatorAlias_ErrorInvalidOperator)
{
    auto script = make_script(R"(
type Point { x, y: float; };

[[operator(invalid_op)]]
fn bad_operator(a: Point, b: Point): Point {
    return a;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have error
}

TEST_F(SmallsResolver, OperatorAlias_ErrorCommutativeOnUnary)
{
    auto script = make_script(R"(
type Point { x, y: float; };

[[operator(minus, commutative)]]
fn bad_operator(p: Point): Point {
    return Point { -p.x, -p.y };
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0); // Should have error
}

TEST_F(SmallsResolver, OperatorAlias_AllArithmetic)
{
    auto script = make_script(R"(
type Vec2 { x, y: float; };

[[operator(plus)]]
fn vec_add(a: Vec2, b: Vec2): Vec2 {
    return Vec2 { a.x + b.x, a.y + b.y };
}

[[operator(minus)]]
fn vec_sub(a: Vec2, b: Vec2): Vec2 {
    return Vec2 { a.x - b.x, a.y - b.y };
}

[[operator(times)]]
fn vec_mul(a: Vec2, b: Vec2): Vec2 {
    return Vec2 { a.x * b.x, a.y * b.y };
}

[[operator(div)]]
fn vec_div(a: Vec2, b: Vec2): Vec2 {
    return Vec2 { a.x / b.x, a.y / b.y };
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorAlias_AllComparison)
{
    auto script = make_script(R"(
type Value { v: int; };

[[operator(eq)]]
fn value_eq(a: Value, b: Value): bool {
    return a.v == b.v;
}

[[operator(ne)]]
fn value_ne(a: Value, b: Value): bool {
    return a.v != b.v;
}

[[operator(lt)]]
fn value_lt(a: Value, b: Value): bool {
    return a.v < b.v;
}

[[operator(le)]]
fn value_le(a: Value, b: Value): bool {
    return a.v <= b.v;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorAlias_MapKeyRequiresHash)
{
    auto script = make_script(R"(
type ID { value: int; };

[[operator(eq)]]
fn id_equals(a: ID, b: ID): bool {
    return a.value == b.value;
}

fn main(): void {
    var lookup = map<ID, int>();
    lookup[ID{42}] = 1;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

TEST_F(SmallsResolver, OperatorAlias_MapKeyWithHash)
{
    auto script = make_script(R"(
type ID { value: int; };

[[operator(eq)]]
fn id_equals(a: ID, b: ID): bool {
    return a.value == b.value;
}

[[operator(hash)]]
fn id_hash(a: ID): int {
    return a.value;
}

fn main(): void {
    var lookup = map<ID, int>();
    lookup[ID{42}] = 1;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// ============================================================================
// Default Structural Operator Tests
// ============================================================================

// A struct with all-primitive fields gets default eq+hash, so it can be used
// as a map key without any [[operator(...)]] annotations.
TEST_F(SmallsResolver, DefaultStructOps_MapKeyNoAnnotations)
{
    auto script = make_script(R"(
type Point { x: int; y: int; };

fn main(): void {
    var lookup = map<Point, int>();
    lookup[Point{1, 2}] = 42;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// A struct whose field is another all-primitive struct also gets defaults.
TEST_F(SmallsResolver, DefaultStructOps_NestedStructMapKey)
{
    auto script = make_script(R"(
type Inner { value: int; };
type Outer { inner: Inner; tag: int; };

fn main(): void {
    var lookup = map<Outer, string>();
    lookup[Outer{Inner{1}, 2}] = "hello";
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// A struct with an array field cannot get default eq/hash, so using it as a
// map key should still produce an error.
TEST_F(SmallsResolver, DefaultStructOps_IneligibleField_NoMapKey)
{
    auto script = make_script(R"(
type TaggedList { tag: int; items: int[]; };

fn main(): void {
    var lookup = map<TaggedList, int>();
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

// An explicit [[operator(eq/hash)]] overrides the default — both annotations
// together should resolve cleanly.
TEST_F(SmallsResolver, DefaultStructOps_ExplicitOverrideAllowed)
{
    auto script = make_script(R"(
type ID { value: int; };

[[operator(eq)]]
fn id_eq(a: ID, b: ID): bool { return a.value == b.value; }

[[operator(hash)]]
fn id_hash(a: ID): int { return a.value; }

fn main(): void {
    var lookup = map<ID, int>();
    lookup[ID{1}] = 10;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// Explicit lt without explicit eq is still an error, even though the struct
// would otherwise qualify for default eq.
TEST_F(SmallsResolver, DefaultStructOps_ExplicitLtRequiresExplicitEq)
{
    auto script = make_script(R"(
type Score { value: int; };

[[operator(lt)]]
fn score_lt(a: Score, b: Score): bool { return a.value < b.value; }
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

// A unit-only sum type gets default eq/hash and can be used as a map key.
TEST_F(SmallsResolver, DefaultStructOps_SumTypeUnitVariants)
{
    auto script = make_script(R"(
type Color = Red | Green | Blue;

fn main(): void {
    var lookup = map<Color, int>();
    lookup[Color.Red] = 1;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// A sum type with all-primitive payloads gets default eq/hash.
TEST_F(SmallsResolver, DefaultStructOps_SumTypeWithPayload)
{
    auto script = make_script(R"(
type Result = Ok(int) | Err(string);

fn main(): void {
    var lookup = map<Result, bool>();
    lookup[Result.Ok(42)] = true;
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);
}

// Arrays are always rejected as map keys.
TEST_F(SmallsResolver, MapKeyPolicy_ArrayRejected)
{
    auto script = make_script(R"(
fn main(): void {
    var lookup = map<int[], string>();
    var key = [1, 2, 3];
    lookup[key] = "hello";
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

// Object/handle keys are rejected by policy.
TEST_F(SmallsResolver, MapKeyPolicy_ObjectRejected)
{
    auto script = make_script(R"(
fn main(): void {
    var lookup = map<object, int>();
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}

// Maps are always rejected as map keys.
TEST_F(SmallsResolver, MapKeyPolicy_MapRejected)
{
    auto script = make_script(R"(
fn main(): void {
    var lookup = map<map<int, int>, int>();
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);

    EXPECT_NO_THROW(script.resolve());
    EXPECT_GT(script.errors(), 0);
}
