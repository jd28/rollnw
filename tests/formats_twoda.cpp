#include <gtest/gtest.h>

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/log.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST(StaticTwoDA, Parse)
{
    nw::StaticTwoDA feat(fs::path("test_data/user/development/feat.2da"));
    EXPECT_TRUE(feat.is_valid());
    EXPECT_EQ(feat.rows(), 1116);
    EXPECT_EQ(*feat.get<nw::StringView>(4, 0), "ArmProfMed");
    EXPECT_EQ(*feat.get<int32_t>(0, "FEAT"), 289);

    auto row = feat.row(12);
    EXPECT_EQ(row.size(), feat.columns());
    EXPECT_EQ(*feat.get<int32_t>(12, "FEAT"), *row.get<int32_t>("FEAT"));
}

TEST(TwoDA, Parse)
{
    nw::TwoDA feat(fs::path("test_data/user/development/feat.2da"));
    EXPECT_TRUE(feat.is_valid());
    EXPECT_EQ(feat.rows(), 1116);
    EXPECT_EQ(*feat.get<nw::StringView>(4, 0), "ArmProfMed");
    EXPECT_EQ(*feat.get<int32_t>(0, "FEAT"), 289);

    feat.set(0, 1, 10);
    EXPECT_TRUE(!!feat.get<int32_t>(0, 1));
    EXPECT_EQ(*feat.get<int32_t>(0, 1), 10);
    feat.set(0, 1, 10.0f);
    EXPECT_EQ(*feat.get<float>(0, 1), 10.0f);
    feat.set(0, 1, "test");
    EXPECT_EQ(*feat.get<nw::String>(0, 1), "test");

    EXPECT_TRUE(feat.add_column("test"));
    EXPECT_EQ(feat.column_index("Test"), 43);

    nw::TwoDA empty(fs::path("test_data/user/development/empty.2da"));
    EXPECT_FALSE(empty.is_valid());

    std::ofstream out{"tmp/feat_reform.2da", std::ios_base::binary};
    out << feat;
}

TEST(TwoDA, AddColumn)
{
    nw::TwoDA polymorph(fs::path("test_data/user/development/polymorph.2da"));
    EXPECT_TRUE(polymorph.is_valid());
    EXPECT_TRUE(polymorph.add_column("FakeItemBitMask"));
    EXPECT_TRUE(polymorph.add_column("AnotherColumn"));

    std::ofstream out{"tmp/polymorph.2da", std::ios_base::binary};
    out << polymorph;
}
