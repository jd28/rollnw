#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstCompiler.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/VirtualMachine.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/smalls/types.hpp>

#include <gtest/gtest.h>

#include <string_view>

using namespace std::literals;

class SmallsVirtualMachine : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

TEST_F(SmallsVirtualMachine, Execute)
{
    using namespace nw::smalls;

    auto& rt = nw::kernel::runtime();
    VirtualMachine vm{};

    BytecodeModule module("test_module");
    auto* func = new CompiledFunction("test_func");
    func->param_count = 0;
    func->register_count = 1;
    func->return_type = rt.type_id("int");

    // r0 = 42 (LOADI)
    func->instructions.push_back(Instruction::make_asbx(Opcode::LOADI, 0, 42));
    // return r0 (RET)
    func->instructions.push_back(Instruction::make_abc(Opcode::RET, 0, 0, 0));

    module.add_function(func);

    nw::Vector<Value> args;
    Value result = vm.execute(&module, "test_func", args);

    EXPECT_EQ(result.type_id, rt.type_id("int"));
    EXPECT_EQ(result.data.ival, 42);
}

TEST_F(SmallsVirtualMachine, ExecuteSimpleFunction)
{
    using namespace nw::smalls;

    // 1. Define Script
    auto script = make_script(R"(
        fn add(x: int, y: int): int {
            return x + y;
        }
    )"sv);

    // 2. Parse & Resolve
    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    // 3. Compile
    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    // 4. Execute
    VirtualMachine vm{};
    nw::Vector<Value> args;
    args.push_back(Value::make_int(10));
    args.push_back(Value::make_int(32));

    Value result = vm.execute(&module, "add", args);

    EXPECT_EQ(result.type_id, nw::kernel::runtime().type_id("int"));
    EXPECT_EQ(result.data.ival, 42);
}

TEST_F(SmallsVirtualMachine, SynthesizedGteLteUseLtOrEqForScriptTypes)
{
    using namespace nw::smalls;
    auto& rt = nw::kernel::runtime();

    constexpr auto source = R"(
        type Weird { v: int; };

        [[operator(eq)]]
        fn weird_eq(a: Weird, b: Weird): bool {
            return a.v == b.v;
        }

        [[operator(lt)]]
        fn weird_lt(a: Weird, b: Weird): bool {
            return (a.v + 10) < b.v;
        }

        fn ge_test(): bool {
            return Weird { v = 5 } >= Weird { v = 10 };
        }

        fn le_test(): bool {
            return Weird { v = 10 } <= Weird { v = 5 };
        }
    )";

    auto* script = rt.load_module_from_source("test.vm.synth_cmp", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<Value> args;

    auto ge = rt.execute_script(script, "ge_test", args);
    ASSERT_TRUE(ge.ok()) << ge.error_message;
    EXPECT_EQ(ge.value.type_id, rt.type_id("bool"));
    EXPECT_FALSE(ge.value.data.bval);

    auto le = rt.execute_script(script, "le_test", args);
    ASSERT_TRUE(le.ok()) << le.error_message;
    EXPECT_EQ(le.value.type_id, rt.type_id("bool"));
    EXPECT_FALSE(le.value.data.bval);
}

TEST_F(SmallsVirtualMachine, TestAndSkip)
{
    using namespace nw::smalls;

    auto& rt = nw::kernel::runtime();
    VirtualMachine vm{};

    BytecodeModule module("test_skip");
    auto* func = new CompiledFunction("test_func");
    func->param_count = 0;
    func->register_count = 2; // r0, r1
    func->return_type = rt.type_id("int");

    // Code:
    // 0: LOADI r0, 10
    // 1: LOADI r1, 20
    // 2: ISLT r0, r1   (10 < 20 is true, so skip 3)
    // 3: JMP +2        (jump to 6)
    // 4: LOADI r0, 100 (then block)
    // 5: JMP +1        (jump to 7)
    // 6: LOADI r0, 200 (else block)
    // 7: RET r0

    func->instructions.push_back(Instruction::make_asbx(Opcode::LOADI, 0, 10));
    func->instructions.push_back(Instruction::make_asbx(Opcode::LOADI, 1, 20));
    func->instructions.push_back(Instruction::make_abc(Opcode::ISLT, 0, 1, 0)); // ISLT r0, r1
    func->instructions.push_back(Instruction::make_jump(Opcode::JMP, 2));
    func->instructions.push_back(Instruction::make_asbx(Opcode::LOADI, 0, 100));
    func->instructions.push_back(Instruction::make_jump(Opcode::JMP, 1));
    func->instructions.push_back(Instruction::make_asbx(Opcode::LOADI, 0, 200));
    func->instructions.push_back(Instruction::make_abc(Opcode::RET, 0, 0, 0));

    module.add_function(func);

    nw::Vector<Value> args;
    Value result = vm.execute(&module, "test_func", args);

    EXPECT_EQ(result.type_id, rt.type_id("int"));
    EXPECT_EQ(result.data.ival, 100);
}

