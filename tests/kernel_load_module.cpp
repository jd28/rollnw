#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/log.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Module.hpp>
#include <nw/serialization/gff_conversion.hpp>
#include <nwn1/Profile.hpp>

#include <nowide/cstdlib.hpp>

using namespace std::literals;

TEST(Kernel, LoadModuleErf)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);
    EXPECT_TRUE(mod->area_count() == 1);
    auto area = mod->get_area(0);
    EXPECT_TRUE(area);
    EXPECT_TRUE(area->common.resref == "start");
    mod->clear(); // Make sure nothing crashes, not called on unload..
}

TEST(Kernel, LoadModuleDirectory)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/module_as_dir/");
    EXPECT_TRUE(mod);
    EXPECT_EQ(mod->area_count(), 1);
    auto area = mod->get_area(0);
    EXPECT_EQ(area->common.resref, "test_area");
    EXPECT_TRUE(area->creatures.size() > 0);
    EXPECT_EQ(area->creatures[0]->hp_max, 110);
    auto cre = nw::kernel::objects().load<nw::Creature>("test_creature"sv);
    EXPECT_TRUE(cre);
}

TEST(Kernel, LoadModuleZip)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/module_as_zip.zip");
    EXPECT_TRUE(mod);
    EXPECT_EQ(mod->area_count(), 1);
    auto area = mod->get_area(0);
    EXPECT_EQ(area->common.resref, "test_area");
    EXPECT_TRUE(area->creatures.size() > 0);
    EXPECT_EQ(area->creatures[0]->hp_max, 110);
    auto cre = nw::kernel::objects().load<nw::Creature>("test_creature"sv);
    EXPECT_TRUE(cre);
}

TEST(Kernel, LoadModuleWithScaled)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/transforms.mod");
    EXPECT_TRUE(mod);
    EXPECT_EQ(mod->area_count(), 1);
    auto area = mod->get_area(0);
    EXPECT_EQ(area->common.resref, "area001");
    EXPECT_GT(area->creatures.size(), 0);
    EXPECT_GT(area->creatures[0]->visual_transform().size(), 0);
    EXPECT_NEAR(area->creatures[0]->visual_transform()[0].scale_x.value_to, 2.2f, 0.1);
}
