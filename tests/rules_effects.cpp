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

    auto eff = nwk::effects().create(nwn1::effect_type_haste);
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
