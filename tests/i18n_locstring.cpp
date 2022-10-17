#include <catch2/catch_all.hpp>

#include <nw/i18n/LocString.hpp>
#include <nw/log.hpp>

#include <nlohmann/json.hpp>

TEST_CASE("locstring: to/from_json", "[i18n]")
{
    nw::LocString l{1};
    l.add(nw::LanguageID::english, "test");
    l.add(nw::LanguageID::french, "french test");

    nlohmann::json j;
    j["strref"] = 1;
    j["strings"] = nlohmann::json::array();
    j["strings"].push_back({{"lang", 0}, {"string", "test"}});
    j["strings"].push_back({{"lang", 2}, {"string", "french test"}});

    nlohmann::json locjson = l;

    REQUIRE(locjson == j);

    nw::LocString l2 = locjson.get<nw::LocString>();
    REQUIRE(l2 == l);
}
