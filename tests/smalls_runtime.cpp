#include "smalls_fixtures.hpp"

#include <nw/log.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/smalls/types.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string_view>

using namespace std::literals;

// Test fixture that ensures kernel services are initialized
class SmallsRuntime : public ::testing::Test {
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

TEST_F(SmallsRuntime, RegisterInternalTypes)
{
    auto& rt = nw::kernel::runtime();

    // Exact count is an implementation detail (TypeTable may carry sentinels/wildcards).
    EXPECT_GE(rt.type_table_.types_.size(), 11);

    // Check that all internal types are registered
    EXPECT_TRUE(!!rt.type_table_.get("void"));
    EXPECT_TRUE(!!rt.type_table_.get("int"));
    EXPECT_TRUE(!!rt.type_table_.get("float"));
    EXPECT_TRUE(!!rt.type_table_.get("string"));
    EXPECT_TRUE(!!rt.type_table_.get("vec3"));
    EXPECT_TRUE(!!rt.type_table_.get("object"));
    EXPECT_TRUE(!!rt.type_table_.get("any"));

    // Non-existent type should return nullptr
    EXPECT_FALSE(!!rt.type_table_.get("no_exist_type"));
}

TEST_F(SmallsRuntime, CapturesGenericFunctionTemplates)
{
    auto& rt = nw::kernel::runtime();

    auto* script = rt.load_module_from_source("test.generic_templates", R"(
        fn identity(x: $T): $T {
            return x;
        }

        fn main(): int {
            return identity(42);
        }
    )");
    ASSERT_NE(script, nullptr);

    auto* module = rt.get_or_compile_module(script);
    ASSERT_NE(module, nullptr);

    auto stats = rt.stats();
    ASSERT_TRUE(stats.contains("generic_function_template_count"));
    EXPECT_GE(stats["generic_function_template_count"].get<size_t>(), 1u);
}

TEST_F(SmallsRuntime, GenericInstantiationWorksAfterSourceCompaction)
{
    auto& rt = nw::kernel::runtime();

    auto config = rt.diagnostic_config();
    config.debug_level = nw::smalls::DebugLevel::none;
    rt.set_diagnostic_config(config);

    auto* provider = rt.load_module_from_source("test.generic_provider", R"(
        fn id(x: $T): $T {
            return x;
        }
    )");
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(rt.get_or_compile_module(provider), nullptr);
    EXPECT_TRUE(provider->ast_discarded());
    EXPECT_TRUE(provider->text().empty());

    auto* consumer = rt.load_module_from_source("test.generic_consumer", R"(
        from test.generic_provider import { id };

        fn main(): int {
            return id(42);
        }
    )");
    ASSERT_NE(consumer, nullptr);

    auto exec_result = rt.execute_script(consumer, "main");
    ASSERT_TRUE(exec_result.ok());
    EXPECT_EQ(exec_result.value.data.ival, 42);
}

TEST_F(SmallsRuntime, GenericTypeAndFunctionInstantiationFromModuleWithDebugNone)
{
    auto& rt = nw::kernel::runtime();

    auto config = rt.diagnostic_config();
    config.debug_level = nw::smalls::DebugLevel::none;
    rt.set_diagnostic_config(config);

    auto* provider = rt.load_module_from_source("test.generic_types_provider", R"(
        type Box!($T) {
            value: $T;
        };

        fn id(x: $T): $T {
            return x;
        }
    )");
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(rt.get_or_compile_module(provider), nullptr);
    EXPECT_TRUE(provider->ast_discarded());
    EXPECT_TRUE(provider->text().empty());

    auto* consumer = rt.load_module_from_source("test.generic_types_consumer", R"(
        from test.generic_types_provider import { Box, id };

        fn main(): int {
            var b: Box!(int) = { 41 };
            return id(b.value) + 1;
        }
    )");
    ASSERT_NE(consumer, nullptr);

    auto exec_result = rt.execute_script(consumer, "main");
    ASSERT_TRUE(exec_result.ok());
    EXPECT_EQ(exec_result.value.data.ival, 42);
}

TEST_F(SmallsRuntime, SourceMapCompactsAstButKeepsSource)
{
    auto& rt = nw::kernel::runtime();

    auto config = rt.diagnostic_config();
    config.debug_level = nw::smalls::DebugLevel::source_map;
    rt.set_diagnostic_config(config);

    auto* script = rt.load_module_from_source("test.empty_source_map", "\n\n");
    ASSERT_NE(script, nullptr);
    ASSERT_NE(rt.get_or_compile_module(script), nullptr);

    EXPECT_TRUE(script->ast_discarded());
    EXPECT_FALSE(script->text().empty());
}

TEST_F(SmallsRuntime, SourceMapKeepsAstForExportingModuleImports)
{
    auto& rt = nw::kernel::runtime();

    auto config = rt.diagnostic_config();
    config.debug_level = nw::smalls::DebugLevel::source_map;
    rt.set_diagnostic_config(config);

    auto* provider = rt.load_module_from_source("test.rehydrate_provider", R"(
        fn id(x: int): int {
            return x;
        }
    )");
    ASSERT_NE(provider, nullptr);
    ASSERT_NE(rt.get_or_compile_module(provider), nullptr);
    EXPECT_FALSE(provider->ast_discarded());
    EXPECT_FALSE(provider->text().empty());

    auto* consumer = rt.load_module_from_source("test.rehydrate_consumer", R"(
        from test.rehydrate_provider import { id };

        fn main(): int {
            return id(9);
        }
    )");
    ASSERT_NE(consumer, nullptr);

    auto exec_result = rt.execute_script(consumer, "main");
    ASSERT_TRUE(exec_result.ok());
    EXPECT_EQ(exec_result.value.data.ival, 9);
}

TEST_F(SmallsRuntime, NoneCompactsAstAndSource)
{
    auto& rt = nw::kernel::runtime();

    auto config = rt.diagnostic_config();
    config.debug_level = nw::smalls::DebugLevel::none;
    rt.set_diagnostic_config(config);

    auto* script = rt.load_module_from_source("test.empty_none", "\n\n");
    ASSERT_NE(script, nullptr);
    ASSERT_NE(rt.get_or_compile_module(script), nullptr);

    EXPECT_TRUE(script->ast_discarded());
    EXPECT_TRUE(script->text().empty());
}

TEST_F(SmallsRuntime, TypeIdLookup)
{
    auto& rt = nw::kernel::runtime();

    // Look up registered types
    auto int_id = rt.type_id("int");
    auto float_id = rt.type_id("float");
    auto string_id = rt.type_id("string");
    auto void_id = rt.type_id("void");
    auto vec3_id = rt.type_id("vec3");
    auto object_id = rt.type_id("object");

    // All should be valid
    EXPECT_NE(int_id, nw::smalls::invalid_type_id);
    EXPECT_NE(float_id, nw::smalls::invalid_type_id);
    EXPECT_NE(string_id, nw::smalls::invalid_type_id);
    EXPECT_NE(void_id, nw::smalls::invalid_type_id);
    EXPECT_NE(vec3_id, nw::smalls::invalid_type_id);
    EXPECT_NE(object_id, nw::smalls::invalid_type_id);

    // All should be different
    EXPECT_NE(int_id, float_id);
    EXPECT_NE(int_id, string_id);
    EXPECT_NE(float_id, string_id);

    // Non-existent type without define should return invalid
    auto bad_id = rt.type_id("nonexistent", false);
    EXPECT_EQ(bad_id, nw::smalls::invalid_type_id);
}

TEST_F(SmallsRuntime, MapObjectKeysRejected)
{
    auto& rt = nw::kernel::runtime();

    auto map_ptr = rt.alloc_map(rt.object_type(), rt.int_type());
    EXPECT_EQ(map_ptr.value, 0u);
}

TEST_F(SmallsRuntime, TypeNameLookup)
{
    auto& rt = nw::kernel::runtime();

    auto int_id = rt.type_id("int", false);
    auto float_id = rt.type_id("float", false);
    auto string_id = rt.type_id("string", false);

    EXPECT_EQ(rt.type_name(int_id), "int");
    EXPECT_EQ(rt.type_name(float_id), "float");
    EXPECT_EQ(rt.type_name(string_id), "string");

    // Invalid type ID should return "<unknown>"
    EXPECT_EQ(rt.type_name(nw::smalls::invalid_type_id), "<unknown>");
}

TEST_F(SmallsRuntime, TypeIdDefine)
{
    auto& rt = nw::kernel::runtime();

    size_t initial_count = rt.type_table_.types_.size();

    // Define a new opaque type
    auto new_id = rt.type_id("MyCustomType", true);
    EXPECT_NE(new_id, nw::smalls::invalid_type_id);
    EXPECT_EQ(rt.type_table_.types_.size(), initial_count + 1);

    // Looking it up again should return the same ID
    auto same_id = rt.type_id("MyCustomType", false);
    EXPECT_EQ(new_id, same_id);

    // Should be able to get its name back
    EXPECT_EQ(rt.type_name(new_id), "MyCustomType");
}

TEST_F(SmallsRuntime, TypeIdConsistency)
{
    auto& rt = nw::kernel::runtime();

    // Multiple lookups of the same type should return the same ID
    auto id1 = rt.type_id("int", false);
    auto id2 = rt.type_id("int", false);
    auto id3 = rt.type_id("int", false);

    EXPECT_EQ(id1, id2);
    EXPECT_EQ(id2, id3);
}

TEST_F(SmallsRuntime, InvalidTypeIdComparison)
{
    using namespace nw::smalls;

    TypeID invalid1 = invalid_type_id;
    TypeID invalid2{};

    EXPECT_EQ(invalid1, invalid2);
    EXPECT_EQ(invalid_type_id, TypeID{});

    auto& rt = nw::kernel::runtime();
    auto valid_id = rt.type_id("int", false);

    EXPECT_NE(valid_id, invalid_type_id);
}

int add(int a, int b) { return a + b; }

TEST_F(SmallsRuntime, NativeFunctionRegistration)
{
    auto& rt = nw::kernel::runtime();

    rt.module("test.math")
        .function("add", add)
        .finalize();

    // Check if registered
    uint32_t index = rt.find_native_function("test.math.add");
    EXPECT_NE(index, UINT32_MAX);

    const auto* func = rt.get_native_function(index);
    ASSERT_TRUE(func != nullptr);
    EXPECT_EQ(func->name, "test.math.add");
    EXPECT_EQ(func->metadata.params.size(), 2);

    // Check if wrapper works (manually invoking wrapper)
    nw::smalls::Value args[2];
    args[0] = nw::smalls::Value::make_int(10);
    args[1] = nw::smalls::Value::make_int(20);

    nw::smalls::Value result = func->wrapper(&rt, args, 2);
    EXPECT_EQ(result.type_id, rt.int_type());
    EXPECT_EQ(result.data.ival, 30);
}

TEST_F(SmallsRuntime, ExecuteScriptIntegration)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn multiply(a: int, b: int): int {
            return a * b;
        }
    )";

    auto* script = rt.load_module_from_source("math_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(6));
    args.push_back(nw::smalls::Value::make_int(7));

    auto exec_result = rt.execute_script(script, "multiply", args);
    ASSERT_TRUE(exec_result.ok());
    EXPECT_EQ(exec_result.value.type_id, rt.int_type());
    EXPECT_EQ(exec_result.value.data.ival, 42);
}

