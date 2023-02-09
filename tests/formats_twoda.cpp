#include <gtest/gtest.h>

#include <nw/formats/TwoDA.hpp>
#include <nw/log.hpp>

#include <cstring>
#include <fstream>

TEST(TwoDA, Parse)
{
    nw::TwoDA feat("test_data/user/development/feat.2da");
    EXPECT_TRUE(feat.is_valid());
    EXPECT_TRUE(feat.rows() == 1116);
    EXPECT_EQ(*feat.get<std::string_view>(4, 0), "ArmProfMed");
    EXPECT_EQ(*feat.get<int32_t>(0, "FEAT"), 289);

    auto row = feat.row(12);
    EXPECT_EQ(row.size(), feat.columns());
    EXPECT_EQ(*feat.get<int32_t>(12, "FEAT"), *row.get<int32_t>("FEAT"));

    feat.set(0, 1, 10);
    EXPECT_TRUE(!!feat.get<int32_t>(0, 1));
    EXPECT_EQ(*feat.get<int32_t>(0, 1), 10);
    feat.set(0, 1, 10.0f);
    EXPECT_EQ(*feat.get<float>(0, 1), 10.0f);
    feat.set(0, 1, "test");
    EXPECT_EQ(*feat.get<std::string>(0, 1), "test");

    nw::TwoDA empty("test_data/user/development/empty.2da");
    EXPECT_FALSE(empty.is_valid());

    std::ofstream out{"tmp/feat_reform.2da", std::ios_base::binary};
    out << feat;
}
