#include <catch2/catch.hpp>

#include <nw/objects/Trigger.hpp>
#include <nw/serialization/Serialization.hpp>

#include <nlohmann/json.hpp>

#include <fstream>

TEST_CASE("trigger: from_gff", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_spray_sewage.utt"};
    REQUIRE(g.valid());
    nw::Trigger t{g.toplevel(), nw::SerializationProfile::blueprint};
    REQUIRE(t.common()->resref == "pl_spray_sewage");
    REQUIRE(t.portrait == 0);
    REQUIRE(t.loadscreen == 0);
    REQUIRE(t.scripts.on_enter == "pl_trig_sewage");
}

TEST_CASE("trigger: to_json", "[objects]")
{
    nw::GffInputArchive g{"test_data/pl_spray_sewage.utt"};
    REQUIRE(g.valid());
    nw::Trigger t{g.toplevel(), nw::SerializationProfile::blueprint};

    nlohmann::json j = t.to_json(nw::SerializationProfile::blueprint);
    nw::Trigger t2{j, nw::SerializationProfile::blueprint};
    nlohmann::json j2 = t2.to_json(nw::SerializationProfile::blueprint);
    REQUIRE(j == j2);

    std::ofstream f{"tmp/pl_spray_sewage.utt.json"};
    f << std::setw(4) << j;
}
