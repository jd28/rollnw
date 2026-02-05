#include "smalls_fixtures.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/AstCompiler.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Smalls.hpp>

#include <gtest/gtest.h>

using namespace std::literals;

class SmallsCompiler : public ::testing::Test {
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

// == Basic Compilation Tests =================================================

TEST_F(SmallsCompiler, CompileEmptyFunction)
{
    auto script = make_script(R"(
        fn test() {
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    // Compile to bytecode
    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());
    EXPECT_FALSE(compiler.failed_);

    // Verify function was compiled
    ASSERT_EQ(module.functions.size(), 1);
    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "test");
    EXPECT_EQ(func->param_count, 0);

    // Should have CLOSEUPVALS + RETVOID instruction
    ASSERT_EQ(func->instructions.size(), 2);
    EXPECT_EQ(func->instructions[0].opcode(), nw::smalls::Opcode::CLOSEUPVALS);
    EXPECT_EQ(func->instructions[1].opcode(), nw::smalls::Opcode::RETVOID);
}

TEST_F(SmallsCompiler, CompileLiteralInt)
{
    auto script = make_script(R"(
        fn test(): int {
            return 42;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should have: LOADI r0, 42; CLOSEUPVALS; RET r0
    ASSERT_EQ(func->instructions.size(), 3);
    EXPECT_EQ(func->instructions[0].opcode(), nw::smalls::Opcode::LOADI);
    EXPECT_EQ(func->instructions[0].arg_sbx(), 42);
    EXPECT_EQ(func->instructions[1].opcode(), nw::smalls::Opcode::CLOSEUPVALS);
    EXPECT_EQ(func->instructions[2].opcode(), nw::smalls::Opcode::RET);
}

TEST_F(SmallsCompiler, CompileLiteralFloat)
{
    auto script = make_script(R"(
        fn test(): float {
            return 3.14;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should use LOADK for float
    ASSERT_EQ(func->instructions.size(), 3);
    EXPECT_EQ(func->instructions[0].opcode(), nw::smalls::Opcode::LOADK);
    EXPECT_EQ(func->instructions[1].opcode(), nw::smalls::Opcode::CLOSEUPVALS);
    EXPECT_EQ(func->instructions[2].opcode(), nw::smalls::Opcode::RET);

    // Verify constant pool has the float
    ASSERT_GT(module.constants.size(), 0);
    EXPECT_FLOAT_EQ(module.constants[0].data.fval, 3.14f);
}

TEST_F(SmallsCompiler, CompileLiteralBool)
{
    auto script = make_script(R"(
        fn test(): bool {
            return true;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile()) << compiler.error_message_;

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should use LOADB for bool
    ASSERT_EQ(func->instructions.size(), 3);
    EXPECT_EQ(func->instructions[0].opcode(), nw::smalls::Opcode::LOADB);
    EXPECT_EQ(func->instructions[0].arg_b(), 1); // true = 1
    EXPECT_EQ(func->instructions[1].opcode(), nw::smalls::Opcode::CLOSEUPVALS);
    EXPECT_EQ(func->instructions[2].opcode(), nw::smalls::Opcode::RET);
}

TEST_F(SmallsCompiler, CompileLiteralString)
{
    auto script = make_script(R"(
        fn test(): string {
            return "hello";
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should use LOADK for string
    ASSERT_EQ(func->instructions.size(), 3);
    EXPECT_EQ(func->instructions[0].opcode(), nw::smalls::Opcode::LOADK);
    EXPECT_EQ(func->instructions[1].opcode(), nw::smalls::Opcode::CLOSEUPVALS);
    EXPECT_EQ(func->instructions[2].opcode(), nw::smalls::Opcode::RET);

    // Verify string pool
    ASSERT_GT(module.string_pool.size(), 0);
    EXPECT_EQ(module.string_pool[0], "hello");
}

TEST_F(SmallsCompiler, CompileLargeInt)
{
    auto script = make_script(R"(
        fn test(): int {
            return 100000;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Large int should use LOADK (constant pool)
    ASSERT_EQ(func->instructions.size(), 3);
    EXPECT_EQ(func->instructions[0].opcode(), nw::smalls::Opcode::LOADK);
    EXPECT_EQ(func->instructions[1].opcode(), nw::smalls::Opcode::CLOSEUPVALS);
    EXPECT_EQ(func->instructions[2].opcode(), nw::smalls::Opcode::RET);

    // Verify constant pool
    ASSERT_GT(module.constants.size(), 0);
    EXPECT_EQ(module.constants[0].data.ival, 100000);
}

// == Binary Expression Tests =================================================

TEST_F(SmallsCompiler, CompileAddition)
{
    auto script = make_script(R"(
        fn add(x: int, y: int): int {
            return x + y;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("add");
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->param_count, 2);

    // Should have: ADD r2, r0, r1; RET r2
    ASSERT_GE(func->instructions.size(), 2);

    // Find ADD instruction
    bool found_add = false;
    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::ADD) {
            found_add = true;
            // Verify it uses valid registers
            // Note: Compiler copies parameters to temp registers, so we don't check for 0 and 1
            EXPECT_GE(instr.arg_b(), 0);
            EXPECT_GE(instr.arg_c(), 0);
        }
    }
    EXPECT_TRUE(found_add);
}

TEST_F(SmallsCompiler, CompileArithmetic)
{
    // Use parameters so the expressions are not constant-foldable,
    // confirming that MUL and ADD opcodes are emitted correctly.
    auto script = make_script(R"(
        fn test(a: int, b: int): int {
            return a + b * 30;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should have MUL and ADD opcodes (b * 30 then + a)
    bool found_mul = false;
    bool found_add = false;

    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::MUL) found_mul = true;
        if (instr.opcode() == nw::smalls::Opcode::ADD) found_add = true;
    }

    EXPECT_TRUE(found_mul);
    EXPECT_TRUE(found_add);
}

TEST_F(SmallsCompiler, CompileComparison)
{
    auto script = make_script(R"(
        fn test(x: int, y: int): bool {
            return x < y;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should have LT opcode
    bool found_lt = false;
    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::LT) {
            found_lt = true;
            // Verify it uses valid registers
            EXPECT_GE(instr.arg_b(), 0);
            EXPECT_GE(instr.arg_c(), 0);
        }
    }
    EXPECT_TRUE(found_lt);
}

TEST_F(SmallsCompiler, CompileLogicalAnd)
{
    auto script = make_script(R"(
        fn test(a: bool, b: bool): bool {
            return a && b;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should use short-circuit evaluation with JMPF
    bool found_jmpf = false;
    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::JMPF) {
            found_jmpf = true;
        }
    }
    EXPECT_TRUE(found_jmpf);
}

// == Unary Expression Tests ==================================================

TEST_F(SmallsCompiler, CompileNegation)
{
    auto script = make_script(R"(
        fn test(x: int): int {
            return -x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should have NEG opcode
    bool found_neg = false;
    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::NEG) {
            found_neg = true;
        }
    }
    EXPECT_TRUE(found_neg);
}

TEST_F(SmallsCompiler, CompileLogicalNot)
{
    auto script = make_script(R"(
        fn test(x: bool): bool {
            return !x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should have NOT opcode
    bool found_not = false;
    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::NOT) {
            found_not = true;
        }
    }
    EXPECT_TRUE(found_not);
}

// == Variable Tests ==========================================================

TEST_F(SmallsCompiler, CompileLocalVariable)
{
    auto script = make_script(R"(
        fn test(): int {
            var x = 42;
            return x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should allocate register for x, load 42, then move/return x
    EXPECT_GT(func->register_count, 0);
}

TEST_F(SmallsCompiler, CompileMultipleVariables)
{
    auto script = make_script(R"(
        fn test(): int {
            var a = 10;
            var b = 20;
            var c = 30;
            return a + b + c;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test");
    ASSERT_NE(func, nullptr);

    // Should have multiple ADD instructions
    int add_count = 0;
    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::ADD) {
            add_count++;
        }
    }
    EXPECT_GE(add_count, 2); // At least 2 additions for a+b+c
}

// == Multiple Functions ======================================================

TEST_F(SmallsCompiler, CompileMultipleFunctions)
{
    auto script = make_script(R"(
        fn add(x: int, y: int): int {
            return x + y;
        }

        fn multiply(x: int, y: int): int {
            return x * y;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    // Should have both functions
    EXPECT_EQ(module.functions.size(), 2);

    const auto* add_func = module.get_function("add");
    ASSERT_NE(add_func, nullptr);
    EXPECT_EQ(add_func->param_count, 2);

    const auto* mul_func = module.get_function("multiply");
    ASSERT_NE(mul_func, nullptr);
    EXPECT_EQ(mul_func->param_count, 2);
}

// == Tuple Return Tests ======================================================

TEST_F(SmallsCompiler, CompileTupleReturn)
{
    auto script = make_script(R"(
        fn swap(x: int, y: int): (int, int) {
            return y, x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("swap");
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->param_count, 2);

    // Should have NEWTUPLE instruction to create the tuple
    bool found_newtuple = false;
    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::NEWTUPLE) {
            found_newtuple = true;
            EXPECT_EQ(instr.arg_b(), 2); // 2 elements in tuple
        }
    }
    EXPECT_TRUE(found_newtuple);
}

TEST_F(SmallsCompiler, CompileTripleTupleReturn)
{
    auto script = make_script(R"(
        fn triple(a: int, b: int, c: int): (int, int, int) {
            return c, b, a;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("triple");
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->param_count, 3);

    // Should have NEWTUPLE with 3 elements
    bool found_newtuple = false;
    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::NEWTUPLE) {
            found_newtuple = true;
            EXPECT_EQ(instr.arg_b(), 3); // 3 elements in tuple
        }
    }
    EXPECT_TRUE(found_newtuple);
}

// == Disassembly Integration Test ============================================

TEST_F(SmallsCompiler, DisassembleWhole)
{
    auto script = make_script(R"(
        fn add(a: int, b: int): int {
            return a + b;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    ASSERT_TRUE(compiler.compile()) << compiler.error_message_;

    nw::String disasm = module.disassemble();

    const char* expected =
        R"(Module: test
Strings: 0
Constants: 0

Function: add (params: 2, regs: 5)
  0000 MOVE       r2, r0
  0001 MOVE       r3, r1
  0002 ADD        r4, r2, r3
  0003 CLOSEUPVALS
  0004 RET        r4

)";

    EXPECT_EQ(disasm, expected);
}

// == Control Flow ============================================================

TEST_F(SmallsCompiler, IfStatement)
{
    auto script = make_script(R"(
        fn test_if(x: int): int {
            if (x > 10) {
                return 1;
            }
            return 0;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test_if");
    ASSERT_NE(func, nullptr);

    // Expected instructions:
    // 0: LOADI r2, 10
    // 1: GT r3, r0, r2
    // 2: JMPF r3, 3 (jump to 6)
    // 3: LOADI r4, 1
    // 4: CLOSEUPVALS
    // 5: RET r4
    // 6: LOADI r5, 0
    // 7: CLOSEUPVALS
    // 8: RET r5
    // 9: RETVOID (implicit)

    // Note: register allocation might differ slightly depending on allocator implementation
    // But opcodes should be consistent.

    const auto& instrs = func->instructions;
    ASSERT_GE(instrs.size(), 7);

    // 0: LOADI (literal 10)
    // Check if LOADI is present near start
    bool found_gt = false;
    for (size_t i = 0; i < instrs.size(); ++i) {
        if (instrs[i].opcode() == nw::smalls::Opcode::GT) {
            found_gt = true;
            // Next should be JMPF
            ASSERT_LT(i + 1, instrs.size());
            EXPECT_EQ(instrs[i + 1].opcode(), nw::smalls::Opcode::JMPF);
            // JMPF should skip the 'then' block (LOADI 1, CLOSEUPVALS, RET) -> size 3
            EXPECT_EQ(instrs[i + 1].arg_sbx(), 3);
            break;
        }
    }
    EXPECT_TRUE(found_gt);
}

TEST_F(SmallsCompiler, IfElseStatement)
{
    auto script = make_script(R"(
        fn test_if_else(x: int): int {
            if (x > 10) {
                return 1;
            } else {
                return 2;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile());

    const auto* func = module.get_function("test_if_else");
    ASSERT_NE(func, nullptr);

    bool found_jmpf = false;
    bool found_jmp = false;

    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::JMPF) found_jmpf = true;
        if (instr.opcode() == nw::smalls::Opcode::JMP) found_jmp = true;
    }

    EXPECT_TRUE(found_jmpf);
    EXPECT_TRUE(found_jmp);
}

TEST_F(SmallsCompiler, ForStatement)
{
    auto script = make_script(R"(
        fn test_for(n: int): int {
            var sum = 0;
            for (var i = 0; i < n; i = i + 1) {
                sum = sum + i;
            }
            return sum;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile()) << compiler.error_message_;

    const auto* func = module.get_function("test_for");
    ASSERT_NE(func, nullptr);

    // Opcodes expected: JMPF (exit loop), JMP (loop back), ADD, etc.
    bool found_jmpf = false;
    bool found_jmp = false;
    int add_count = 0;

    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::JMPF) found_jmpf = true;
        if (instr.opcode() == nw::smalls::Opcode::JMP) found_jmp = true;
        if (instr.opcode() == nw::smalls::Opcode::ADD) add_count++;
    }

    EXPECT_TRUE(found_jmpf);
    EXPECT_TRUE(found_jmp);
    EXPECT_GE(add_count, 2); // i + 1 and sum + i
}

TEST_F(SmallsCompiler, BreakContinue)
{
    auto script = make_script(R"(
        fn test_loop(): int {
            var i = 0;
            var sum = 0;
            for {
                i = i + 1;
                if (i > 10) { break; }
                if (i % 2 == 0) { continue; }
                sum = sum + i;
            }
            return sum;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile()) << compiler.error_message_;

    const auto* func = module.get_function("test_loop");
    ASSERT_NE(func, nullptr);

    // Verify presence of JMP instructions for break/continue
    int jmp_count = 0;
    int jmpf_count = 0; // if checks use JMPF

    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::JMP) jmp_count++;
        if (instr.opcode() == nw::smalls::Opcode::JMPF) jmpf_count++;
    }

    // break -> JMP (to end)
    // continue -> JMP (to start)
    // loop back -> JMP (to start)
    // if (i>10) -> JMPF
    // if (i%2==0) -> JMPF
    // So we expect at least 3 JMPs and 2 JMPFs.
    EXPECT_GE(jmp_count, 3);
    EXPECT_GE(jmpf_count, 2);
}

TEST_F(SmallsCompiler, FunctionCall)
{
    auto script = make_script(R"(
        fn bar(a: int): int { return a + 1; }
        fn foo(): int {
            return bar(42);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile()) << compiler.error_message_;

    const auto* func = module.get_function("foo");
    ASSERT_NE(func, nullptr);

    // Expected:
    // LOADI rX, 42
    // MOVE rY+1, rX
    // CALL rY, bar_idx, 1
    // RET rY

    bool found_call = false;
    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::CALL) {
            found_call = true;
            EXPECT_EQ(instr.arg_c(), 1); // 1 argument
        }
    }
    EXPECT_TRUE(found_call);
}

TEST_F(SmallsCompiler, SwitchStatement)
{
    auto script = make_script(R"(
        fn test_switch(x: int): int {
            switch (x) {
                case 1: return 10;
                case 2: return 20;
                default: return 30;
            }
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile()) << compiler.error_message_;

    const auto* func = module.get_function("test_switch");
    ASSERT_NE(func, nullptr);

    // Verify ISEQ and JMP instructions
    int iseq_count = 0;
    int jmp_count = 0;

    for (const auto& instr : func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::ISEQ) iseq_count++;
        if (instr.opcode() == nw::smalls::Opcode::JMP) jmp_count++;
    }

    // Expect 2 ISEQs (for case 1, case 2)
    // Expect JMPs for dispatch and fallthrough logic
    EXPECT_EQ(iseq_count, 2);
    EXPECT_GT(jmp_count, 2);
}

// == Generic Functions =======================================================

TEST_F(SmallsCompiler, GenericFunctionSkippedDuringCompilation)
{
    auto script = make_script(R"(
        fn identity(x: $T): $T {
            return x;
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile()) << compiler.error_message_;

    // Generic functions should be skipped during normal compilation
    EXPECT_EQ(module.functions.size(), 0);
}

TEST_F(SmallsCompiler, GenericFunctionInstantiatedOnCall)
{
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

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile()) << compiler.error_message_;

    // Should have test function and instantiated identity<int>
    EXPECT_GE(module.functions.size(), 1);

    const auto* test_func = module.get_function("test");
    ASSERT_NE(test_func, nullptr);

    // Should have a CALL instruction
    bool found_call = false;
    for (const auto& instr : test_func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::CALL) {
            found_call = true;
        }
    }
    EXPECT_TRUE(found_call);
}

TEST_F(SmallsCompiler, GenericFunctionMultipleTypeParams)
{
    auto script = make_script(R"(
        fn swap(a: $A, b: $B): ($B, $A) {
            return b, a;
        }

        fn test(): (float, int) {
            return swap(1, 2.5);
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());

    nw::smalls::BytecodeModule module("test");
    nw::smalls::AstCompiler compiler(&script, &module, &nw::kernel::runtime(), nw::kernel::runtime().diagnostic_context());

    EXPECT_TRUE(compiler.compile()) << compiler.error_message_;

    const auto* test_func = module.get_function("test");
    ASSERT_NE(test_func, nullptr);

    bool found_call = false;
    for (const auto& instr : test_func->instructions) {
        if (instr.opcode() == nw::smalls::Opcode::CALL) {
            found_call = true;
        }
    }
    EXPECT_TRUE(found_call);
}