TEST_F(SmallsVirtualMachine, ControlFlowIf)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(x: int): int {
            if (x > 10) {
                return 1;
            } else {
                return 0;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};

    // Test True path
    nw::Vector<Value> args1;
    args1.push_back(Value::make_int(20));
    auto res1 = vm.execute(&module, "test", args1);
    EXPECT_EQ(res1.data.ival, 1);

    // Test False path
    nw::Vector<Value> args2;
    args2.push_back(Value::make_int(5));
    auto res2 = vm.execute(&module, "test", args2);
    EXPECT_EQ(res2.data.ival, 0);
}

TEST_F(SmallsVirtualMachine, ControlFlowFor)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn sum(n: int): int {
            var total = 0;
            for (var i = 1; i <= n; i = i + 1) {
                total = total + i;
            }
            return total;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    nw::Vector<Value> args;
    args.push_back(Value::make_int(5)); // Sum 1..5 = 15
    auto res = vm.execute(&module, "sum", args);
    EXPECT_EQ(res.data.ival, 15);
}

TEST_F(SmallsVirtualMachine, ControlFlowWhile)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test_while(n: int): int {
            var i = 0;
            var sum = 0;
            for (i < n) {
                sum = sum + i;
                i = i + 1;
            }
            return sum;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    nw::Vector<Value> args;
    args.push_back(Value::make_int(5)); // 0+1+2+3+4 = 10
    auto res = vm.execute(&module, "test_while", args);
    EXPECT_EQ(res.data.ival, 10);
}

TEST_F(SmallsVirtualMachine, ControlFlowBreakContinue)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): int {
            var i = 0;
            var sum = 0;
            for (i = 0; i < 10; i = i + 1) {
                if (i % 2 == 0) { continue; } // Skip evens: 0, 2, 4, 6, 8
                if (i > 7) { break; }         // Break at 9
                sum = sum + i;                // Adds: 1, 3, 5, 7
            }
            return sum;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    nw::Vector<Value> args;
    auto res = vm.execute(&module, "test", args);
    EXPECT_EQ(res.data.ival, 16);
}

TEST_F(SmallsVirtualMachine, ControlFlowTernary)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(x: bool): int {
            return x ? 10 : 20;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};

    nw::Vector<Value> args1;
    args1.push_back(Value::make_bool(true));
    auto res1 = vm.execute(&module, "test", args1);
    EXPECT_EQ(res1.data.ival, 10);

    nw::Vector<Value> args2;
    args2.push_back(Value::make_bool(false));
    auto res2 = vm.execute(&module, "test", args2);
    EXPECT_EQ(res2.data.ival, 20);
}

TEST_F(SmallsVirtualMachine, SimpleCall)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn foo(): int { return 42; }
        fn main(): int { return foo(); }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "main", {});
    EXPECT_EQ(res.data.ival, 42);
}

TEST_F(SmallsVirtualMachine, ArgsCall)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn add(a: int, b: int): int { return a + b; }
        fn main(): int { return add(10, 20); }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "main", {});
    EXPECT_EQ(res.data.ival, 30);
}

TEST_F(SmallsVirtualMachine, RecursiveFactorial)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn fact(n: int): int {
            if (n <= 1) { return 1; }
            return n * fact(n - 1);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    nw::Vector<Value> args;
    args.push_back(Value::make_int(5));
    auto res = vm.execute(&module, "fact", args);
    EXPECT_EQ(res.data.ival, 120);
}

TEST_F(SmallsVirtualMachine, NestedCalls)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn inc(x: int): int { return x + 1; }
        fn double(x: int): int { return x * 2; }
        fn main(): int { return double(inc(10)); }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "main", {});
    EXPECT_EQ(res.data.ival, 22);
}

// ============================================================================
// == Tuple Tests =============================================================
// ============================================================================

TEST_F(SmallsVirtualMachine, CreateTuple)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): (int, int) {
            return (1, 2);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    auto& rt = nw::kernel::runtime();
    EXPECT_TRUE(res.type_id != rt.int_type());
    const auto* type = rt.get_type(res.type_id);
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->type_kind, TK_tuple);
}

