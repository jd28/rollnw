#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/BytecodeVerifier.hpp>
#include <nw/smalls/runtime.hpp>

using namespace std::literals;

class SmallsBytecodeVerifier : public ::testing::Test {
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

TEST_F(SmallsBytecodeVerifier, AcceptsValidModule)
{
    using namespace nw::smalls;

    BytecodeModule module("m");
    auto* f = new CompiledFunction("main");
    f->param_count = 0;
    f->register_count = 1;
    f->instructions.push_back(Instruction::make_asbx(Opcode::LOADI, 0, 42));
    f->instructions.push_back(Instruction::make_abc(Opcode::RET, 0, 0, 0));
    module.add_function(f);

    nw::String err;
    EXPECT_TRUE(verify_bytecode_module(&module, &err)) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsRegisterOutOfRange)
{
    using namespace nw::smalls;

    BytecodeModule module("m");
    auto* f = new CompiledFunction("f");
    f->param_count = 0;
    f->register_count = 1; // only r0 is valid
    f->instructions.push_back(Instruction::make_abc(Opcode::ADD, 1, 0, 0));
    f->instructions.push_back(Instruction::make_abc(Opcode::RET, 0, 0, 0));
    module.add_function(f);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("register out of range"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsConstantIndexOutOfRange)
{
    using namespace nw::smalls;

    BytecodeModule module("m");
    auto* f = new CompiledFunction("f");
    f->param_count = 0;
    f->register_count = 1;
    f->instructions.push_back(Instruction::make_abx(Opcode::LOADK, 0, 1234));
    f->instructions.push_back(Instruction::make_abc(Opcode::RET, 0, 0, 0));
    module.add_function(f);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("constant index out of range"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsJumpTargetOutOfRange)
{
    using namespace nw::smalls;

    BytecodeModule module("m");
    auto* f = new CompiledFunction("f");
    f->param_count = 0;
    f->register_count = 1;
    f->instructions.push_back(Instruction::make_jump(Opcode::JMP, 1000));
    module.add_function(f);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("target out of range"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsGlobalSlotOutOfRange)
{
    using namespace nw::smalls;

    BytecodeModule module("m");
    module.global_count = 1;
    module.globals.resize(1);

    auto* f = new CompiledFunction("f");
    f->param_count = 0;
    f->register_count = 1;
    f->instructions.push_back(Instruction::make_abx(Opcode::GETGLOBAL, 0, 7));
    f->instructions.push_back(Instruction::make_abc(Opcode::RET, 0, 0, 0));
    module.add_function(f);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("global slot out of range"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsClosureDescriptorRegOutOfRange)
{
    using namespace nw::smalls;

    BytecodeModule module("m");

    auto* callee = new CompiledFunction("$lambda");
    callee->param_count = 0;
    callee->register_count = 1;
    callee->upvalue_count = 1;
    module.add_function(callee);

    auto* outer = new CompiledFunction("outer");
    outer->param_count = 0;
    outer->register_count = 1;
    outer->instructions.push_back(Instruction::make_abx(Opcode::CLOSURE, 0, 0));

    // Descriptor: local capture, reg index 3 (out of range for reg_count=1)
    const uint8_t desc = static_cast<uint8_t>((3u << 1u) | 0x01u);
    const uint32_t word = static_cast<uint32_t>(desc);
    outer->instructions.push_back(Instruction{word});
    outer->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(outer);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("closure local upvalue reg out of range"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsClosureCapturesNonLocalWithoutUpvalues)
{
    using namespace nw::smalls;

    BytecodeModule module("m");

    auto* callee = new CompiledFunction("$lambda");
    callee->param_count = 0;
    callee->register_count = 1;
    callee->upvalue_count = 1;
    module.add_function(callee);

    auto* outer = new CompiledFunction("outer");
    outer->param_count = 0;
    outer->register_count = 1;
    outer->upvalue_count = 0;
    outer->instructions.push_back(Instruction::make_abx(Opcode::CLOSURE, 0, 0));

    // Descriptor: non-local capture, upvalue index 0; outer has no upvalues.
    const uint8_t desc = static_cast<uint8_t>((0u << 1u) | 0x00u);
    outer->instructions.push_back(Instruction{static_cast<uint32_t>(desc)});
    outer->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(outer);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("captures non-local upvalue"), nw::String::npos) << err;
}
