#include <catch2/catch.hpp>

#include <nw/objects/Door.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("door: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/door_ttr_002.utd"};
    REQUIRE(g.valid());
    nw::Door d{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(d.valid());

    REQUIRE(d.common()->resref == "door_ttr_002");
    REQUIRE(d.appearance == 0);
    REQUIRE(!d.plot);
    REQUIRE(!d.lock.locked);
}

TEST_CASE("door: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/door_ttr_002.utd"};
    REQUIRE(g.valid());
    nw::Door d{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(d.valid());

    nlohmann::json j = d.to_json(nw::SerializationProfile::blueprint);
    nw::Door d2{j, nw::SerializationProfile::blueprint};
    REQUIRE(d2.valid());

    nlohmann::json j2 = d2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);

    nw::Lock l;
    l.from_json(d.lock.to_json());
    REQUIRE(l.lockable == d.lock.lockable);

    std::ofstream f{"tmp/door_ttr_002.utd.json"};
    f << std::setw(4) << j;
}
