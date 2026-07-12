#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>
#include <nw/objects/ObjectComponentSystem.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/Profile.hpp>
#include <nw/serialization/gff_conversion.hpp>

#include <nowide/cstdlib.hpp>

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
    auto mod = nw::kernel::load_module("test_data/user/modules/does_not_exist.mod");
    EXPECT_FALSE(mod);
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
    auto previous = nw::kernel::config().profile();
    nw::kernel::config().set_profile("nwn1");
    EXPECT_EQ(nw::kernel::config().profile(), "nwn1");
    nw::kernel::config().set_profile(previous);
}