TEST_F(SmallsVirtualMachine, IndexTuple)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): int {
            var t = (10, 20);
            return t[1];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});
    EXPECT_EQ(res.data.ival, 20);
}

TEST_F(SmallsVirtualMachine, MultipleReturn)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn swap(x: int, y: int): (int, int) {
            return y, x;
        }
        fn main(): int {
            var t = swap(100, 200);
            return t[0];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "main", {});
    EXPECT_EQ(res.data.ival, 200);
}

// ============================================================================
// == Struct Tests ============================================================
// ============================================================================

TEST_F(SmallsVirtualMachine, CreateStruct)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        type Point { x, y: int; };
        fn test(): int {
            var p = Point { x = 10, y = 20 };
            return p.x + p.y;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});
    EXPECT_EQ(res.data.ival, 30);
}

TEST_F(SmallsVirtualMachine, SetStructField)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        type Point { x, y: int; };
        fn test(): int {
            var p = Point { x = 1, y = 2 };
            p.x = 100;
            return p.x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});
    EXPECT_EQ(res.data.ival, 100);
}

TEST_F(SmallsVirtualMachine, NestedStructs)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        type Point { x, y: int; };
        type Line { start, end: Point; };
        fn test(): int {
            var l = Line {
                start = Point { x = 10, y = 20 },
                end = Point { x = 30, y = 40 }
            };
            return l.start.x + l.end.y;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});
    EXPECT_EQ(res.data.ival, 50);
}

// ================================================================================================
// == Native Function Call Tests ==================================================================
// ================================================================================================

static int32_t s_last_val = 0;
static void set_val(int32_t v) { s_last_val = v; }
static int32_t get_val() { return s_last_val; }
static int32_t add_vals(int32_t a, int32_t b) { return a + b; }

TEST_F(SmallsVirtualMachine, NativeFunctionExecution)
{
    using namespace nw::smalls;
    auto& rt = nw::kernel::runtime();

    rt.module("test")
        .function("set_val", &set_val)
        .function("get_val", &get_val)
        .function("add_vals", &add_vals)
        .finalize();

    auto script = make_script(R"(
[[native]] fn set_val(v: int);
[[native]] fn get_val(): int;
[[native]] fn add_vals(a: int, b: int): int;

fn test_native(x: int): int {
    set_val(x);
    var y = get_val();
    return add_vals(y, 10);
}
)"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &rt, rt.diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    nw::Vector<Value> args;
    args.push_back(Value::make_int(32));

    Value result = vm.execute(&module, "test_native", args);

    EXPECT_EQ(result.data.ival, 42);
    EXPECT_EQ(s_last_val, 32);
}

TEST_F(SmallsVirtualMachine, SwitchExecution)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test_switch(x: int): int {
            switch (x) {
                case 1: return 100;
                case 2: return 200;
                default: return 300;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};

    // Case 1
    nw::Vector<Value> args1;
    args1.push_back(Value::make_int(1));
    auto res1 = vm.execute(&module, "test_switch", args1);
    EXPECT_EQ(res1.data.ival, 100);

    // Case 2
    nw::Vector<Value> args2;
    args2.push_back(Value::make_int(2));
    auto res2 = vm.execute(&module, "test_switch", args2);
    EXPECT_EQ(res2.data.ival, 200);

    // Default
    nw::Vector<Value> args3;
    args3.push_back(Value::make_int(99));
    auto res3 = vm.execute(&module, "test_switch", args3);
    EXPECT_EQ(res3.data.ival, 300);
}

TEST_F(SmallsVirtualMachine, SwitchNoFallthrough)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test_no_fallthrough(x: int): int {
            var res = 0;
            switch (x) {
                case 1:
                    res = res + 10;
                case 2:
                    res = res + 20;
                case 3:
                    res = res + 30;
            }
            return res;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};

    // Case 1: 10 only (no fallthrough)
    nw::Vector<Value> args1;
    args1.push_back(Value::make_int(1));
    auto res1 = vm.execute(&module, "test_no_fallthrough", args1);
    EXPECT_EQ(res1.data.ival, 10);

    // Case 2: 20 only
    nw::Vector<Value> args2;
    args2.push_back(Value::make_int(2));
    auto res2 = vm.execute(&module, "test_no_fallthrough", args2);
    EXPECT_EQ(res2.data.ival, 20);

    // Case 3: 30 only
    nw::Vector<Value> args3;
    args3.push_back(Value::make_int(3));
    auto res3 = vm.execute(&module, "test_no_fallthrough", args3);
    EXPECT_EQ(res3.data.ival, 30);
}

