#include <gtest/gtest.h>

#include <nw/objects/Area.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/Location.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/constants.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/serialization/Gff.hpp>
#include <nw/serialization/GffBuilder.hpp>
#include <nw/serialization/gff_conversion.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

int32_t creature_ability_score_from_script(const nw::Creature* creature, nw::Ability ability, bool base = false)
{
    if (!creature || ability == nw::Ability::invalid()) { return 0; }

    nw::Vector<nw::smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(nw::smalls::Value::make_int(*ability));
    args.push_back(nw::smalls::Value::make_bool(base));
    return nwn1::bridge::call_nwn1_module_int("nwn1.creature", "get_ability_score", args).value_or(0);
}

} // namespace

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
    EXPECT_EQ(ent->creatures[0]->resref, "test_creature");
    EXPECT_EQ(creature_ability_score_from_script(ent->creatures[0], nwn1::ability_strength, true), 20);
}

TEST(Area, JsonSerializesSubobjectsAsPropsetsComponents)
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

    EXPECT_EQ(j.at("$type"), "CAF");
    EXPECT_FALSE(j.contains("common"));
    EXPECT_TRUE(j.contains("object"));
    EXPECT_TRUE(j.contains("components"));
    EXPECT_TRUE(j.at("components").contains("locals"));
    ASSERT_TRUE(j.contains("creatures"));
    ASSERT_FALSE(j.at("creatures").empty());

    const auto& creature = j.at("creatures").at(0);
    EXPECT_FALSE(creature.contains("$type"));
    EXPECT_FALSE(creature.contains("$version"));
    EXPECT_TRUE(creature.contains("components"));
    EXPECT_TRUE(creature.contains("core.creature.CreatureDescriptor"));
    EXPECT_TRUE(creature.contains("core.creature.CreatureStats"));

    for (const char* list : {
             "creatures",
             "doors",
             "encounters",
             "items",
             "placeables",
             "sounds",
             "stores",
             "triggers",
             "waypoints",
         }) {
        ASSERT_TRUE(j.contains(list)) << list;
        ASSERT_TRUE(j.at(list).is_array()) << list;
        for (const auto& embedded : j.at(list)) {
            EXPECT_FALSE(embedded.contains("$type")) << list;
            EXPECT_FALSE(embedded.contains("$version")) << list;
            EXPECT_TRUE(embedded.contains("components")) << list;
        }
    }

    auto roundtrip = new nw::Area;
    ASSERT_TRUE(nw::deserialize(roundtrip, j));
    ASSERT_FALSE(roundtrip->creatures.empty());
    EXPECT_EQ(roundtrip->creatures[0]->resref, "test_creature");

    nlohmann::json roundtrip_json;
    nw::serialize(roundtrip, roundtrip_json);

    const auto& roundtrip_creature = roundtrip_json.at("creatures").at(0);
    EXPECT_FALSE(roundtrip_creature.contains("$type"));
    EXPECT_FALSE(roundtrip_creature.contains("$version"));
    EXPECT_TRUE(roundtrip_creature.contains("object"));
    EXPECT_TRUE(roundtrip_creature.at("object").contains("resref"));
    EXPECT_TRUE(roundtrip_creature.contains("components"));
    EXPECT_FALSE(roundtrip_creature.at("components").contains("resref"));
    EXPECT_TRUE(roundtrip_creature.contains("core.creature.CreatureDescriptor"));
    EXPECT_TRUE(roundtrip_creature.contains("core.creature.CreatureStats"));
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

TEST(Location, DeserializeGitBareCoordinatesWithoutArea)
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

    nw::Location loc;
    EXPECT_TRUE(deserialize(loc, in.toplevel(), nw::SerializationProfile::instance));
    EXPECT_EQ(loc.area, nw::object_invalid);
    EXPECT_FLOAT_EQ(loc.position.x, 97.0f);
    EXPECT_FLOAT_EQ(loc.position.y, 139.0f);
    EXPECT_FLOAT_EQ(loc.position.z, 0.25f);
    EXPECT_FLOAT_EQ(loc.orientation.x, std::cos(bearing));
    EXPECT_FLOAT_EQ(loc.orientation.y, std::sin(bearing));
    EXPECT_FLOAT_EQ(loc.orientation.z, 0.0f);
}
