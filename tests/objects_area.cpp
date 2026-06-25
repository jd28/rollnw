#include <gtest/gtest.h>

#include <nw/objects/Area.hpp>
#include <nw/objects/Common.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Location.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
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

TEST(Location, DeserializeGitBareCoordinates)
{
    fs::create_directories("tmp");

    constexpr float bearing = 1.25f;
    nw::GffBuilder out{"GFF"};
    out.top.add_field("Area", uint32_t{42})
        .add_field("X", 12.5f)
        .add_field("Y", 34.25f)
        .add_field("Z", -0.5f)
        .add_field("Bearing", bearing);
    out.build();
    ASSERT_TRUE(out.write_to("tmp/location_bare_coordinates.gff"));

    nw::Gff in{"tmp/location_bare_coordinates.gff"};
    ASSERT_TRUE(in.valid());

    nw::Location loc;
    EXPECT_TRUE(deserialize(loc, in.toplevel(), nw::SerializationProfile::instance));
    EXPECT_EQ(loc.area, nw::ObjectID{42});
    EXPECT_FLOAT_EQ(loc.position.x, 12.5f);
    EXPECT_FLOAT_EQ(loc.position.y, 34.25f);
    EXPECT_FLOAT_EQ(loc.position.z, -0.5f);
    EXPECT_FLOAT_EQ(loc.orientation.x, std::cos(bearing));
    EXPECT_FLOAT_EQ(loc.orientation.y, std::sin(bearing));
    EXPECT_FLOAT_EQ(loc.orientation.z, 0.0f);
}

TEST(Common, DeserializeGitBareCoordinatesWithoutArea)
{
    fs::create_directories("tmp");

    constexpr float bearing = 0.75f;
    nw::GffBuilder out{"GFF"};
    out.top.add_field("TemplateResRef", nw::Resref{"plc_test"})
        .add_field("Tag", nw::String{"plc_test"})
        .add_field("X", 97.0f)
        .add_field("Y", 139.0f)
        .add_field("Z", 0.25f)
        .add_field("Bearing", bearing);
    out.build();
    ASSERT_TRUE(out.write_to("tmp/common_bare_coordinates_no_area.gff"));

    nw::Gff in{"tmp/common_bare_coordinates_no_area.gff"};
    ASSERT_TRUE(in.valid());

    nw::Common common;
    EXPECT_TRUE(deserialize(common, in.toplevel(), nw::SerializationProfile::instance, nw::ObjectType::placeable));
    EXPECT_EQ(common.location.area, nw::object_invalid);
    EXPECT_FLOAT_EQ(common.location.position.x, 97.0f);
    EXPECT_FLOAT_EQ(common.location.position.y, 139.0f);
    EXPECT_FLOAT_EQ(common.location.position.z, 0.25f);
    EXPECT_FLOAT_EQ(common.location.orientation.x, std::cos(bearing));
    EXPECT_FLOAT_EQ(common.location.orientation.y, std::sin(bearing));
    EXPECT_FLOAT_EQ(common.location.orientation.z, 0.0f);
}
