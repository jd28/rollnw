#include <catch2/catch.hpp>

#include <nw/i18n/TlkTable.hpp>
#include <nw/log.hpp>

TEST_CASE("Tlk Table", "[i18n]")
{
    nw::TlkTable t{"test_data/dialog.tlk", "test_data/dialog.tlk"};
    REQUIRE(t.is_valid());
    REQUIRE(t.get(1000) == "Silence");
    REQUIRE(t.get(0x01001000) == "Stay here and don't move until I return.");
    REQUIRE(t.get(~0) == "");
}
