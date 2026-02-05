#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/Bytecode.hpp>

class SmallsBytecode : public ::testing::Test {
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

// == Instruction Encoding/Decoding Tests =====================================

TEST_F(SmallsBytecode, InstructionABC)
{
    using namespace nw::smalls;

    auto instr = Instruction::make_abc(Opcode::ADD, 5, 10, 15);

    EXPECT_EQ(instr.opcode(), Opcode::ADD);
    EXPECT_EQ(instr.arg_a(), 5);
    EXPECT_EQ(instr.arg_b(), 10);
    EXPECT_EQ(instr.arg_c(), 15);
}

TEST_F(SmallsBytecode, InstructionABx)
{
    using namespace nw::smalls;

    auto instr = Instruction::make_abx(Opcode::LOADK, 3, 1000);

    EXPECT_EQ(instr.opcode(), Opcode::LOADK);
    EXPECT_EQ(instr.arg_a(), 3);
    EXPECT_EQ(instr.arg_bx(), 1000);
}

TEST_F(SmallsBytecode, InstructionAsBx)
{
    using namespace nw::smalls;

    // Positive immediate
    auto instr1 = Instruction::make_asbx(Opcode::LOADI, 2, 500);
    EXPECT_EQ(instr1.opcode(), Opcode::LOADI);
    EXPECT_EQ(instr1.arg_a(), 2);
    EXPECT_EQ(instr1.arg_sbx(), 500);

    // Negative immediate
    auto instr2 = Instruction::make_asbx(Opcode::LOADI, 4, -250);
    EXPECT_EQ(instr2.opcode(), Opcode::LOADI);
    EXPECT_EQ(instr2.arg_a(), 4);
    EXPECT_EQ(instr2.arg_sbx(), -250);
}

TEST_F(SmallsBytecode, InstructionJump)
{
    using namespace nw::smalls;

    // Positive offset
    auto instr1 = Instruction::make_jump(Opcode::JMP, 100);
    EXPECT_EQ(instr1.opcode(), Opcode::JMP);
    EXPECT_EQ(instr1.arg_jump(), 100);

    // Negative offset
    auto instr2 = Instruction::make_jump(Opcode::JMP, -50);
    EXPECT_EQ(instr2.opcode(), Opcode::JMP);
    EXPECT_EQ(instr2.arg_jump(), -50);

    // Large positive offset
    auto instr3 = Instruction::make_jump(Opcode::JMP, 8388607); // Max 24-bit positive
    EXPECT_EQ(instr3.opcode(), Opcode::JMP);
    EXPECT_EQ(instr3.arg_jump(), 8388607);

    // Large negative offset
    auto instr4 = Instruction::make_jump(Opcode::JMP, -8388608); // Max 24-bit negative
    EXPECT_EQ(instr4.opcode(), Opcode::JMP);
    EXPECT_EQ(instr4.arg_jump(), -8388608);
}

TEST_F(SmallsBytecode, InstructionAllOpcodes)
{
    using namespace nw::smalls;

    // Verify we can encode/decode all opcodes
    auto instr = Instruction::make_abc(Opcode::NEG, 1, 2, 3);
    EXPECT_EQ(instr.opcode(), Opcode::NEG);

    instr = Instruction::make_abc(Opcode::GETFIELD, 10, 20, 30);
    EXPECT_EQ(instr.opcode(), Opcode::GETFIELD);

    instr = Instruction::make_abc(Opcode::CALL, 0, 5, 3);
    EXPECT_EQ(instr.opcode(), Opcode::CALL);
    EXPECT_EQ(instr.arg_a(), 0);
    EXPECT_EQ(instr.arg_b(), 5);
    EXPECT_EQ(instr.arg_c(), 3);
}

// == Constant Pool Tests =====================================================

TEST_F(SmallsBytecode, ConstantInt)
{
    using namespace nw::smalls;

    auto c = Constant::make_int(42);
    EXPECT_EQ(c.data.ival, 42);

    c = Constant::make_int(-100);
    EXPECT_EQ(c.data.ival, -100);
}

TEST_F(SmallsBytecode, ConstantFloat)
{
    using namespace nw::smalls;

    auto c = Constant::make_float(3.14f);
    EXPECT_FLOAT_EQ(c.data.fval, 3.14f);

    c = Constant::make_float(-2.5f);
    EXPECT_FLOAT_EQ(c.data.fval, -2.5f);
}

TEST_F(SmallsBytecode, ConstantString)
{
    using namespace nw::smalls;

    auto c = Constant::make_string(10);
    EXPECT_EQ(c.data.string_idx, 10);
}

// == BytecodeModule Tests ====================================================

TEST_F(SmallsBytecode, ModuleCreation)
{
    using namespace nw::smalls;

    BytecodeModule module("test_module");
    EXPECT_EQ(module.module_name, "test_module");
    EXPECT_EQ(module.functions.size(), 0);
    EXPECT_EQ(module.constants.size(), 0);
    EXPECT_EQ(module.string_pool.size(), 0);
}

TEST_F(SmallsBytecode, StringPoolDeduplication)
{
    using namespace nw::smalls;

    BytecodeModule module("test");

    // Add string first time
    uint32_t idx1 = module.add_string("hello");
    EXPECT_EQ(idx1, 0);
    EXPECT_EQ(module.string_pool.size(), 1);
    EXPECT_EQ(module.get_string(idx1), "hello");

    // Add same string again (should return same index)
    uint32_t idx2 = module.add_string("hello");
    EXPECT_EQ(idx2, idx1);
    EXPECT_EQ(module.string_pool.size(), 1);

    // Add different string
    uint32_t idx3 = module.add_string("world");
    EXPECT_EQ(idx3, 1);
    EXPECT_EQ(module.string_pool.size(), 2);
    EXPECT_EQ(module.get_string(idx3), "world");
}

TEST_F(SmallsBytecode, ConstantPoolManagement)
{
    using namespace nw::smalls;

    BytecodeModule module("test");

    // Add int constant
    uint32_t idx1 = module.add_constant(Constant::make_int(42));
    EXPECT_EQ(idx1, 0);
    EXPECT_EQ(module.constants.size(), 1);
    EXPECT_EQ(module.get_constant(idx1).data.ival, 42);

    // Add float constant
    uint32_t idx2 = module.add_constant(Constant::make_float(3.14f));
    EXPECT_EQ(idx2, 1);
    EXPECT_EQ(module.constants.size(), 2);
    EXPECT_FLOAT_EQ(module.get_constant(idx2).data.fval, 3.14f);
}

TEST_F(SmallsBytecode, FunctionManagement)
{
    using namespace nw::smalls;

    BytecodeModule module("test");

    // Create and add function
    auto* func1 = new CompiledFunction("add");
    func1->param_count = 2;
    func1->register_count = 3;

    module.add_function(func1);
    EXPECT_EQ(module.functions.size(), 1);

    // Lookup by name
    const CompiledFunction* found = module.get_function("add");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "add");
    EXPECT_EQ(found->param_count, 2);

