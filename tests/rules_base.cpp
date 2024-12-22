#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/system.hpp>
#include <nwn1/constants.hpp>

TEST(Rules, Classes)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);
    EXPECT_EQ(nw::kernel::rules().classes.get_spell_level(nwn1::class_type_bard, nwn1::spell_fear), 3);
    EXPECT_EQ(nw::kernel::rules().classes.get_spell_level(nwn1::class_type_paladin, nwn1::spell_fear), -1);
}
