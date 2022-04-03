#include <catch2/catch.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>

TEST_CASE("strings manager", "[kernel]")
{
    nw::kernel::strings().load_dialog_tlk("test_data/root/data/dialog.tlk");
    nw::kernel::strings().load_custom_tlk("test_data/root/data/dialog.tlk");

    REQUIRE(nw::kernel::strings().get(1000) == "Silence");
    REQUIRE(nw::kernel::strings().get(0x01001000) == "Stay here and don't move until I return.");
    REQUIRE(nw::kernel::strings().get(0xFFFFFFFF) == "");
}

TEST_CASE("strings intern", "[kernel]")
{
    auto str = nw::kernel::strings().intern("This is a Test");
    REQUIRE(str == "This is a Test");
}
