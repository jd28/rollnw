#include <gtest/gtest.h>

#include <nw/api/constants.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/rules/system.hpp>

TEST(Rules, Classes)
{
    auto mod = nw::kernel::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);
    EXPECT_EQ(nw::kernel::rules().classes.get_spell_level(nwn1::class_type_bard, nwn1::spell_fear), 3);
    EXPECT_EQ(nw::kernel::rules().classes.get_spell_level(nwn1::class_type_paladin, nwn1::spell_fear), -1);

    auto sorc = nw::kernel::rules().classes.get(nwn1::class_type_sorcerer);
    EXPECT_TRUE(sorc);
    EXPECT_EQ(sorc->spells_gained[10 * nw::kernel::rules().maximum_spell_levels()], 6);
    EXPECT_EQ(sorc->spells_known[9 * nw::kernel::rules().maximum_spell_levels() + 5], 1);
}
