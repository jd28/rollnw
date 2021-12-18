#include <catch2/catch.hpp>

#include <nw/objects/Creature.hpp>
#include <nw/serialization/Serialization.hpp>

TEST_CASE("Loading nw_chicken", "[objects]")
{
    nw::GffInputArchive g{"test_data/nw_chicken.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(c.common()->resref == "nw_chicken");
    REQUIRE(c.stats.abilities[2] == 8);
    REQUIRE(c.on_attacked == "nw_c2_default5");
    REQUIRE(c.appearance.id == 31);
    REQUIRE(c.gender == 1);
}

TEST_CASE("Loading test_creature", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_agent_001.utc"};
    nw::Creature c{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(c.common()->resref == "pl_agent_001");
    REQUIRE(c.stats.abilities[2] == 16);
    REQUIRE(c.on_attacked == "mon_ai_5attacked");
    REQUIRE(c.appearance.id == 6);
    REQUIRE(c.appearance.body_parts.left_shin == 1);
    REQUIRE(c.stats.feats.size() == 37);
    REQUIRE(c.soundset == 171);
    REQUIRE(std::get<nw::Resref>(c.equipment.chest) == "dk_agent_thread2");
    REQUIRE(c.combat_info.ac_natural == 0);
    REQUIRE(c.combat_info.special_abilities.size() == 1);
    REQUIRE(c.combat_info.special_abilities[0].spell == 120);
}
