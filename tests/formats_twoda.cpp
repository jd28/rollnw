#include <gtest/gtest.h>

#include <nw/formats/StaticTwoDA.hpp>
#include <nw/formats/TwoDA.hpp>
#include <nw/log.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Parsed against a synthetic fixture rather than a shipped game table. These
// assertions used to encode game facts -- feat.2da's 1116 rows, its 43rd
// column, the label in row 4 -- which tested the game's data as much as the
// parser, and tied the parser tests to content the repo should not carry.
//
// twoda_parse.2da: 4 columns (LABEL, VALUE, RATIO, FLAG), 5 rows, with row 2
// entirely **** so the empty-cell path is covered by construction.
namespace {
constexpr const char* k_parse_fixture = "test_data/user/development/twoda_parse.2da";
constexpr size_t k_fixture_rows = 5;
constexpr size_t k_fixture_columns = 4;
} // namespace

TEST(StaticTwoDA, Parse)
{
    nw::StaticTwoDA tda{fs::path{k_parse_fixture}};
    ASSERT_TRUE(tda.is_valid());
    EXPECT_EQ(tda.rows(), k_fixture_rows);
    EXPECT_EQ(tda.columns(), k_fixture_columns);
    EXPECT_EQ(*tda.get<nw::StringView>(3, 0), "Charlie");
    EXPECT_EQ(*tda.get<int32_t>(0, "VALUE"), 289);
    EXPECT_EQ(*tda.get<float>(1, "RATIO"), 0.25f);
    EXPECT_EQ(*tda.get<int32_t>(4, "VALUE"), -5);

    // An all-**** row yields no values but still has the table's width.
    auto empty_row = tda.row(2);
    EXPECT_EQ(empty_row.size(), tda.columns());
    EXPECT_FALSE(!!tda.get<nw::StringView>(2, 0));

    auto row = tda.row(3);
    EXPECT_EQ(row.size(), tda.columns());
    EXPECT_EQ(*tda.get<int32_t>(3, "VALUE"), *row.get<int32_t>("VALUE"));
}

TEST(TwoDA, Parse)
{
    nw::TwoDA tda{fs::path{k_parse_fixture}};
    ASSERT_TRUE(tda.is_valid());
    EXPECT_EQ(tda.rows(), k_fixture_rows);
    EXPECT_EQ(*tda.get<nw::StringView>(3, 0), "Charlie");
    EXPECT_EQ(*tda.get<int32_t>(0, "VALUE"), 289);

    // A cell's type follows whatever was last written to it.
    tda.set(0, 1, 10);
    EXPECT_TRUE(!!tda.get<int32_t>(0, 1));
    EXPECT_EQ(*tda.get<int32_t>(0, 1), 10);
    tda.set(0, 1, 10.0f);
    EXPECT_EQ(*tda.get<float>(0, 1), 10.0f);
    tda.set(0, 1, "test");
    EXPECT_EQ(*tda.get<nw::String>(0, 1), "test");

    // A new column lands at the end, and lookup is case-insensitive.
    EXPECT_TRUE(tda.add_column("test"));
    EXPECT_EQ(tda.column_index("Test"), k_fixture_columns);

    nw::TwoDA empty(fs::path("test_data/user/development/empty.2da"));
    ASSERT_FALSE(empty.is_valid());

    std::ofstream out{"tmp/twoda_reform.2da", std::ios_base::binary};
    out << tda;
}

TEST(TwoDA, AddColumn)
{
    nw::TwoDA tda{fs::path{k_parse_fixture}};
    ASSERT_TRUE(tda.is_valid());
    EXPECT_TRUE(tda.add_column("FakeItemBitMask"));
    EXPECT_TRUE(tda.add_column("AnotherColumn"));
    EXPECT_EQ(tda.column_index("FakeItemBitMask"), k_fixture_columns);
    EXPECT_EQ(tda.column_index("AnotherColumn"), k_fixture_columns + 1);

    std::ofstream out{"tmp/twoda_addcolumn.2da", std::ios_base::binary};
    out << tda;
}
