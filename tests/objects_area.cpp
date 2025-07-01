#include <gtest/gtest.h>

#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(Area, GffDeserialize)
{
    auto name = nw::Area::get_name_from_file(fs::path("test_data/user/development/test_area.are"));
    EXPECT_EQ(name, "test_area");

    auto ent = nw::kernel::objects().make<nw::Area>();
    nw::Gff are{"test_data/user/development/test_area.are"};
    EXPECT_TRUE(are.valid());
    nw::Gff git{"test_data/user/development/test_area.git"};
    EXPECT_TRUE(git.valid());
    nw::Gff gic{"test_data/user/development/test_area.gic"};
    EXPECT_TRUE(gic.valid());

    deserialize(ent, are.toplevel(), git.toplevel(), gic.toplevel());

    EXPECT_EQ(ent->tileset_resref, "ttf02");
    EXPECT_TRUE(!(ent->flags & nw::AreaFlags::interior));
    EXPECT_TRUE(!(ent->flags & nw::AreaFlags::natural));
    EXPECT_GT(ent->tiles.size(), 0);
    EXPECT_EQ(ent->tiles[0].id, 68);
    EXPECT_EQ(ent->height, 16);
    EXPECT_EQ(ent->width, 16);

    EXPECT_TRUE(ent->creatures.size() > 0);
    EXPECT_EQ(ent->creatures[0]->common.resref, "test_creature");
    EXPECT_EQ(ent->creatures[0]->stats.get_ability_score(nwn1::ability_strength), 20);
}

TEST(Area, JsonRoundtrip)
{
    auto ent = new nw::Area;
    nw::Gff are{"test_data/user/development/test_area.are"};
    EXPECT_TRUE(are.valid());
    nw::Gff git{"test_data/user/development/test_area.git"};
    EXPECT_TRUE(git.valid());
    nw::Gff gic{"test_data/user/development/test_area.gic"};
    EXPECT_TRUE(gic.valid());

    deserialize(ent, are.toplevel(), git.toplevel(), gic.toplevel());

    nlohmann::json j;
    nw::serialize(ent, j);

    auto ent2 = new nw::Area;
    nw::deserialize(ent2, j);
    EXPECT_TRUE(ent2);

    nlohmann::json j2;
    nw::serialize(ent2, j2);

    EXPECT_EQ(j, j2);
}
