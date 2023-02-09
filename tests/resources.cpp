#include <gtest/gtest.h>

#include <nw/resources/Resource.hpp>

#include <nlohmann/json.hpp>

using namespace std::literals;

TEST(ResourceType, Conversion)
{
    EXPECT_EQ(nw::ResourceType::from_extension("2da"), nw::ResourceType::twoda);
    EXPECT_EQ(nw::ResourceType::from_extension(".2da"), nw::ResourceType::twoda);
    EXPECT_EQ(nw::ResourceType::from_extension("xxx"), nw::ResourceType::invalid);

    EXPECT_EQ(nw::ResourceType::to_string(nw::ResourceType::txi), "txi"s);
}

TEST(Resref, JsonConversion)
{
    nw::Resref r{"test"};
    nlohmann::json json_resref = r;
    EXPECT_TRUE(json_resref.is_string());
    nw::Resref r2 = json_resref.get<nw::Resref>();
    EXPECT_EQ(r, r2);
    EXPECT_TRUE(r2.length());
}

TEST(Resource, JsonConversion)
{
    nw::Resource r{"test"sv, nw::ResourceType::twoda};
    nlohmann::json resource_json = r;
    EXPECT_EQ(r.filename(), resource_json.get<std::string>());

    nw::Resource r2 = resource_json.get<nw::Resource>();
    EXPECT_EQ(r, r2);
}
