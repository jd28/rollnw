#include <gtest/gtest.h>

#include <nw/i18n/LocString.hpp>
#include <nw/log.hpp>

#include <nlohmann/json.hpp>

TEST(LocString, JsonConversion)
{
    nw::LocString l{1};

    EXPECT_EQ(l.size(), 0);
    l.add(nw::LanguageID::english, "test");
    EXPECT_TRUE(l.contains(nw::LanguageID::english));
    EXPECT_FALSE(l.contains(nw::LanguageID::french));
    l.add(nw::LanguageID::french, "french test");
    EXPECT_TRUE(l.contains(nw::LanguageID::french));
    EXPECT_EQ(l.size(), 2);

    EXPECT_EQ(l.strref(), 1);
    l.set_strref(2);
    EXPECT_EQ(l.strref(), 2);

    nlohmann::json j;
    j["strref"] = 2;
    j["strings"] = nlohmann::json::array();
    j["strings"].push_back({{"lang", 0}, {"string", "test"}});
    j["strings"].push_back({{"lang", 2}, {"string", "french test"}});

    nlohmann::json locjson = l;

    EXPECT_EQ(locjson, j);

    nw::LocString l2 = locjson.get<nw::LocString>();
    EXPECT_EQ(l2, l);
}
