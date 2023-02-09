#include <gtest/gtest.h>

#include <nw/legacy/LocString.hpp>
#include <nw/log.hpp>

#include <nlohmann/json.hpp>

TEST(LocString, JsonConversion)
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

    EXPECT_TRUE(locjson == j);

    nw::LocString l2 = locjson.get<nw::LocString>();
    EXPECT_TRUE(l2 == l);
}
