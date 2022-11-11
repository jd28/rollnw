#include <catch2/catch_all.hpp>

#include <nw/kernel/Kernel.hpp>
#include <nw/kernel/Strings.hpp>
#include <nw/log.hpp>

TEST_CASE("strings manager", "[kernel]")
{
    nw::kernel::strings().load_dialog_tlk("test_data/root/lang/en/data/dialog.tlk");
    nw::kernel::strings().load_custom_tlk("test_data/root/lang/en/data/dialog.tlk");

    REQUIRE(nw::kernel::strings().get(1000) == "Silence");
    REQUIRE(nw::kernel::strings().get(0x01001000) == "Stay here and don't move until I return.");
    REQUIRE(nw::kernel::strings().get(0xFFFFFFFF) == "");

    nw::LocString test{1000};
    REQUIRE(nw::kernel::strings().get(test) == "Silence");
    test.add(nw::LanguageID::english, "Silencio");
    REQUIRE(nw::kernel::strings().get(test) == "Silencio");
}

TEST_CASE("strings intern", "[kernel]")
{
    auto str = nw::kernel::strings().intern("This is a Test");
    REQUIRE(str == "This is a Test");

    auto str2 = nw::kernel::strings().get_interned("asdf;lkj");
    REQUIRE_FALSE(str2);

    auto str3 = nw::kernel::strings().get_interned("This is a Test");
    REQUIRE(str3);
}
