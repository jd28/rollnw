#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nw/util/scope_exit.hpp>

#include <nowide/cstdlib.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace std::literals;

namespace {

void expect_creature_hp_max(nw::Creature* creature, int32_t expected)
{
    ASSERT_NE(creature, nullptr);
    auto* vitals = nw::kernel::objects().components().find_vitals(creature->handle());
    ASSERT_NE(vitals, nullptr);
    EXPECT_EQ(vitals->hp_max, expected);
}

} // namespace

TEST(Kernel, LoadModuleErf)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);
    EXPECT_TRUE(mod->area_count() == 1);
    auto area = mod->get_area(0);
    EXPECT_TRUE(area);
    EXPECT_TRUE(area->resref == "start");
    mod->clear(); // Make sure nothing crashes, not called on unload..
}

TEST(Kernel, LoadModuleDirectory)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/module_as_dir/");
    EXPECT_TRUE(mod);
    EXPECT_EQ(mod->area_count(), 1);
    auto area = mod->get_area(0);
    EXPECT_EQ(area->resref, "test_area");
    EXPECT_TRUE(area->creatures.size() > 0);
    expect_creature_hp_max(area->creatures[0], 110);
    auto cre = nw::kernel::objects().load<nw::Creature>("test_creature"sv);
    EXPECT_TRUE(cre);
}

TEST(Kernel, LoadModuleZip)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/module_as_zip.zip");
    EXPECT_TRUE(mod);
    EXPECT_EQ(mod->area_count(), 1);
    auto area = mod->get_area(0);
    EXPECT_EQ(area->resref, "test_area");
    EXPECT_TRUE(area->creatures.size() > 0);
    expect_creature_hp_max(area->creatures[0], 110);
    auto cre = nw::kernel::objects().load<nw::Creature>("test_creature"sv);
    EXPECT_TRUE(cre);
}

TEST(Kernel, LoadMissingModule)
{
    ASSERT_TRUE(nw::kernel::resman().contains(
        {"tall_a01_01"sv, nw::ResourceType::wok}));

    auto mod = nw::kernel::load_module("test_data/user/modules/does_not_exist.mod");
    EXPECT_FALSE(mod);
    EXPECT_TRUE(nw::kernel::resman().contains(
        {"tall_a01_01"sv, nw::ResourceType::wok}));
}

TEST(Kernel, LoadModuleWithIgnoredLegacyRenderScale)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/transforms.mod");
    EXPECT_TRUE(mod);
    EXPECT_EQ(mod->area_count(), 1);
    auto area = mod->get_area(0);
    EXPECT_EQ(area->resref, "area001");
    EXPECT_GT(area->creatures.size(), 0);
}

TEST(Kernel, ConfigProfileOption)
{
    nw::kernel::Config config;
    config.set_paths(".", ".");

    nw::ConfigOptions options;
    options.profile = "custom_profile";
    config.initialize(options);

    EXPECT_EQ(config.profile(), "custom_profile");
    EXPECT_EQ(config.combat_policy_module(), "custom_profile.combat");
    EXPECT_EQ(config.effects_policy_module(), "custom_profile.effects");
    EXPECT_EQ(config.init_module(), "custom_profile.init");

    options.profile = "invalid-profile";
    EXPECT_THROW(config.initialize(options), std::invalid_argument);

    options.profile = "1invalid";
    EXPECT_THROW(config.initialize(options), std::invalid_argument);

    options.profile = "";
    EXPECT_THROW(config.initialize(options), std::invalid_argument);
}

TEST(Kernel, GameServicesRequireExplicitProfile)
{
    auto& services = nw::kernel::services();
    auto& config = nw::kernel::config();
    services.shutdown();

    config.initialize({});
    EXPECT_FALSE(config.profile().has_value());
    EXPECT_THROW(services.start(), std::runtime_error);

    nw::ConfigOptions options;
    options.profile = "nwn1";
    config.initialize(std::move(options));
    EXPECT_NO_THROW(services.start());
}

TEST(Kernel, GameStartupRejectsPackageWithoutQualifierMatcher)
{
    namespace fs = std::filesystem;

    auto& services = nw::kernel::services();
    auto& config = nw::kernel::config();
    const fs::path package = fs::path{"stdlib"} / "missing_matcher";
    services.shutdown();

    const auto restore = create_scope_exit([&services, &config, &package]() {
        services.shutdown();
        std::error_code error;
        fs::remove_all(package, error);
        nw::ConfigOptions options;
        options.profile = "nwn1";
        config.initialize(std::move(options));
    });

    std::error_code directory_error;
    fs::create_directories(package / "data_specs", directory_error);
    ASSERT_FALSE(directory_error) << directory_error.message();

    {
        std::ofstream manifest{package / "resources.json"};
        ASSERT_TRUE(manifest);
        manifest << "[]\n";
    }
    {
        std::ofstream metadata{package / "package.json"};
        ASSERT_TRUE(metadata);
        metadata << R"({"name":"missing-matcher","version":"1.0.0"})"
                 << '\n';
    }
    {
        std::ofstream propsets{package / "propsets.smalls"};
        ASSERT_TRUE(propsets);
        propsets << "const placeholder: int = 0;\n";
    }
    {
        std::ofstream profile{package / "profile.smalls"};
        ASSERT_TRUE(profile);
        profile << R"(
fn init(): bool { return true; }
fn object_instantiated(_obj: object) {}
)";
    }

    nw::ConfigOptions options;
    options.profile = "missing_matcher";
    options.init_module.clear();
    config.initialize(std::move(options));

    try {
        services.start();
        FAIL() << "game startup accepted a package without match_qualifier";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string_view{error.what()}.find("required hook contract"),
            std::string_view::npos)
            << error.what();
    }
}
