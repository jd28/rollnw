#include <catch2/catch_all.hpp>

#include <nw/i18n/Tlk.hpp>
#include <nw/kernel/Kernel.hpp>
#include <nw/log.hpp>

TEST_CASE("Tlk", "[formats]")
{
    nw::Tlk t = nw::Tlk("test_data/root/lang/en/data/dialog.tlk");
    REQUIRE(t.valid());
    REQUIRE(t.size() > 0);
    REQUIRE(t.get(1000) == "Silence");
    REQUIRE(t.get(0xFFFFFFFF) == "");
}

TEST_CASE("tlk: load languages")
{
    auto install_path = nw::kernel::config().options().install;
    nw::Tlk de{"test_data/root/lang/de/data/dialog.tlk"};
    REQUIRE(de.valid());
    REQUIRE(de.get(10) == "Mönch");
    de.save_as("tmp/dialog.tlk");
    nw::Tlk t2 = nw::Tlk{"tmp/dialog.tlk"};
    REQUIRE(t2.valid());
    REQUIRE(de.get(10) == "Mönch");
}

TEST_CASE("tlk: set", "[i18n]")
{
    nw::Tlk t = nw::Tlk("test_data/root/lang/en/data/dialog.tlk");
    REQUIRE(t.valid());
    t.set(1, "Hello World");
    REQUIRE(t.get(1) == "Hello World");
}

TEST_CASE("tlk: save_as", "[i18n]")
{
    nw::Tlk t = nw::Tlk("test_data/root/lang/en/data/dialog.tlk");
    REQUIRE(t.valid());
    t.set(1, "Hello World");
    t.save_as("tmp/dialog.tlk");
    nw::Tlk t2 = nw::Tlk{"tmp/dialog.tlk"};
    REQUIRE(t2.valid());
    REQUIRE(t2.size() > 0);
    REQUIRE(t2.get(1) == "Hello World");
    REQUIRE(t2.get(1000) == "Silence");
    REQUIRE(t2.get(0xFFFFFFFF) == "");
}
