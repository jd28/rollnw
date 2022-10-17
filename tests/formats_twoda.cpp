#include <catch2/catch_all.hpp>

#include <nw/formats/TwoDA.hpp>
#include <nw/log.hpp>

#include <cstring>
#include <fstream>

TEST_CASE("TwoDA Parse", "[formats]")
{
    nw::TwoDA feat("test_data/user/development/feat.2da");
    REQUIRE(feat.is_valid());
    REQUIRE(feat.rows() == 1116);
    REQUIRE(*feat.get<std::string_view>(4, 0) == "ArmProfMed");
    REQUIRE(*feat.get<int32_t>(0, "FEAT") == 289);

    auto row = feat.row(12);
    REQUIRE(row.size() == feat.columns());
    REQUIRE(*feat.get<int32_t>(12, "FEAT") == *row.get<int32_t>("FEAT"));

    feat.set(0, 1, 10);
    REQUIRE(!!feat.get<int32_t>(0, 1));
    REQUIRE(*feat.get<int32_t>(0, 1) == 10);
    feat.set(0, 1, 10.0f);
    REQUIRE(*feat.get<float>(0, 1) == 10.0f);
    feat.set(0, 1, "test");
    REQUIRE(*feat.get<std::string>(0, 1) == "test");

    nw::TwoDA empty("test_data/user/development/empty.2da");
    REQUIRE(!empty.is_valid());

    std::ofstream out{"tmp/feat_reform.2da", std::ios_base::binary};
    out << feat;
}
