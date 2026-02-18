#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/AstCompiler.hpp>
#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/Context.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/VirtualMachine.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

using namespace std::literals;
using namespace nw::smalls;

class SmallsVMReentrant : public ::testing::Test {
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

// Global VM pointer for the native callback
static VirtualMachine* g_test_vm = nullptr;
static BytecodeModule* g_test_module = nullptr;

// Wrapper to match ModuleBuilder signature
static int32_t call_script_callback_wrapper()
{
    nw::Vector<Value> inner_args;
    Value result = g_test_vm->execute(g_test_module, "inner", inner_args);
    return result.data.ival;
}

TEST_F(SmallsVMReentrant, ScriptNativeScriptCallChain)
{
    // This tests: Script outer() → Native callback() → Script inner()

    auto& rt = nw::kernel::runtime();

    // Register native module BEFORE parsing script
    rt.module("test")
        .function("call_script_callback", &call_script_callback_wrapper)
        .finalize();

    auto script = make_script(R"(
        [[native]]
        fn call_script_callback(): int;

        fn inner(): int {
            return 42;
        }

        fn outer(): int {
            // This will call the native function, which calls back to inner()
            return call_script_callback();
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    // Compile to bytecode
    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &rt, rt.diagnostic_context());
    EXPECT_TRUE(compiler.compile());

    // Set up global pointers for the native callback
    VirtualMachine vm{};
    g_test_vm = &vm;
    g_test_module = &module;

    // Execute outer() which will trigger reentrant execution
    nw::Vector<Value> args;
    Value result = vm.execute(&module, "outer", args);

    // Clean up globals
    g_test_vm = nullptr;
    g_test_module = nullptr;

    // Verify the result
    EXPECT_FALSE(vm.failed());
    EXPECT_EQ(result.type_id, rt.int_type());
    EXPECT_EQ(result.data.ival, 42);
}

// Native function that throws an exception
static int32_t throw_exception_wrapper()
{
    throw std::runtime_error("Native function error!");
}

TEST_F(SmallsVMReentrant, NativeExceptionHandling)
{
    auto& rt = nw::kernel::runtime();

    // Register native module BEFORE parsing script
    rt.module("test")
        .function("throw_exception", &throw_exception_wrapper)
        .finalize();

    auto script = make_script(R"(
        [[native]]
        fn throw_exception(): int;

        fn test(): int {
            return throw_exception();
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &rt, rt.diagnostic_context());
    EXPECT_TRUE(compiler.compile());

    VirtualMachine vm{};
    nw::Vector<Value> args;
    vm.execute(&module, "test", args);

    // Should have failed
    EXPECT_TRUE(vm.failed());
    EXPECT_FALSE(vm.error_message().empty());
    EXPECT_TRUE(vm.error_message().find("threw exception") != nw::String::npos);
}

// Native function that calls a script which then fails
static int32_t call_failing_script_wrapper()
{
    nw::Vector<Value> inner_args;
    Value result = g_test_vm->execute(g_test_module, "will_fail", inner_args);
    return result.data.ival;
}

TEST_F(SmallsVMReentrant, NestedErrorPropagation)
{
    // Tests that errors propagate correctly through:
    // Script outer() → Native → Script will_fail() (error!)

    auto& rt = nw::kernel::runtime();

    // Register native module BEFORE parsing script
    rt.module("test")
        .function("call_failing_script", &call_failing_script_wrapper)
        .finalize();

    auto script = make_script(R"(
        [[native]]
        fn call_failing_script(): int;

        fn will_fail(): int {
            var x = 10 / 0;  // Division by zero - should trigger VM error
            return x;
        }

        fn outer(): int {
            return call_failing_script();
        }
    )"sv);

    EXPECT_NO_THROW(script.parse());
    EXPECT_EQ(script.errors(), 0);
    ASSERT_NO_THROW(script.resolve());
    EXPECT_EQ(script.errors(), 0);

    BytecodeModule module("test");
    AstCompiler compiler(&script, &module, &rt, rt.diagnostic_context());
    EXPECT_TRUE(compiler.compile());

    VirtualMachine vm{};
    g_test_vm = &vm;
    g_test_module = &module;

    nw::Vector<Value> args;
    vm.execute(&module, "outer", args);

    g_test_vm = nullptr;
    g_test_module = nullptr;

    // Should have failed (error propagated from inner call)
    EXPECT_TRUE(vm.failed());
    EXPECT_FALSE(vm.error_message().empty());
    EXPECT_TRUE(vm.error_message().find("Division by zero") != nw::String::npos);
}