TEST_F(SmallsRuntime, ErrorReportingDivisionByZero)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn divide(a: int, b: int): int {
            return a / b;
        }
    )";

    auto* script = rt.load_module_from_source("error_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nw::smalls::Value::make_int(10));
    args.push_back(nw::smalls::Value::make_int(0));

    auto result = rt.execute_script(script, "divide", args);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(SmallsRuntime, ErrorReportingNonExistentFunction)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn test(): int {
            return 42;
        }
    )";

    auto* script = rt.load_module_from_source("func_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "nonexistent", args);

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(SmallsRuntime, ErrorReportingNonExistentModule)
{
    auto& rt = nw::kernel::runtime();

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script("nonexistent.module", "test", args);

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.error_message.empty());
    EXPECT_NE(result.error_message.find("nonexistent.module"), nw::String::npos);
}

TEST_F(SmallsRuntime, ErrorRecoveryAfterReset)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn divide(a: int, b: int): int {
            return a / b;
        }

        fn add(a: int, b: int): int {
            return a + b;
        }
    )";

    auto* script = rt.load_module_from_source("recovery_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args_fail;
    args_fail.push_back(nw::smalls::Value::make_int(10));
    args_fail.push_back(nw::smalls::Value::make_int(0));

    auto result_fail = rt.execute_script(script, "divide", args_fail);
    EXPECT_FALSE(result_fail.ok());

    rt.reset_error();

    nw::Vector<nw::smalls::Value> args_success;
    args_success.push_back(nw::smalls::Value::make_int(5));
    args_success.push_back(nw::smalls::Value::make_int(3));

    auto result_success = rt.execute_script(script, "add", args_success);
    ASSERT_TRUE(result_success.ok());
    EXPECT_EQ(result_success.value.type_id, rt.int_type());
    EXPECT_EQ(result_success.value.data.ival, 8);
}

