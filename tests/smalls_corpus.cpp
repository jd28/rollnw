#include "smalls_fixtures.hpp"

#include <nw/smalls/Bytecode.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::string> discover_test_modules(const fs::path& root)
{
    std::vector<std::string> result;
    if (!fs::is_directory(root)) { return result; }
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".smalls") { continue; }
        result.push_back("tests." + entry.path().stem().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string corpus_module_test_name(const ::testing::TestParamInfo<std::string>& info)
{
    std::string result = info.param;
    for (char& ch : result) {
        auto c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c)) {
            ch = '_';
        }
    }
    return result;
}

} // namespace

class SmallsCorpus : public ::testing::Test {
protected:
    void SetUp() override
    {
        nw::kernel::services().start();
        nw::kernel::runtime().add_module_path(fs::path("stdlib/core"));
        nw::kernel::runtime().add_module_path(fs::path("stdlib/tests"));
        nw::kernel::runtime().set_script_tests_enabled(true);
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
    }
};

class SmallsCorpusModule : public SmallsCorpus, public ::testing::WithParamInterface<std::string> {
};

TEST_F(SmallsCorpus, LoadTestsSuiteModule)
{
    auto& rt = nw::kernel::runtime();
    auto* tests_module = rt.load_module("tests.tests");
    ASSERT_NE(tests_module, nullptr);

    auto tests = rt.module_tests(tests_module);
    ASSERT_EQ(tests.size(), 1u);
    EXPECT_EQ(tests[0].name, "tests.tests corpus");
}

TEST_F(SmallsCorpus, DiscoversScriptCorpusModules)
{
    const auto modules = discover_test_modules("stdlib/tests");
    ASSERT_EQ(modules.size(), 24u);
}

TEST_P(SmallsCorpusModule, ExecutesScriptCorpusModule)
{
    auto& rt = nw::kernel::runtime();
    const std::string& module_name = GetParam();
    auto* script = rt.load_module(module_name);
    ASSERT_NE(script, nullptr) << module_name;
    ASSERT_EQ(script->errors(), 0) << module_name;

    auto tests = rt.module_tests(script);
    ASSERT_EQ(tests.size(), 1u) << module_name;

    rt.reset_test_state();
    auto results = rt.execute_tests(script);
    ASSERT_EQ(results.size(), 1u) << module_name;
    ASSERT_TRUE(results[0].ok()) << module_name << ": " << results[0].error_message;
    EXPECT_EQ(rt.test_failures(), 0u) << module_name;
    EXPECT_GT(rt.test_count(), 0u) << module_name;
}

INSTANTIATE_TEST_SUITE_P(ScriptCorpus, SmallsCorpusModule,
    ::testing::ValuesIn(discover_test_modules("stdlib/tests")),
    corpus_module_test_name);
