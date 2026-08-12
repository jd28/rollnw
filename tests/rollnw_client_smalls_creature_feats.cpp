#include <gtest/gtest.h>

#include "../tools/ui/smalls_creature_feats.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/smalls/runtime.hpp>

#include <algorithm>
#include <cctype>

namespace nwk = nw::kernel;

TEST(ClientSmallsCreatureFeats, BuildsFilteredRowsFromLiveAssignmentData)
{
    auto module = nwk::load_module("test_data/user/modules/DockerDemo.mod");
    ASSERT_TRUE(module);
    auto* creature = nwk::objects().load_file<nw::Creature>("test_data/user/development/pl_agent_001.utc");
    ASSERT_NE(creature, nullptr);

    nw::toolset::CreatureFeatViewSnapshot snapshot;
    nw::toolset::build_creature_feat_rows(nwk::runtime(), creature->handle(), "", snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::CreatureFeatViewStatus::ready) << snapshot.diagnostic;
    ASSERT_FALSE(snapshot.rows.empty());
    EXPECT_EQ(std::count_if(snapshot.rows.begin(), snapshot.rows.end(), [](const auto& row) {
        return row.assigned;
    }),
        snapshot.assigned_count);
    for (size_t index = 1; index < snapshot.rows.size(); ++index) {
        std::string previous{snapshot.text_view(snapshot.rows[index - 1].name)};
        std::string current{snapshot.text_view(snapshot.rows[index].name)};
        std::ranges::transform(previous, previous.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        std::ranges::transform(current, current.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        EXPECT_TRUE((!previous.empty() && current.empty())
            || previous < current
            || (previous == current
                && snapshot.rows[index - 1].feat_id < snapshot.rows[index].feat_id))
            << previous << " should sort before " << current;
    }

    const auto feat = nwn1::feat_epic_toughness_1;
    auto row = std::find_if(snapshot.rows.begin(), snapshot.rows.end(), [feat](const auto& value) {
        return value.feat_id == static_cast<uint32_t>(*feat);
    });
    ASSERT_NE(row, snapshot.rows.end());
    ASSERT_FALSE(row->assigned);
    const std::string name{snapshot.text_view(row->name)};
    ASSERT_FALSE(name.empty());

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*feat));
    args.push_back(nw::smalls::Value::make_bool(true));
    const auto changed = nwk::runtime().execute_script("nwn1.creature_state", "set_feat", args);
    ASSERT_TRUE(changed.ok()) << changed.error_message;
    ASSERT_TRUE(changed.value.data.bval);

    nw::toolset::build_creature_feat_rows(nwk::runtime(), creature->handle(), name, snapshot);
    ASSERT_EQ(snapshot.status, nw::toolset::CreatureFeatViewStatus::ready) << snapshot.diagnostic;
    row = std::find_if(snapshot.rows.begin(), snapshot.rows.end(), [feat](const auto& value) {
        return value.feat_id == static_cast<uint32_t>(*feat);
    });
    ASSERT_NE(row, snapshot.rows.end());
    EXPECT_TRUE(row->assigned);
}

TEST(ClientSmallsCreatureFeats, RejectsNonCreatureHandles)
{
    nw::toolset::CreatureFeatViewSnapshot snapshot;
    nw::toolset::build_creature_feat_rows(nwk::runtime(), nw::ObjectHandle{}, "", snapshot);
    EXPECT_EQ(snapshot.status, nw::toolset::CreatureFeatViewStatus::invalid_object);
    EXPECT_TRUE(snapshot.rows.empty());
}
