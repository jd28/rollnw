#include <gtest/gtest.h>

#include "nwn1_test_builders.hpp"

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/effects.hpp>
#include <nw/rules/feats.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

namespace {

struct TemporaryModuleServices {
    fs::path path;

    ~TemporaryModuleServices()
    {
        nwk::services().shutdown();
        std::error_code error;
        fs::remove_all(path, error);
    }
};

void write_itempropdef(const fs::path& path, std::string_view cost_reference)
{
    std::ofstream out{path / "itempropdef.2da"};
    out << "2DA V2.0\n\n"
           "Name SubTypeResRef CostTableResRef Param1ResRef Cost GameStrRef Description\n"
           "0 424242 **** "
        << cost_reference
        << " **** 1.5 424243 424244\n";
}

nw::String test_itemprop_to_string(const nw::ItemProperty& ip)
{
    nw::String result;
    if (ip.type == std::numeric_limits<uint16_t>::max()) { return result; }
    auto type = nw::ItemPropertyType::make(ip.type);
    auto def = nwk::effects().ip_definition(type);
    if (!def) { return result; }

    result = nwk::strings().get(def->game_string);

    if (ip.subtype != std::numeric_limits<uint16_t>::max() && def->subtype) {
        if (auto name = def->subtype->get<uint32_t>(ip.subtype, "Name")) {
            result += " " + nwk::strings().get(*name);
        }
    }

    if (ip.cost_value != std::numeric_limits<uint16_t>::max() && def->cost_table) {
        if (auto name = def->cost_table->get<uint32_t>(ip.cost_value, "Name")) {
            result += " " + nwk::strings().get(*name);
        }
    }

    if (ip.param_value != std::numeric_limits<uint8_t>::max() && def->param_table) {
        if (auto name = def->param_table->get<uint32_t>(ip.param_value, "Name")) {
            result += " " + nwk::strings().get(*name);
        }
    }

    return result;
}

} // namespace

TEST(Rules, Effects)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto eff = nwk::effects().create(nw::EffectType::make(1));
    ASSERT_NE(eff, nullptr);
    eff->set_string(2, "my string");
    EXPECT_EQ(eff->get_string(2), "my string");
    EXPECT_EQ(eff->get_int(3), 0);
    nwk::effects().destroy(eff);
}

TEST(Rules, ItemProperties)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ip = nwn1::itemprop_haste();
    auto str = test_itemprop_to_string(ip);
    EXPECT_EQ(str, "Haste");

    auto ip2 = nwn1::itemprop_ability_modifier(nw::Ability::make(0), 6);
    auto str2 = test_itemprop_to_string(ip2);
    EXPECT_EQ(str2, "Enhancement Bonus: Strength +6");
}

TEST(EffectSystem, Pool)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    for (size_t i = 0; i < 100; ++i) {
        auto eff = nwk::effects().create(nw::EffectType::make(1));
        nwk::effects().destroy(eff);
    }
}

TEST(EffectSystem, ApplyRemoveEffect)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto eff = nwk::effects().create(nw::EffectType::make(1));

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(obj);
    EXPECT_TRUE(nwk::effects().apply_to(obj, eff));
    // Note: haste status is tracked in propset, not synced back to C++ object
    EXPECT_EQ(obj->effects().size(), 1);
    const bool removed = nwk::effects().remove_from(obj, eff);
    if (removed) { nwk::effects().destroy(eff); }
    EXPECT_TRUE(removed);
    EXPECT_EQ(obj->effects().size(), 0);
}

TEST(EffectSystem, IPCostParamTables)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    EXPECT_TRUE(nwk::effects().ip_cost_table(4));
    EXPECT_TRUE(nwk::effects().ip_param_table(3));
    EXPECT_EQ(nwk::effects().ip_definition(nw::ItemPropertyType::make(0))->name, 649u);
    EXPECT_TRUE(nwk::effects().itemprops());
}

TEST(EffectSystem, ItemPropertyCatalogPublishesActiveModuleBatchAtomically)
{
    const fs::path module_path = "tmp/item_property_catalog_module";
    std::error_code error;
    fs::remove_all(module_path, error);
    fs::copy("test_data/user/modules/module_as_dir", module_path,
        fs::copy_options::recursive, error);
    ASSERT_FALSE(error);
    TemporaryModuleServices cleanup{module_path};

    write_itempropdef(module_path, "****");
    auto* module = nwk::load_module(module_path);
    ASSERT_NE(module, nullptr);
    ASSERT_EQ(nwk::effects().ip_definitions().size(), 1u);
    EXPECT_EQ(nwk::effects().ip_definition(nw::ItemPropertyType::make(0))->name,
        424242u);

    nwk::effects().initialize(nwk::ServiceInitTime::module_post_load);
    EXPECT_EQ(nwk::effects().ip_definitions().size(), 1u);

    write_itempropdef(module_path, "-1");
    module = nwk::load_module(module_path);
    ASSERT_NE(module, nullptr);
    EXPECT_TRUE(nwk::effects().ip_definitions().empty());
    EXPECT_EQ(nwk::effects().itemprops(), nullptr);
    EXPECT_FALSE(nwk::rules().appearances.entries.empty());

    write_itempropdef(module_path, "2147483647");
    module = nwk::load_module(module_path);
    ASSERT_NE(module, nullptr);
    EXPECT_TRUE(nwk::effects().ip_definitions().empty());
    EXPECT_EQ(nwk::effects().itemprops(), nullptr);
    EXPECT_FALSE(nwk::rules().appearances.entries.empty());
}

TEST(EffectSystem, EffectPool)
{
    nw::RuntimeObjectPool pool;

    auto h1 = pool.allocate_effect();
    auto* eff1 = static_cast<nw::Effect*>(pool.get(h1));
    EXPECT_TRUE(!!eff1);
    EXPECT_EQ(h1.generation, 1);
    EXPECT_EQ(h1.id, 0);
    EXPECT_EQ(h1.type, nw::RuntimeObjectPool::TYPE_EFFECT);

    auto h2 = pool.allocate_effect();
    auto* eff2 = static_cast<nw::Effect*>(pool.get(h2));
    EXPECT_TRUE(!!eff2);
    EXPECT_EQ(h2.generation, 1);
    EXPECT_EQ(h2.id, 1);
    pool.destroy(h2);

    auto h3 = pool.allocate_effect();
    auto* eff3 = static_cast<nw::Effect*>(pool.get(h3));
    EXPECT_TRUE(!!eff3);
    EXPECT_EQ(h3.generation, 2);
    EXPECT_EQ(h3.id, 1);
    EXPECT_EQ(h3.type, nw::RuntimeObjectPool::TYPE_EFFECT);
}
