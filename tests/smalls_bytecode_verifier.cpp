#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/BytecodeVerifier.hpp>
#include <nw/smalls/VirtualMachineIntrinsics.hpp>
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
    f->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(f);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("target out of range"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsJumpTargetOnePastEnd)
{
    using namespace nw::smalls;

    BytecodeModule module("m");
    auto* f = new CompiledFunction("f");
    f->param_count = 0;
    f->register_count = 1;
    f->instructions.push_back(Instruction::make_jump(Opcode::JMP, 1));
    f->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(f);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("target out of range"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsJumpIntoClosureDescriptor)
{
    using namespace nw::smalls;

    BytecodeModule module("m");

    auto* callee = new CompiledFunction("$lambda");
    callee->param_count = 0;
    callee->register_count = 1;
    callee->upvalue_count = 1;
    callee->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(callee);

    auto* outer = new CompiledFunction("outer");
    outer->param_count = 0;
    outer->register_count = 1;
    outer->instructions.push_back(Instruction::make_abx(Opcode::CLOSURE, 0, 0));
    outer->instructions.push_back(Instruction{0x01u}); // local upvalue descriptor
    outer->instructions.push_back(Instruction::make_jump(Opcode::JMP, -2));
    outer->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(outer);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("not an instruction start"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsMissingTerminalReturn)
{
    using namespace nw::smalls;

    BytecodeModule module("m");
    auto* f = new CompiledFunction("f");
    f->param_count = 0;
    f->register_count = 1;
    f->instructions.push_back(Instruction::make_asbx(Opcode::LOADI, 0, 42));
    module.add_function(f);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("missing terminal return"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsCallArityMismatch)
{
    using namespace nw::smalls;

    BytecodeModule module("m");

    auto* callee = new CompiledFunction("callee");
    callee->param_count = 2;
    callee->register_count = 2;
    callee->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(callee);

    auto* caller = new CompiledFunction("caller");
    caller->param_count = 0;
    caller->register_count = 2;
    caller->instructions.push_back(Instruction::make_abc(Opcode::CALL, 0, 0, 1));
    caller->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(caller);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("argument count mismatch"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsCallToDeclarationWithoutBytecode)
{
    using namespace nw::smalls;

    BytecodeModule module("m");

    auto* declaration = new CompiledFunction("native_decl");
    declaration->param_count = 1;
    declaration->register_count = 1;
    module.add_function(declaration);

    auto* caller = new CompiledFunction("caller");
    caller->param_count = 0;
    caller->register_count = 2;
    caller->instructions.push_back(Instruction::make_abc(Opcode::CALL, 0, 0, 1));
    caller->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(caller);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("callee has no bytecode body"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsUnresolvedExternalCallWithRuntimeContext)
{
    using namespace nw::smalls;

    Runtime& rt = nw::kernel::runtime();

    BytecodeModule module("m");
    const uint32_t ref_idx = module.add_external_ref(nw::kernel::strings().intern("test.missing_ext"));

    auto* caller = new CompiledFunction("caller");
    caller->param_count = 0;
    caller->register_count = 1;
    caller->instructions.push_back(Instruction::make_abc(Opcode::CALLEXT, 0, static_cast<uint8_t>(ref_idx), 0));
    caller->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(caller);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &rt, &err));
    EXPECT_NE(err.find("unresolved external function"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsNativeExternalCallArityMismatch)
{
    using namespace nw::smalls;

    Runtime& rt = nw::kernel::runtime();

    ExternalFunction ext;
    ext.qualified_name = nw::kernel::strings().intern("test.native_ext");
    ext.metadata.params.push_back(ParamMetadata{nw::String("a"), rt.int_type()});
    ext.metadata.params.push_back(ParamMetadata{nw::String("b"), rt.int_type()});
    const uint32_t ext_idx = rt.register_external_function(std::move(ext));

    BytecodeModule module("m");
    const uint32_t ref_idx = module.add_external_ref(nw::kernel::strings().intern("test.native_ext"));
    module.external_indices[ref_idx] = ext_idx;

    auto* caller = new CompiledFunction("caller");
    caller->param_count = 0;
    caller->register_count = 2;
    caller->instructions.push_back(Instruction::make_abc(Opcode::CALLEXT, 0, static_cast<uint8_t>(ref_idx), 1));
    caller->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(caller);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &rt, &err));
    EXPECT_NE(err.find("external argument count mismatch"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsScriptExternalCallArityMismatch)
{
    using namespace nw::smalls;

    Runtime& rt = nw::kernel::runtime();

    BytecodeModule external_module("provider");
    auto* callee = new CompiledFunction("callee");
    callee->param_count = 2;
    callee->register_count = 2;
    callee->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    external_module.add_function(callee);

    ExternalFunction ext;
    ext.qualified_name = nw::kernel::strings().intern("provider.callee");
    ext.script_module = &external_module;
    ext.func_idx = 0;
    const uint32_t ext_idx = rt.register_external_function(std::move(ext));

    BytecodeModule module("m");
    const uint32_t ref_idx = module.add_external_ref(nw::kernel::strings().intern("provider.callee"));
    module.external_indices[ref_idx] = ext_idx;

    auto* caller = new CompiledFunction("caller");
    caller->param_count = 0;
    caller->register_count = 2;
    caller->instructions.push_back(Instruction::make_abc(Opcode::CALLEXT, 0, static_cast<uint8_t>(ref_idx), 1));
    caller->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(caller);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &rt, &err));
    EXPECT_NE(err.find("external argument count mismatch"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsIntrinsicArityMismatch)
{
    using namespace nw::smalls;

    BytecodeModule module("m");
    auto* f = new CompiledFunction("f");
    f->param_count = 0;
    f->register_count = 2;
    f->instructions.push_back(Instruction::make_abc(Opcode::CALLINTR, 0, static_cast<uint8_t>(IntrinsicId::BitAnd), 1));
    f->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(f);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("intrinsic argument count mismatch"), nw::String::npos) << err;
}

TEST_F(SmallsBytecodeVerifier, RejectsIntrinsicRegisterFootprintOutOfRange)
{
    using namespace nw::smalls;

    BytecodeModule module("m");
    auto* f = new CompiledFunction("f");
    f->param_count = 0;
    f->register_count = 2;
    f->instructions.push_back(Instruction::make_abc(Opcode::CALLINTR, 0, static_cast<uint8_t>(IntrinsicId::MapIterNext), 1));
    f->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
    module.add_function(f);

    nw::String err;
    EXPECT_FALSE(verify_bytecode_module(&module, &err));
    EXPECT_NE(err.find("register window out of range"), nw::String::npos) << err;
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
    callee->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
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
    callee->instructions.push_back(Instruction::make_abc(Opcode::RETVOID, 0, 0, 0));
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
