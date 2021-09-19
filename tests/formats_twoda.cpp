#include <catch2/catch.hpp>

#include "nw/log.hpp"

#include "nw/formats/TwoDA.hpp"

#include <cstring>
#include <fstream>

TEST_CASE("TwoDA Parse", "[formats]")
{
    nw::TwoDA feat("test_data/feat.2da");
    REQUIRE(feat.is_valid());
    REQUIRE(feat.rows() == 1116);
    REQUIRE(*feat.get<std::string_view>(4, 0) == "ArmProfMed");
    REQUIRE(*feat.get<int32_t>(0, "FEAT") == 289);

    nw::TwoDA empty("test_data/empty.2da");
    REQUIRE(!empty.is_valid());

    std::ofstream out{"tmp/feat_reform.2da"};
    out << feat;
}