TEST_F(SmallsRuntime, GasMeteringInfiniteLoop)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn main() {
            for {
            }
        }
    )";

    auto* script = rt.load_module_from_source("gas_loop_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args, 1000);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.failed);
    EXPECT_NE(result.error_message.find("Script exceeded execution limit"), nw::String::npos);
}

TEST_F(SmallsRuntime, GasMeteringInfiniteRecursion)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn recurse(): int {
            return recurse();
        }

        fn main() {
            recurse();
        }
    )";

    auto* script = rt.load_module_from_source("gas_recurse_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args, 20);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.failed);
    EXPECT_NE(result.error_message.find("Script exceeded execution limit"), nw::String::npos);
}

TEST_F(SmallsRuntime, SuccessfulExecutionHasNoError)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn success(): int {
            return 123;
        }
    )";

    auto* script = rt.load_module_from_source("success_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "success", args);

    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.failed);
    EXPECT_TRUE(result.error_message.empty());
    EXPECT_TRUE(result.stack_trace.empty());
    EXPECT_EQ(result.value.type_id, rt.int_type());
    EXPECT_EQ(result.value.data.ival, 123);
}

TEST_F(SmallsRuntime, ClosureBasicCapture)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn make_adder(n: int): fn(int): int {
            return fn(x: int): int { return x + n; };
        }

        fn main(): int {
            var add5 = make_adder(5);
            return add5(3);
        }
    )";

    auto* script = rt.load_module_from_source("closure_basic", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.type_id, rt.int_type());
    EXPECT_EQ(result.value.data.ival, 8);
}

