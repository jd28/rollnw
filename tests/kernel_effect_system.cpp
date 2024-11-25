#include <gtest/gtest.h>

#include <nw/functions.hpp>
#include <nw/kernel/EffectSystem.hpp>
#include <nw/kernel/Objects.hpp>
#include <nw/log.hpp>
#include <nw/objects/Creature.hpp>
#include <nwn1/Profile.hpp>

namespace nwk = nw::kernel;

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
}
