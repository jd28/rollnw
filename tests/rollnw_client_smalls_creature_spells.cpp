#include <gtest/gtest.h>

#include "../tools/ui/smalls_creature_spells.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <cctype>

namespace nwk = nw::kernel;

TEST(ClientSmallsCreatureSpells, BuildsSortedFilteredRowsFromLiveLoadout)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/wizard_pm.utc");
    ASSERT_NE(creature, nullptr);

    nw::toolset::CreatureSpellViewSnapshot snapshot;
    nw::toolset::build_creature_spell_rows(nwk::runtime(), creature->handle(),
        -1, -1, snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::CreatureSpellViewStatus::ready)
        << snapshot.diagnostic;
    EXPECT_EQ(snapshot.selected_class, *nwn1::class_type_wizard);
    EXPECT_TRUE(snapshot.memorizes);
    ASSERT_FALSE(snapshot.rows.empty());

    for (size_t index = 1; index < snapshot.rows.size(); ++index) {
        std::string previous{snapshot.text_view(snapshot.rows[index - 1].name)};
        std::string current{snapshot.text_view(snapshot.rows[index].name)};
        std::ranges::transform(previous, previous.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        std::ranges::transform(current, current.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        EXPECT_TRUE(previous < current
            || (previous == current
                && snapshot.rows[index - 1].spell_id < snapshot.rows[index].spell_id));
    }

    const auto fireball = std::ranges::find(snapshot.rows,
        *nwn1::spell_fireball, &nw::toolset::CreatureSpellRow::spell_id);
    ASSERT_NE(fireball, snapshot.rows.end());
    EXPECT_EQ(fireball->level, 3);

    std::vector<uint32_t> matches;
    const std::string fireball_name{snapshot.text_view(fireball->name)};
    nw::toolset::filter_creature_spell_rows(snapshot, fireball_name, 3, matches);
    ASSERT_EQ(matches.size(), 1);
    EXPECT_EQ(snapshot.rows[matches.front()].spell_id, *nwn1::spell_fireball);

    nw::toolset::filter_creature_spell_rows(snapshot, "", 10, matches);
    EXPECT_TRUE(matches.empty());
}

TEST(ClientSmallsCreatureSpells, RejectsNonCreatureHandles)
{
    nw::toolset::CreatureSpellViewSnapshot snapshot;
    nw::toolset::build_creature_spell_rows(
        nwk::runtime(), nw::ObjectHandle{}, -1, -1, snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::CreatureSpellViewStatus::invalid_object);
    EXPECT_TRUE(snapshot.rows.empty());
}

TEST(ClientSmallsCreatureSpells, ProjectsKnownStateForSpontaneousCaster)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/sorcrdd.utc");
    ASSERT_NE(creature, nullptr);

    nw::toolset::CreatureSpellViewSnapshot snapshot;
    nw::toolset::build_creature_spell_rows(nwk::runtime(), creature->handle(),
        *nwn1::class_type_sorcerer, *nw::metamagic_none, snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::CreatureSpellViewStatus::ready)
        << snapshot.diagnostic;
    EXPECT_EQ(snapshot.selected_class, *nwn1::class_type_sorcerer);
    EXPECT_FALSE(snapshot.memorizes);

    const auto light = std::ranges::find(snapshot.rows,
        *nwn1::spell_light, &nw::toolset::CreatureSpellRow::spell_id);
    ASSERT_NE(light, snapshot.rows.end());
    EXPECT_TRUE(light->known);
    EXPECT_EQ(light->uses, 0);
}

TEST(ClientSmallsCreatureSpells, ReportsCreatureWithoutSpellcastingClasses)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>(
        "test_data/user/development/pl_agent_001.utc.json");
    ASSERT_NE(creature, nullptr);

    nw::toolset::CreatureSpellViewSnapshot snapshot;
    nw::toolset::build_creature_spell_rows(
        nwk::runtime(), creature->handle(), -1, -1, snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::CreatureSpellViewStatus::ready);
    EXPECT_TRUE(snapshot.classes.empty());
    EXPECT_TRUE(snapshot.rows.empty());
    EXPECT_EQ(snapshot.diagnostic, "Creature has no spellcasting classes");
}