TEST_F(SmallsRuntime, ClosureMutableCapture)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn make_counter(): fn(): int {
            var count = 0;
            return fn(): int {
                count = count + 1;
                return count;
            };
        }

        fn main(): int {
            var c = make_counter();
            c();
            return c();
        }
    )";

    auto* script = rt.load_module_from_source("closure_counter", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.type_id, rt.int_type());
    EXPECT_EQ(result.value.data.ival, 2);
}

TEST_F(SmallsRuntime, ClosureSharedUpvalue)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn make_pair(): (fn(): int, fn()) {
            var x = 0;
            var get = fn(): int { return x; };
            var inc = fn() { x = x + 1; };
            return (get, inc);
        }

        fn main(): int {
            var get, inc = make_pair();
            inc();
            return get();
        }
    )";

    auto* script = rt.load_module_from_source("closure_pair", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value.type_id, rt.int_type());
    EXPECT_EQ(result.value.data.ival, 1);
}

// == Prelude Function Tests ==================================================

TEST_F(SmallsRuntime, PreludePrintln)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn main() {
            println("Hello, world!");
        }
    )";

    auto* script = rt.load_module_from_source("prelude_println_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
}

TEST_F(SmallsRuntime, PreludePrint)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn main() {
            print("Hello");
            print(" ");
            print("world!");
        }
    )";

    auto* script = rt.load_module_from_source("prelude_print_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
}

TEST_F(SmallsRuntime, PreludeAssertPass)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn main() {
            assert(true);
            assert(1 == 1);
        }
    )";

    auto* script = rt.load_module_from_source("prelude_assert_pass_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
}

TEST_F(SmallsRuntime, PreludeAssertFail)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn main() {
            assert(false);
        }
    )";

    auto* script = rt.load_module_from_source("prelude_assert_fail_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.failed);
}

TEST_F(SmallsRuntime, PreludeError)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn main() {
            error("something went wrong");
        }
    )";

    auto* script = rt.load_module_from_source("prelude_error_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.failed);
}

TEST_F(SmallsRuntime, PreludePanic)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn main() {
            panic("critical failure");
        }
    )";

    auto* script = rt.load_module_from_source("prelude_panic_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.failed);
}

TEST_F(SmallsRuntime, PreludeFunctionsInExpressions)
{
    auto& rt = nw::kernel::runtime();

    const char* source = R"(
        fn check(x: int): int {
            if (x < 0) {
                error("x must be non-negative");
            }
            return x * 2;
        }

        fn main(): int {
            return check(5);
        }
    )";

    auto* script = rt.load_module_from_source("prelude_in_expr_test", source);
    ASSERT_NE(script, nullptr);

    nw::Vector<nw::smalls::Value> args;
    auto result = rt.execute_script(script, "main", args);
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, 10);
}