TEST_F(SmallsVirtualMachine, CastIntToFloat)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(x: int): float {
            return x as float;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    nw::Vector<Value> args;
    args.push_back(Value::make_int(10));
    auto res = vm.execute(&module, "test", args);

    EXPECT_EQ(res.type_id, nw::kernel::runtime().float_type());
    EXPECT_FLOAT_EQ(res.data.fval, 10.0f);
}

TEST_F(SmallsVirtualMachine, CastFloatToInt)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(x: float): int {
            return x as int;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    nw::Vector<Value> args;
    args.push_back(Value::make_float(10.9f));
    auto res = vm.execute(&module, "test", args);

    EXPECT_EQ(res.type_id, nw::kernel::runtime().int_type());
    EXPECT_EQ(res.data.ival, 10);
}

TEST_F(SmallsVirtualMachine, TypeCheckIs)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(x: int): bool {
            return x is int;
        }
        fn test_fail(x: int): bool {
            return x is float;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    nw::Vector<Value> args;
    args.push_back(Value::make_int(10));

    auto res1 = vm.execute(&module, "test", args);
    EXPECT_TRUE(res1.data.bval);

    auto res2 = vm.execute(&module, "test_fail", args);
    EXPECT_FALSE(res2.data.bval);
}

TEST_F(SmallsVirtualMachine, MapLiteral)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): map!(string, int) {
            return { "a": 1, "b": 2 };
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    auto& rt = nw::kernel::runtime();
    EXPECT_EQ(rt.get_type(res.type_id)->type_kind, TK_map);

    Value val;
    HeapPtr ptr = res.data.hptr;
    // MapInstance contains the data
    EXPECT_TRUE(rt.map_get(ptr, Value::make_string(rt.alloc_string("a")), val));
    EXPECT_EQ(val.data.ival, 1);
    EXPECT_TRUE(rt.map_get(ptr, Value::make_string(rt.alloc_string("b")), val));
    EXPECT_EQ(val.data.ival, 2);
}

TEST_F(SmallsVirtualMachine, MapGetSet)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): int {
            var m: map!(int, int) = { 1: 10, 2: 20 };
            m[1] = 100;
            return m[1] + m[2];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    EXPECT_EQ(res.data.ival, 120);
}

TEST_F(SmallsVirtualMachine, ArrayLiteral)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): array!(int) {
            return {10, 20, 30};
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    auto& rt = nw::kernel::runtime();
    EXPECT_EQ(rt.get_type(res.type_id)->type_kind, TK_array);
    EXPECT_EQ(rt.array_size(res.data.hptr), 3);

    Value val;
    EXPECT_TRUE(rt.array_get(res.data.hptr, 0, val));
    EXPECT_EQ(val.data.ival, 10);
    EXPECT_TRUE(rt.array_get(res.data.hptr, 2, val));
    EXPECT_EQ(val.data.ival, 30);
}

TEST_F(SmallsVirtualMachine, ArrayGetSet)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): int {
            var a: array!(int) = {10, 20, 30};
            a[1] = 100;
            return a[0] + a[1] + a[2];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile());

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    EXPECT_EQ(res.data.ival, 140); // 10 + 100 + 30
}

TEST_F(SmallsVirtualMachine, SizedArrayTest)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): int {
            var a: int[3] = {10, 20, 30};
            return a[0] + a[1] + a[2];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    EXPECT_EQ(res.data.ival, 60);
}

TEST_F(SmallsVirtualMachine, SizedArrayIndexVariable)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): int {
            var i = 1;
            var a: int[3] = {10, 20, 30};
            return a[i];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    EXPECT_EQ(res.data.ival, 20);
}

TEST_F(SmallsVirtualMachine, NestedArrayLiterals)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): int {
            var a1: array!(int) = {1, 2, 3};
            var a2: array!(int) = {4, 5, 6};
            return a1[0] * 1000 + a1[1] * 100 + a2[0] * 10 + a2[1];
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    EXPECT_EQ(res.data.ival, 1245); // 1*1000 + 2*100 + 4*10 + 5 = 1245
}