    // Get index
    uint32_t idx = module.get_function_index("add");
    EXPECT_EQ(idx, 0);

    // Lookup non-existent function
    const CompiledFunction* not_found = module.get_function("nonexistent");
    EXPECT_EQ(not_found, nullptr);

    uint32_t bad_idx = module.get_function_index("nonexistent");
    EXPECT_EQ(bad_idx, UINT32_MAX);

    // Add another function
    auto* func2 = new CompiledFunction("multiply");
    module.add_function(func2);
    EXPECT_EQ(module.functions.size(), 2);

    uint32_t idx2 = module.get_function_index("multiply");
    EXPECT_EQ(idx2, 1);
}

TEST_F(SmallsBytecode, CompiledFunctionBasic)
{
    using namespace nw::smalls;

    CompiledFunction func("test_func");
    func.param_count = 2;
    func.register_count = 5;
    func.return_type = TypeID{1}; // Assuming TypeID 1 is int

    // Add some instructions
    func.instructions.push_back(Instruction::make_abc(Opcode::ADD, 2, 0, 1));
    func.instructions.push_back(Instruction::make_abc(Opcode::RET, 2, 0, 0));

    EXPECT_EQ(func.name, "test_func");
    EXPECT_EQ(func.param_count, 2);
    EXPECT_EQ(func.register_count, 5);
    EXPECT_EQ(func.instructions.size(), 2);

    // Verify instructions
    EXPECT_EQ(func.instructions[0].opcode(), Opcode::ADD);
    EXPECT_EQ(func.instructions[1].opcode(), Opcode::RET);
}

TEST_F(SmallsBytecode, InstructionSequence)
{
    using namespace nw::smalls;

    CompiledFunction func("example");

    // Build a simple instruction sequence: r2 = r0 + r1; return r2;
    func.instructions.push_back(Instruction::make_abc(Opcode::ADD, 2, 0, 1));
    func.instructions.push_back(Instruction::make_abc(Opcode::RET, 2, 0, 0));

    ASSERT_EQ(func.instructions.size(), 2);

    // Verify first instruction
    auto instr1 = func.instructions[0];
    EXPECT_EQ(instr1.opcode(), Opcode::ADD);
    EXPECT_EQ(instr1.arg_a(), 2); // dest
    EXPECT_EQ(instr1.arg_b(), 0); // lhs
    EXPECT_EQ(instr1.arg_c(), 1); // rhs

    // Verify second instruction
    auto instr2 = func.instructions[1];
    EXPECT_EQ(instr2.opcode(), Opcode::RET);
    EXPECT_EQ(instr2.arg_a(), 2); // return value in r2
}

TEST_F(SmallsBytecode, Disassemble)
{
    using namespace nw::smalls;

    BytecodeModule module("test_mod");
    module.add_string("hello");

    auto* func = new CompiledFunction("test_func");
    func->param_count = 1;
    func->register_count = 3;

    // r1 = 10 (LOADI)
    func->instructions.push_back(Instruction::make_asbx(Opcode::LOADI, 1, 10));
    // r2 = r0 + r1 (ADD)
    func->instructions.push_back(Instruction::make_abc(Opcode::ADD, 2, 0, 1));
    // return r2 (RET)
    func->instructions.push_back(Instruction::make_abc(Opcode::RET, 2, 0, 0));

    module.add_function(func);

    nw::String disasm = module.disassemble();
    EXPECT_FALSE(disasm.empty());

    // Check for expected disassembly output
    EXPECT_TRUE(disasm.find("Module: test_mod") != nw::String::npos);
    EXPECT_TRUE(disasm.find("Function: test_func") != nw::String::npos);
    EXPECT_TRUE(disasm.find("LOADI") != nw::String::npos);
    EXPECT_TRUE(disasm.find("ADD") != nw::String::npos);
    EXPECT_TRUE(disasm.find("RET") != nw::String::npos);
    EXPECT_TRUE(disasm.find("r1, 10") != nw::String::npos);
    EXPECT_TRUE(disasm.find("r2, r0, r1") != nw::String::npos);
}
