#include <gtest/gtest.h>

#include <nw/kernel/Objects.hpp>
#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/serialization/Archives.hpp>
#include <nwn1/Profile.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST(Area, GffDeserialize)
{
    auto ent = nw::kernel::objects().make<nw::Area>();
    nw::Gff are{"test_data/user/development/test_area.are"};
    EXPECT_TRUE(are.valid());
    nw::Gff git{"test_data/user/development/test_area.git"};
    EXPECT_TRUE(git.valid());
    nw::Gff gic{"test_data/user/development/test_area.gic"};
    EXPECT_TRUE(gic.valid());

    deserialize(ent, are.toplevel(), git.toplevel(), gic.toplevel());

    EXPECT_EQ(ent->tileset, "ttf02");
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
    nw::Area::serialize(ent, j);

    auto ent2 = new nw::Area;
    nw::Area::deserialize(ent2, j);
    EXPECT_TRUE(ent2);

    nlohmann::json j2;
    nw::Area::serialize(ent2, j2);

    EXPECT_EQ(j, j2);
}
