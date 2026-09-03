#include <gtest/gtest.h>

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/resources/ResourceManager.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>
#include <nw/util/game_install.hpp>
#include <nw/util/string.hpp>

#include <filesystem>

namespace nwk = nw::kernel;
namespace fs = std::filesystem;

TEST(KernelRules, SpellSchools)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(mod);

    auto* script = nwk::runtime().load_module_from_source(
        "test.active_spellschools", R"(
            import nwn1.spellschools as Schools;

            fn main(): bool {
                return Schools.count() > 1
                    && Schools.entry(1).letter == "A";
            }
        )");
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);
    const auto result = nwk::runtime().execute_script(script, "main", {});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_TRUE(result.value.data.bval);
}

TEST(KernelRules, Spells)
{
    auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    EXPECT_TRUE(mod);

    nw::StaticTwoDA spells{nwk::resman().demand(
        nw::Resource{nw::StringView{"spells"}, nw::ResourceType::twoda})};
    nw::StaticTwoDA schools{nwk::resman().demand(
        nw::Resource{nw::StringView{"spellschools"}, nw::ResourceType::twoda})};
    ASSERT_TRUE(spells.is_valid());
    ASSERT_TRUE(schools.is_valid());

    nw::String letter;
    ASSERT_TRUE(spells.get_to(
        nwn1::spell_acid_fog.idx(), "School", letter, false));
    int32_t expected_school = -1;
    int32_t matches = 0;
    for (size_t index = 0; index < schools.rows(); ++index) {
        nw::String candidate;
        if (schools.get_to(index, "Letter", candidate, false)
            && nw::string::icmp(letter, candidate)) {
            expected_school = static_cast<int32_t>(index);
            ++matches;
        }
    }
    ASSERT_EQ(matches, 1);

    auto* script = nwk::runtime().load_module_from_source(
        "test.active_spell_school_resolution", R"(
            import nwn1.spells as Spells;
            from core.types import { Spell };

            fn main(spell: int): int {
                return Spells.school(Spell(spell)) as int;
            }
        )");
    ASSERT_NE(script, nullptr);
    ASSERT_EQ(script->errors(), 0);
    const auto result = nwk::runtime().execute_script(script, "main",
        {nw::smalls::Value::make_int(*nwn1::spell_acid_fog)});
    ASSERT_TRUE(result.ok()) << result.error_message;
    EXPECT_EQ(result.value.data.ival, expected_school);
}
