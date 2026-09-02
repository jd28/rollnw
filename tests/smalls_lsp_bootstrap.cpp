#include <nw/kernel/Kernel.hpp>
#include <nw/rules/effects.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/scope_exit.hpp>

#include <gtest/gtest.h>

#include <filesystem>

TEST(SmallsLspBootstrap, LanguageModeDoesNotCreateOrLoadAGameProfile)
{
    auto& services = nw::kernel::services();
    auto& config = nw::kernel::config();
    services.shutdown();
    nw::ConfigOptions no_profile;
    config.initialize(std::move(no_profile));
    const auto shutdown = create_scope_exit([&services, &config]() {
        services.shutdown();
        nw::ConfigOptions options;
        options.profile = "nwn1";
        config.initialize(std::move(options));
    });

    services.create(nw::kernel::ServiceMode::language);
    auto& runtime = nw::kernel::runtime();
    runtime.add_module_path(std::filesystem::path{"stdlib/core"});

    EXPECT_NO_THROW(services.start(nw::kernel::ServiceMode::language));
    EXPECT_FALSE(config.profile().has_value());
    EXPECT_EQ(services.profile(), nullptr);
    EXPECT_EQ(services.get<nw::EffectSystem>(), nullptr);
    EXPECT_EQ(runtime.type_id("nwn1.propsets.ItemStats", false),
        nw::smalls::invalid_type_id);

    auto* core_item = runtime.load_module("core.item");
    ASSERT_NE(core_item, nullptr);
    EXPECT_EQ(core_item->errors(), 0);
}
