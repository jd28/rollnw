#include <catch2/catch.hpp>

#include <nw/i18n/Tlk.hpp>
#include <nw/log.hpp>

TEST_CASE("Tlk", "[formats]")
{
    nw::Tlk t = nw::Tlk("test_data/dialog.tlk");
    REQUIRE(t.is_valid());
    REQUIRE(t.size() > 0);
    REQUIRE(t.get(1000) == "Silence");
    REQUIRE(t.get(-1u) == "");
}
