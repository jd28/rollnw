#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/rules/combat.hpp>
#include <nw/rules/combat_scheduler.hpp>
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

void write_combat_test_package(const std::filesystem::path& package,
    std::string_view combat_source)
{
    namespace fs = std::filesystem;
    std::error_code error;
    fs::remove_all(package, error);
    error.clear();
    fs::create_directories(package / "data_specs", error);
    if (error) {
        throw std::runtime_error(error.message());
    }

    std::ofstream{package / "resources.json"} << "[]\n";
    std::ofstream{package / "package.json"}
        << R"({"name":"combat-test","version":"1.0.0"})"
        << '\n';
    std::ofstream{package / "propsets.smalls"}
        << "const placeholder: int = 0;\n";
    std::ofstream{package / "profile.smalls"} << R"(
fn init(): bool { return true; }
fn object_instantiated(_obj: object) {}
fn match_qualifier(_obj: object, _type: int, _subtype: int, _match: int, _value: int): bool {
    return true;
}
)";
    std::ofstream{package / "combat.smalls"} << combat_source;
}

constexpr std::string_view package_neutral_combat_source = R"(
import core.effects as effects;

[[value_type]]
type PackageAttackResult {
    attack_type: int;
    attack_result: int;
    attack_roll: int;
    attack_bonus: int;
    armor_class: int;
    nth_attack: int;
    damage_total: int;
    critical_multiplier: int;
    critical_threat: int;
    concealment: int;
    iteration_penalty: int;
    is_ranged: bool;
    target_is_creature: bool;
    effects_to_apply: array!(effects.Effect);
    effects_to_remove: array!(effects.Effect);
};

fn resolve_attack(_attacker: Creature, _target: object): PackageAttackResult {
    var apply: array!(effects.Effect) = {};
    var remove: array!(effects.Effect) = {};
    return PackageAttackResult {
        attack_type = 2,
        attack_result = 2,
        attack_roll = 19,
        attack_bonus = 42,
        armor_class = 13,
        nth_attack = 1,
        damage_total = 17,
        critical_multiplier = 3,
        critical_threat = 4,
        concealment = 11,
        iteration_penalty = 5,
        is_ranged = true,
        target_is_creature = true,
        effects_to_apply = apply,
        effects_to_remove = remove,
    };
}

fn resolve_attack_cooldown_ticks(_attacker: Creature, _round_ticks: int): int {
    return 7;
}
)";

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

TEST(Kernel, GameStartupRejectsInvalidCombatPolicyContract)
{
    namespace fs = std::filesystem;

    auto& services = nw::kernel::services();
    auto& config = nw::kernel::config();
    const fs::path package = fs::path{"stdlib"} / "invalid_combat_policy";
    services.shutdown();

    const auto restore = create_scope_exit([&services, &config, &package]() {
        services.shutdown();
        std::error_code error;
        fs::remove_all(package, error);
        nw::ConfigOptions options;
        options.profile = "nwn1";
        config.initialize(std::move(options));
    });

    write_combat_test_package(package, R"(
[[value_type]]
type IncompleteAttackResult { attack_type: int; };
fn resolve_attack(_attacker: Creature, _target: object): IncompleteAttackResult {
    return IncompleteAttackResult { attack_type = 0 };
}
)");

    nw::ConfigOptions options;
    options.profile = "invalid_combat_policy";
    config.initialize(std::move(options));
    config.set_init_module("");
    EXPECT_THROW(services.start(), std::runtime_error);

    services.shutdown();
    write_combat_test_package(package, R"(
[[value_type]]
type IncompleteAttackResult { attack_type: int; };
fn resolve_attack(_attacker: Creature, _target: object): IncompleteAttackResult {
    return IncompleteAttackResult { attack_type = 0 };
}
fn resolve_attack_cooldown_ticks(_attacker: Creature, _round_ticks: int): int {
    return 1;
}
)");
    EXPECT_THROW(services.start(), std::runtime_error);
}

TEST(Kernel, CombatSchedulerUsesStructurallyValidatedPackagePolicy)
{
    namespace fs = std::filesystem;

    auto& services = nw::kernel::services();
    auto& config = nw::kernel::config();
    const fs::path package = fs::path{"stdlib"} / "package_neutral_combat";
    services.shutdown();

    const auto restore = create_scope_exit([&services, &config, &package]() {
        services.shutdown();
        std::error_code error;
        fs::remove_all(package, error);
        nw::ConfigOptions options;
        options.profile = "nwn1";
        config.initialize(std::move(options));
    });

    write_combat_test_package(package, package_neutral_combat_source);
    nw::ConfigOptions options;
    options.profile = "package_neutral_combat";
    config.initialize(std::move(options));
    config.set_init_module("");
    ASSERT_NO_THROW(services.start());

    auto* attacker = nw::kernel::objects().make<nw::Creature>();
    auto* target = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(attacker, nullptr);
    ASSERT_NE(target, nullptr);

    nw::AttackData attack;
    ASSERT_TRUE(nw::combat::resolve_attack(attacker, target, &attack));
    EXPECT_EQ(attack.type, nw::AttackType::make(2));
    EXPECT_EQ(attack.result, nw::AttackResult::hit_by_roll);
    EXPECT_EQ(attack.attack_roll, 19);
    EXPECT_EQ(attack.attack_bonus, 42);
    EXPECT_EQ(attack.armor_class, 13);
    EXPECT_EQ(attack.nth_attack, 1);
    EXPECT_EQ(attack.damage_total, 17);
    EXPECT_EQ(attack.multiplier, 3);
    EXPECT_EQ(attack.threat_range, 4);
    EXPECT_EQ(attack.concealment, 11);
    EXPECT_EQ(attack.iteration_penalty, 5);
    EXPECT_TRUE(attack.is_ranged_attack);
    EXPECT_TRUE(attack.target_is_creature);
    EXPECT_EQ(nw::combat::resolve_attack_cooldown_ticks(attacker, 60), 7u);

    config.set_combat_policy_module("changed_after_bootstrap");
    EXPECT_FALSE(nw::combat::resolve_attack(attacker, target));
    EXPECT_EQ(nw::combat::resolve_attack_cooldown_ticks(attacker, 60), 1u);

    services.shutdown();
    nw::ConfigOptions restart_options;
    restart_options.profile = "package_neutral_combat";
    config.initialize(std::move(restart_options));
    config.set_init_module("");
    ASSERT_NO_THROW(services.start());
    attacker = nw::kernel::objects().make<nw::Creature>();
    target = nw::kernel::objects().make<nw::Creature>();
    ASSERT_NE(attacker, nullptr);
    ASSERT_NE(target, nullptr);
    EXPECT_TRUE(nw::combat::resolve_attack(attacker, target));
}
