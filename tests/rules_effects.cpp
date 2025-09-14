#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptapi.hpp>
#include <nw/rules/attributes.hpp>
#include <nw/rules/effects.hpp>
#include <nw/rules/feats.hpp>
#include <nw/scriptapi.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace nwk = nw::kernel;

TEST(Rules, Effects)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    nw::Effect eff;
    eff.set_string(2, "my string");
    EXPECT_EQ(eff.get_string(2), "my string");
    EXPECT_EQ(eff.get_int(3), 0);
}

TEST(Rules, ItemProperties)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto ip = nwn1::itemprop_haste();
    auto str = nw::itemprop_to_string(ip);
    EXPECT_EQ(str, "Haste");

    auto ip2 = nwn1::itemprop_ability_modifier(nwn1::ability_strength, 6);
    auto str2 = nw::itemprop_to_string(ip2);
    EXPECT_EQ(str2, "Enhancement Bonus: Strength +6");
}

TEST(EffectSystem, Pool)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    for (size_t i = 0; i < 100; ++i) {
        auto eff = nwk::effects().create(nwn1::effect_type_haste);
        nwk::effects().destroy(eff);
    }
}

TEST(EffectSystem, ApplyRemoveEffect)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto eff = nwk::effects().create(nwn1::effect_type_haste);

    auto obj = nwk::objects().load_file<nw::Creature>("test_data/user/development/nw_chicken.utc");
    EXPECT_TRUE(obj);
    EXPECT_TRUE(nw::apply_effect(obj, eff));
    EXPECT_TRUE(obj->hasted);
    EXPECT_EQ(obj->effects().size(), 1);
    EXPECT_TRUE(nw::remove_effect(obj, eff));
    EXPECT_FALSE(obj->hasted);
    EXPECT_EQ(obj->effects().size(), 0);
}

TEST(EffectSystem, IPCostParamTables)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    EXPECT_TRUE(nwk::effects().ip_cost_table(4));
    EXPECT_TRUE(nwk::effects().ip_param_table(3));
    EXPECT_EQ(nwk::effects().ip_definition(nwn1::ip_ability_bonus)->name, 649u);
    EXPECT_TRUE(nwk::effects().itemprops());
}

TEST(EffectSystem, EffectPool)
{
    nw::HandlePool<nw::EffectHandle, nw::Effect> pool{1024};

    auto eff1 = pool.create();
    EXPECT_TRUE(!!eff1);
    EXPECT_EQ(eff1->handle().generation, 1);
    EXPECT_EQ(eff1->handle().index, 0);

    auto eff2 = pool.create();
    EXPECT_TRUE(!!eff2);
    EXPECT_EQ(eff2->handle().generation, 1);
    EXPECT_EQ(eff2->handle().index, 1);
    pool.destroy(eff2->handle());

    auto eff3 = pool.create();
    EXPECT_TRUE(!!eff3);
    EXPECT_EQ(eff3->handle().generation, 2);
    EXPECT_EQ(eff3->handle().index, 1);
}