TEST_F(SmallsVirtualMachine, NestedArrayIndexAssignment)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn test(): int {
            var arr: array!(array!(int)) = {{1, 2, 3}, {4, 5, 6}};
            var v00 = arr[0][0];
            var v01 = arr[0][1];
            var v02 = arr[0][2];
            var v10 = arr[1][0];
            var v11 = arr[1][1];
            var v12 = arr[1][2];
            return v00 * 100000 + v01 * 10000 + v02 * 1000 + v10 * 100 + v11 * 10 + v12;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    EXPECT_EQ(res.data.ival, 123456); // 1,2,3,4,5,6 -> 123456

    // Now test actual nested index assignment
    auto script2 = make_script(R"(
        fn test2(): int {
            var arr: array!(array!(int)) = {{1, 2, 3}, {4, 5, 6}};
            arr[0][1] = 99;
            return arr[0][0] + arr[0][1] + arr[0][2];
        }
    )"sv);

    EXPECT_NO_THROW(script2.parse());
    ASSERT_NO_THROW(script2.resolve());

    BytecodeModule module2("test2");
    AstCompiler compiler2(&script2, &module2, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler2.compile()) << compiler2.error_message_;

    VirtualMachine vm2{};
    auto res2 = vm2.execute(&module2, "test2", {});

    EXPECT_EQ(res2.data.ival, 103); // 1 + 99 + 3
}

TEST_F(SmallsVirtualMachine, CompoundIndexAssignments)
{
    using namespace nw::smalls;

    // Test: array[index].field = value
    auto script1 = make_script(R"(
        type Point { x, y: int; };
        fn test1(): int {
            var points: array!(Point) = {Point{x=1, y=2}, Point{x=3, y=4}};
            points[0].x = 99;
            return points[0].x + points[0].y;
        }
    )"sv);

    EXPECT_NO_THROW(script1.parse());
    ASSERT_NO_THROW(script1.resolve());

    BytecodeModule module1("test1");
    AstCompiler compiler1(&script1, &module1, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler1.compile()) << compiler1.error_message_;

    VirtualMachine vm1{};
    auto res1 = vm1.execute(&module1, "test1", {});
    EXPECT_EQ(res1.data.ival, 101); // 99 + 2

    // Test: struct.field[index] = value
    auto script2 = make_script(R"(
        type Container { values: array!(int); };
        fn test2(): int {
            var c = Container { values = {1, 2, 3} };
            c.values[1] = 99;
            return c.values[0] + c.values[1] + c.values[2];
        }
    )"sv);

    EXPECT_NO_THROW(script2.parse());
    ASSERT_NO_THROW(script2.resolve());

    BytecodeModule module2("test2");
    AstCompiler compiler2(&script2, &module2, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler2.compile()) << compiler2.error_message_;

    VirtualMachine vm2{};
    auto res2 = vm2.execute(&module2, "test2", {});
    EXPECT_EQ(res2.data.ival, 103); // 1 + 99 + 3

    // Test: compound assignment operators on indexed expressions
    auto script3 = make_script(R"(
        fn test3(): int {
            var arr: array!(int) = {10, 20, 30};
            arr[1] += 5;
            arr[0] *= 2;
            return arr[0] + arr[1] + arr[2];
        }
    )"sv);

    EXPECT_NO_THROW(script3.parse());
    ASSERT_NO_THROW(script3.resolve());

    BytecodeModule module3("test3");
    AstCompiler compiler3(&script3, &module3, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler3.compile()) << compiler3.error_message_;

    VirtualMachine vm3{};
    auto res3 = vm3.execute(&module3, "test3", {});
    EXPECT_EQ(res3.data.ival, 75); // (10*2) + (20+5) + 30 = 20 + 25 + 30
}

TEST_F(SmallsVirtualMachine, StackTraceOnError)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn bar(): int {
            var arr: array!(int) = {1, 2, 3};
            return arr[100];  // Out of bounds - will fail
        }

        fn foo(): int {
            return bar();
        }

        fn main(): int {
            return foo();
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    vm.execute(&module, "main", {});

    // Should fail with array bounds error
    EXPECT_TRUE(vm.failed());
    EXPECT_FALSE(vm.error_message().empty());

    // Stack trace should show: bar -> foo -> main
    nw::String stack_trace = vm.get_stack_trace();
    EXPECT_TRUE(stack_trace.find("bar") != nw::String::npos);
    EXPECT_TRUE(stack_trace.find("foo") != nw::String::npos);
    EXPECT_TRUE(stack_trace.find("main") != nw::String::npos);

    std::cout << "\n=== Stack Trace ===\n"
              << stack_trace << "==================\n";
}

