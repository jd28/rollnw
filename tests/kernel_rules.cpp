#include <gtest/gtest.h>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Rules.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/util/game_install.hpp>

#include <filesystem>

namespace nwk = nw::kernel;
namespace fs = std::filesystem;

TEST(KernelRules, SpellSchools)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    auto& spellschools = nwk::rules().spellschools;
    EXPECT_GT(spellschools.entries.size(), 1);
    EXPECT_EQ(spellschools.entries[1].letter, "A");
}

TEST(KernelRules, Spells)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    const auto* acid_fog = nwk::rules().spells.get(nwn1::spell_acid_fog);
    ASSERT_TRUE(acid_fog);
    EXPECT_EQ(*acid_fog->metamagic_mask, 0x3f);
}
