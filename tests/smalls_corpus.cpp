#include "smalls_fixtures.hpp"

#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

class SmallsCorpus : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
        nw::kernel::runtime().add_module_path(fs::path("stdlib/core"));
        nw::kernel::runtime().add_module_path(fs::path("stdlib/tests"));
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

TEST_F(SmallsCorpus, LoadTestsSuiteModule)
{
    auto& rt = nw::kernel::runtime();
    auto* tests_module = rt.load_module("tests.tests");
    ASSERT_NE(tests_module, nullptr);
    EXPECT_NE(tests_module->exports().find("main"), nullptr);
}

TEST_F(SmallsCorpus, ExecuteTestsScriptCorpus)
{
    auto& rt = nw::kernel::runtime();

    constexpr std::array<std::string_view, 24> modules = {
        "tests.arithmetic",
        "tests.array_intrinsics",
        "tests.bit_intrinsics",
        "tests.collections",
        "tests.closures",
        "tests.control_flow",
        "tests.default_args",
        "tests.foreach",
        "tests.fstrings",
        "tests.generics",
        "tests.loops_nested",
        "tests.map_intrinsics",
        "tests.module_globals",
        "tests.newtypes",
        "tests.operators",
        "tests.script_operators",
        "tests.string_intrinsics",
        "tests.strings_basic",
        "tests.structs",
        "tests.switch",
        "tests.tests",
        "tests.tuples",
        "tests.types_sums",
        "tests.value_types",
    };

    for (auto module_name : modules) {
        auto* script = rt.load_module(module_name);
        ASSERT_NE(script, nullptr) << module_name;
        ASSERT_EQ(script->errors(), 0) << module_name;
        ASSERT_NE(script->exports().find("main"), nullptr) << module_name;

        rt.reset_test_state();
        auto result = rt.execute_script(script, "main");
        ASSERT_TRUE(result.ok()) << module_name << ": " << result.error_message;
        EXPECT_EQ(rt.test_failures(), 0u) << module_name;
        EXPECT_GT(rt.test_count(), 0u) << module_name;
    }
}
