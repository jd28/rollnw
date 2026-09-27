#include <nw/kernel/Kernel.hpp>
#include <nw/rules/effects.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

class SmallsLspBootstrap : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto& services = nw::kernel::services();
        services.shutdown();
        nw::kernel::config().initialize({});
        services.create(nw::kernel::ServiceMode::language);
        nw::kernel::runtime().add_module_path(std::filesystem::path{"stdlib/core"});
        services.start(nw::kernel::ServiceMode::language);
        nw::kernel::runtime().set_diagnostic_config({nw::smalls::DebugLevel::full});
    }

    void TearDown() override
    {
        nw::kernel::services().shutdown();
        nw::ConfigOptions options;
        options.profile = "nwn1";
        nw::kernel::config().initialize(std::move(options));
    }
};

TEST_F(SmallsLspBootstrap, LanguageModeDoesNotSelectOrLoadAGamePackage)
{
    auto& services = nw::kernel::services();
    auto& runtime = nw::kernel::runtime();

    EXPECT_FALSE(nw::kernel::config().profile().has_value());
    EXPECT_EQ(services.get<nw::EffectSystem>(), nullptr);
    EXPECT_EQ(runtime.type_id("nwn1.propsets.ItemStats", false),
        nw::smalls::invalid_type_id);

    auto* core_item = runtime.load_module("core.item");
    ASSERT_NE(core_item, nullptr);
    EXPECT_EQ(core_item->errors(), 0);
}

TEST_F(SmallsLspBootstrap, BundledCoreModulesResolveWithoutTheirApplicationHost)
{
    auto& runtime = nw::kernel::runtime();
    const auto root = std::filesystem::path{"stdlib/core"};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".smalls") { continue; }
        const auto module = runtime.path_to_module_name(entry.path(), root.parent_path());
        SCOPED_TRACE(module);
        std::ifstream input{entry.path()};
        ASSERT_TRUE(input.is_open());
        std::ostringstream source;
        source << input.rdbuf();
        auto* script = runtime.load_module_from_source(module, source.str());
        ASSERT_NE(script, nullptr);
        EXPECT_EQ(script->errors(), 0);
    }
}

TEST_F(SmallsLspBootstrap, UnboundNativeDeclarationsStillCheckSourceTypes)
{
    auto& runtime = nw::kernel::runtime();
    auto* valid = runtime.load_module_from_source("host.valid", R"(
        [[native]] type Handle;
        [[native]] type Row { value: int; };
        [[native]] fn read(handle: Handle): Row;
        fn value(handle: Handle): int { return read(handle).value; }
    )");
    ASSERT_NE(valid, nullptr);
    EXPECT_EQ(valid->errors(), 0);

    auto* invalid_call = runtime.load_module_from_source("host.invalid_call", R"(
        [[native]] fn read(index: int): int;
        fn value(): int { return read("wrong"); }
    )");
    ASSERT_NE(invalid_call, nullptr);
    EXPECT_GT(invalid_call->errors(), 0);

    auto* invalid_body = runtime.load_module_from_source("host.invalid_body", R"(
        [[native]] fn read(): int { return 1; }
    )");
    ASSERT_NE(invalid_body, nullptr);
    EXPECT_GT(invalid_body->errors(), 0);
}

TEST_F(SmallsLspBootstrap, RegisteredNativeDeclarationsStillCheckTheirHostContract)
{
    auto& runtime = nw::kernel::runtime();
    runtime.module("host.registered")
        .function("read", +[](int32_t value) -> int32_t { return value; })
        .finalize();
    auto* script = runtime.load_module_from_source("host.registered", R"(
        [[native]] fn read(value: string): int;
    )");
    ASSERT_NE(script, nullptr);
    EXPECT_GT(script->errors(), 0);
}

TEST_F(SmallsLspBootstrap, EvictionRefreshesCachedCoreScripts)
{
    auto& runtime = nw::kernel::runtime();
    auto* prelude = runtime.core_prelude();
    auto* tests = runtime.core_test();
    ASSERT_NE(prelude, nullptr);
    ASSERT_NE(tests, nullptr);

    const std::array<nw::StringView, 2> changed{"core.prelude", "core.test"};
    runtime.evict_modules(changed);
    auto* reloaded_prelude = runtime.core_prelude();
    auto* reloaded_tests = runtime.core_test();
    ASSERT_NE(reloaded_prelude, nullptr);
    ASSERT_NE(reloaded_tests, nullptr);
    EXPECT_NE(reloaded_prelude, prelude);
    EXPECT_NE(reloaded_tests, tests);
    EXPECT_EQ(reloaded_prelude->errors(), 0);
    EXPECT_EQ(reloaded_tests->errors(), 0);
    EXPECT_NE(reloaded_prelude->exports().find("println"), nullptr);
}

TEST_F(SmallsLspBootstrap, EvictionInvalidatesImplicitPreludeDependents)
{
    auto& runtime = nw::kernel::runtime();
    auto* prelude = runtime.load_module_from_source("host.prelude", "const value = 1;");
    ASSERT_NE(prelude, nullptr);
    runtime.set_user_prelude("host.prelude");
    auto* consumer = runtime.load_module_from_source("host.consumer", "fn read(): int { return value; }");
    ASSERT_NE(consumer, nullptr);
    ASSERT_EQ(consumer->errors(), 0);
    const auto dependencies = consumer->dependencies();
    EXPECT_NE(std::find(dependencies.begin(), dependencies.end(), "host.prelude"), dependencies.end());
    EXPECT_NE(std::find(dependencies.begin(), dependencies.end(), "core.prelude"), dependencies.end());

    const std::array<nw::StringView, 1> changed{"host.prelude"};
    runtime.evict_modules(changed);
    EXPECT_EQ(runtime.user_prelude(), nullptr);
    ASSERT_NE(runtime.load_module_from_source("host.prelude", "const value = \"changed\";"), nullptr);
    runtime.set_user_prelude("host.prelude");
    auto* reloaded = runtime.load_module_from_source("host.consumer", "fn read(): int { return value; }");
    ASSERT_NE(reloaded, nullptr);
    EXPECT_NE(reloaded, consumer);
    EXPECT_GT(reloaded->errors(), 0);
}