// ============================================================================
// == Stack Value Type Tests ==================================================
// ============================================================================

TEST_F(SmallsVirtualMachine, ValueTypeCopy)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        [[value_type]]
        type Vec2 { x, y: float; };

        fn test(): float {
            var a: Vec2 = { 1.0, 2.0 };
            var b = a;
            b.x = 9.0;
            return a.x + b.x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    // a.x should still be 1.0, b.x should be 9.0
    EXPECT_FLOAT_EQ(res.data.fval, 10.0f);
}

TEST_F(SmallsVirtualMachine, ValueTypeReturnAcrossCall)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        [[value_type]]
        type Vec2 { x, y: float; };

        fn make(): Vec2 {
            var v: Vec2 = { 1.0, 2.0 };
            return v;
        }

        fn test(): float {
            var a = make();
            return a.x + a.y;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    EXPECT_FLOAT_EQ(res.data.fval, 3.0f);
}

TEST_F(SmallsVirtualMachine, ValueTypePassAsParam)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        [[value_type]]
        type Vec2 { x, y: float; };

        fn add(v: Vec2): float {
            return v.x + v.y;
        }

        fn test(): float {
            var a: Vec2 = { 1.0, 2.0 };
            return add(a);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    EXPECT_FLOAT_EQ(res.data.fval, 3.0f);
}

TEST_F(SmallsVirtualMachine, ValueTypeWithHeapRef)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        [[value_type]]
        type Named { id: int; name: string; };

        fn test(): string {
            var n: Named = { 42, "hello" };
            return n.name;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    auto res = vm.execute(&module, "test", {});

    auto& rt = nw::kernel::runtime();
    EXPECT_EQ(res.type_id, rt.string_type());
    auto str = rt.get_string_view(res.data.hptr);
    EXPECT_EQ(str, "hello"sv);
}

TEST_F(SmallsVirtualMachine, StackRootEnumeration)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        [[value_type]]
        type Named { id: int; name: string; };

        fn test(): int {
            var n: Named = { 42, "hello" };
            return n.id;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());
    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    // Find the Named type through the module's type_refs (used by STACK_ALLOC)
    auto& rt = nw::kernel::runtime();
    ASSERT_FALSE(module.type_refs.empty());

    // The first type_ref should be the Named type (from STACK_ALLOC in test())
    TypeID named_tid = module.type_refs[0];
    ASSERT_NE(named_tid, invalid_type_id);

    const Type* named_type = rt.get_type(named_tid);
    ASSERT_NE(named_type, nullptr);
    EXPECT_TRUE(named_type->contains_heap_refs);
    EXPECT_EQ(named_type->type_kind, TK_struct);

    StructID sid = named_type->type_params[0].as<StructID>();
    const StructDef* def = rt.type_table_.get(sid);
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->heap_ref_count, 1);
    EXPECT_NE(def->heap_ref_offsets, nullptr);

    // Verify the offset points to the string field (after the int)
    // int is 4 bytes, so string should be at offset 4 (or 8 with alignment)
    EXPECT_GE(def->heap_ref_offsets[0], 4u);
}

// == Generic Functions =======================================================

TEST_F(SmallsVirtualMachine, GenericIdentityInt)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn identity(x: $T): $T {
            return x;
        }

        fn test(): int {
            return identity(42);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    nw::Vector<Value> args;
    Value result = vm.execute(&module, "test", args);

    EXPECT_EQ(result.type_id, nw::kernel::runtime().type_id("int"));
    EXPECT_EQ(result.data.ival, 42);
}

TEST_F(SmallsVirtualMachine, GenericIdentityFloat)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn identity(x: $T): $T {
            return x;
        }

        fn test(): float {
            return identity(3.14);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    nw::Vector<Value> args;
    Value result = vm.execute(&module, "test", args);

    EXPECT_EQ(result.type_id, nw::kernel::runtime().type_id("float"));
    EXPECT_FLOAT_EQ(result.data.fval, 3.14f);
}

TEST_F(SmallsVirtualMachine, GenericMax)
{
    using namespace nw::smalls;

    auto script = make_script(R"(
        fn max(a: $T, b: $T): $T {
            if (a > b) {
                return a;
            }
            return b;
        }

        fn test(): int {
            return max(10, 25);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    VirtualMachine vm{};
    nw::Vector<Value> args;
    Value result = vm.execute(&module, "test", args);

    EXPECT_EQ(result.type_id, nw::kernel::runtime().type_id("int"));
    EXPECT_EQ(result.data.ival, 25);
}
